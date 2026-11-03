/*
 * dsme-slot-operation.c -- The multi-superframe state machine.
 *
 * Structure follows Contiki-NG's TSCH slot operation, for the same reason:
 * on a Cooja mote a busy-wait only advances simulated time in 1 ms steps,
 * whereas an rtimer fires at the exact microsecond it was set for (the mote's
 * next wake-up is taken straight from simRtimerNextExpirationTime). Every
 * instant inside a slot that has to be accurate is therefore reached by
 * scheduling an rtimer and yielding a protothread, never by spinning.
 *
 * One slot is handled per rtimer firing:
 *
 *   slot 0        beacon: transmit if this superframe is ours, else listen
 *   slots 1..8    CAP: slotted CSMA-CA for GTS/extGTS commands,
 *                 or extCFP / listen-only period once converted
 *   slots 9..15   CFP: TDMA transmission in an allocated DSME-GTS
 */

#include "contiki.h"
#include "net/netstack.h"
#include "dev/radio.h"
#include "dev/watchdog.h"
#include "lib/random.h"
#include "sys/rtimer.h"

#include "dsme.h"
#include "dsme-slot-operation.h"
#include "dsme-packet.h"
#include "dsme-queue.h"
#include "dsme-act.h"
#include "dsme-sab.h"
#include "dsme-gts.h"
#include "dsme-tacfpext.h"
#include "dsme-predictor.h"
#include "dsme-log.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
#define US(x) ((rtimer_clock_t)US_TO_RTIMERTICKS(x))

static struct rtimer slot_timer;
static struct pt slot_pt;
static struct pt sub_pt;

static rtimer_clock_t slot_ref;     /* scheduled start of the current slot */
static uint8_t running;
static uint8_t data_seqno;
static uint8_t beacon_seqno;

static uint8_t tx_buf[128];
static uint8_t rx_buf[128];

/* Set by the beacon handler to hand a fresh synchronisation to the main loop. */
static uint8_t just_joined;

/*---------------------------------------------------------------------------*/
static void slot_timer_callback(struct rtimer *t, void *ptr);
static char slot_operation(struct rtimer *t, void *ptr);

/*---------------------------------------------------------------------------*/
/* rtimer plumbing                                                            */
/*---------------------------------------------------------------------------*/
/*
 * \return 1 when an rtimer was armed and the caller should yield, 0 when the
 *         instant has already passed and the caller should carry straight on.
 */
static int
schedule_at(rtimer_clock_t ref, rtimer_clock_t offset)
{
  rtimer_clock_t target = ref + offset;

  if(RTIMER_CLOCK_DIFF(target, RTIMER_NOW()) <= 1) {
    return 0;
  }
  return rtimer_set(&slot_timer, target, 1, slot_timer_callback, NULL) == RTIMER_OK;
}

#define DSME_YIELD_AT(pt, ref, offset)            \
  do {                                            \
    if(schedule_at((ref), US(offset))) {          \
      PT_YIELD(pt);                               \
    }                                             \
  } while(0)

/* Same, but with an offset already expressed in rtimer ticks. */
#define DSME_YIELD_AT_TICKS(pt, ref, offset)      \
  do {                                            \
    if(schedule_at((ref), (offset))) {            \
      PT_YIELD(pt);                               \
    }                                             \
  } while(0)

/*---------------------------------------------------------------------------*/
/* Radio helpers                                                              */
/*---------------------------------------------------------------------------*/
static void
set_channel(uint8_t ch)
{
  NETSTACK_RADIO.set_value(RADIO_PARAM_CHANNEL, (radio_value_t)ch);
}
/*---------------------------------------------------------------------------*/
#if DSME_LOG_LEVEL > 1
/* Channel the radio reports it is actually on. The channel trace logs this
 * rather than the value we asked for, so the trace is evidence that the
 * retune took effect rather than a restatement of the request. */
static uint8_t
radio_channel(void)
{
  radio_value_t v = 0;
  NETSTACK_RADIO.get_value(RADIO_PARAM_CHANNEL, &v);
  return (uint8_t)v;
}
#endif /* DSME_LOG_LEVEL > 1 */
/*---------------------------------------------------------------------------*/
static int
read_frame(void)
{
  if(!NETSTACK_RADIO.pending_packet()) {
    return 0;
  }
  return NETSTACK_RADIO.read(rx_buf, sizeof(rx_buf));
}
/*---------------------------------------------------------------------------*/
static rtimer_clock_t
frame_ticks(uint8_t len)
{
  return US((uint32_t)len * DSME_US_PER_OCTET);
}
/*---------------------------------------------------------------------------*/
/* Beacon handling                                                            */
/*---------------------------------------------------------------------------*/
/*
 * Reference B, Section 5.1.2: every coordinator sends its EB in one superframe
 * of the beacon interval, picked by a distributed beacon-scheduling algorithm.
 * Beacon scheduling is not the object of this study, so the superframe is
 * assigned statically from the short address. Cooja motes share one simulated
 * clock and so do not drift, which means a single successful beacon reception
 * keeps a node synchronised for the whole run.
 */
static uint8_t
beacon_superframe_of(uint16_t addr)
{
  return (uint8_t)(addr % DSME_SF_PER_BI);
}
/*---------------------------------------------------------------------------*/
static uint8_t
we_beacon_now(void)
{
  uint32_t sf_in_bi = dsme_sf_of(dsme_time.asn) % DSME_SF_PER_BI;

  if(dsme_is_pan_coordinator) {
    return sf_in_bi == 0;
  }
  if(!dsme_joined) {
    return 0;
  }
  /* Superframe 0 belongs to the PAN coordinator. */
  return beacon_superframe_of(dsme_short_addr) == sf_in_bi && sf_in_bi != 0;
}
/*---------------------------------------------------------------------------*/
static void
handle_beacon(const uint8_t *buf, int len)
{
  dsme_beacon_info_t info;
  rtimer_clock_t rx_start;

  if(!dsme_pkt_parse_beacon(buf, (uint8_t)len, &info)) {
    return;
  }
  if(info.so != DSME_SO || info.mo != DSME_MO || info.bo != DSME_BO) {
    return; /* a PAN with a different structure */
  }

  dsme_time.pan_bsn = info.pan_bsn;

  if(dsme_joined) {
    return;
  }

  /* The beacon left the air interface at (beacon slot start + TsTxOffset), so
   * the reception timestamp pins the sender's slot grid exactly. */
  if(NETSTACK_RADIO.get_object(RADIO_PARAM_LAST_PACKET_TIMESTAMP, &rx_start,
                               sizeof(rx_start)) != RADIO_RESULT_OK) {
    return;
  }
  slot_ref = rx_start - US(DSME_TS_TX_OFFSET_US) + DSME_SLOT_DURATION;
  dsme_time.asn = info.asn + 1;
  dsme_joined = 1;
  just_joined = 1;
  DSME_LOG("JOIN|asn=%lu|n=%u|bsn=%u",
           (unsigned long)dsme_time.asn, dsme_short_addr, info.pan_bsn);
}
/*---------------------------------------------------------------------------*/
/* Frame dispatch                                                             */
/*---------------------------------------------------------------------------*/
static void
process_frame(const uint8_t *buf, int len)
{
  dsme_hdr_t h;

  if(len <= 0 || !dsme_pkt_parse_hdr(buf, (uint8_t)len, &h)) {
    return;
  }
  if(h.src == dsme_short_addr) {
    return;
  }

  switch(h.type) {
  case DSME_FRAME_BEACON:
    handle_beacon(buf, len);
    break;

  case DSME_FRAME_CMD: {
    dsme_cmd_info_t info;
    if(dsme_pkt_parse_cmd(buf, (uint8_t)len, &info)) {
      dsme_gts_rx_cmd(&h, &info);
    }
    break;
  }

  case DSME_FRAME_DATA:
    if(h.dst == dsme_short_addr) {
      dsme_stats.rx_data++;
      DSME_LOG_DBG("RX|asn=%lu|n=%u|from=%u|seq=%u",
                   (unsigned long)dsme_time.asn, dsme_short_addr, h.src, h.seqno);
    }
    break;

  default:
    break;
  }
}
/*---------------------------------------------------------------------------*/
/* Beacon slot                                                                */
/*---------------------------------------------------------------------------*/
static
PT_THREAD(pt_beacon(struct pt *pt))
{
  static uint8_t len;

  PT_BEGIN(pt);

  if(we_beacon_now()) {
    DSME_YIELD_AT(pt, slot_ref, DSME_TS_TX_OFFSET_US);
    set_channel(DSME_CONTROL_CHANNEL);
    NETSTACK_RADIO.on();
    if(dsme_is_pan_coordinator) {
      /* macPANCoordinatorBSN advances once per beacon interval and drives the
       * channel hopping sequence of the whole PAN (Reference C, Eq. 1). */
      dsme_time.pan_bsn++;
    }
    len = dsme_pkt_build_beacon(tx_buf, beacon_seqno++, dsme_short_addr,
                                dsme_time.asn, dsme_time.pan_bsn);
    NETSTACK_RADIO.send(tx_buf, len);
  } else {
    DSME_YIELD_AT(pt, slot_ref, DSME_TS_RX_OFFSET_US);
    set_channel(DSME_CONTROL_CHANNEL);
    NETSTACK_RADIO.on();
    DSME_YIELD_AT(pt, slot_ref,
                  DSME_TS_TX_OFFSET_US + DSME_BEACON_LEN * DSME_US_PER_OCTET + 400);
    process_frame(rx_buf, read_frame());
  }

  PT_END(pt);
}
/*---------------------------------------------------------------------------*/
/* CAP slot (and listen-only period)                                          */
/*---------------------------------------------------------------------------*/
/*
 * Slotted CSMA-CA, IEEE 802.15.4-2015. The node listens across the whole slot
 * so that it can receive the commands of other pairs -- which is how the SAB
 * of a one-hop neighbourhood stays current -- and interleaves its own backoff
 * countdown with that listening. During a listen-only period the backoff is
 * simply never started: the CAP belongs to a neighbour's extCFP and must be
 * kept clear (Reference C, Section III-B).
 */
static
PT_THREAD(pt_cap(struct pt *pt, uint8_t may_transmit))
{
  static uint8_t nb, be, bp, attempted;
  static uint16_t next_try_bp;
  static dsme_cmd_t *cmd;
  static int rx_len;

  PT_BEGIN(pt);

  DSME_YIELD_AT(pt, slot_ref, DSME_TS_RX_OFFSET_US);
  set_channel(DSME_CONTROL_CHANNEL);
  NETSTACK_RADIO.on();

  attempted = 0;
  nb = 0;
  be = DSME_MAC_MIN_BE;
  next_try_bp = (uint16_t)(random_rand() % (1u << be));

  for(bp = 0; bp < DSME_CAP_BACKOFF_PERIODS; bp++) {
    DSME_YIELD_AT_TICKS(pt, slot_ref,
                        US(DSME_TS_RX_OFFSET_US) +
                        (rtimer_clock_t)bp * US(DSME_BACKOFF_PERIOD_US));

    rx_len = read_frame();
    if(rx_len > 0) {
      /* Handling this frame may itself queue a REPLY, which is why the
       * transmit queue is re-read below rather than cached: a REPLY is
       * pushed to the front, so a pointer taken at the start of the slot
       * would name the wrong frame by now. */
      process_frame(rx_buf, rx_len);
      continue;
    }

    if(!may_transmit || attempted || bp < next_try_bp) {
      continue;
    }
    cmd = dsme_cmd_queue_peek(&dsme_cmd_queue);
    if(cmd == NULL) {
      continue;
    }

    /* Clear channel assessment at a backoff period boundary. */
    if(NETSTACK_RADIO.channel_clear()) {
      rtimer_clock_t now = RTIMER_NOW();
      rtimer_clock_t end =
        slot_ref + DSME_SLOT_DURATION - US(DSME_CAP_TAIL_GUARD_US);
      if(RTIMER_CLOCK_DIFF(end, now + frame_ticks(cmd->len)) > 0) {
        uint8_t is_request = (cmd->buf[DSME_HDR_LEN] == DSME_CMD_GTS_REQUEST ||
                              cmd->buf[DSME_HDR_LEN] == DSME_CMD_EXTGTS_REQUEST);
        NETSTACK_RADIO.send(cmd->buf, cmd->len);
        dsme_stats.cmd_tx++;
        /* The three-way handshake uses the REPLY as its confirmation and
         * macMaxFrameTotalWaitTime as its timeout, so no link-layer
         * acknowledgement is exchanged for command frames; REPLY and NOTIFY
         * are broadcasts and could not be acknowledged in any case. */
        if(is_request) {
          dsme_gts_request_sent(
            cmd->buf[DSME_HDR_LEN] == DSME_CMD_EXTGTS_REQUEST);
        }
        dsme_cmd_queue_pop(&dsme_cmd_queue);
      }
      /* One frame per CAP slot, as the backoff machine implies. */
      attempted = 1;
    } else {
      nb++;
      if(nb > DSME_MAC_MAX_CSMA_BACKOFFS) {
        /* The frame stays queued and is retried in the next CAP. */
        dsme_stats.cmd_cca_fail++;
        attempted = 1;
      } else {
        be = be < DSME_MAC_MAX_BE ? (uint8_t)(be + 1) : DSME_MAC_MAX_BE;
        next_try_bp = (uint16_t)(bp + 1 + (random_rand() % (1u << be)));
      }
    }
  }

  PT_END(pt);
}
/*---------------------------------------------------------------------------*/
/* CFP / extCFP slot                                                          */
/*---------------------------------------------------------------------------*/
static
PT_THREAD(pt_gts(struct pt *pt, dsme_act_entry_t *link, uint8_t is_ext))
{
  static uint8_t ch, len;
  static dsme_packet_t *pkt;
  static int rx_len;
  static dsme_hdr_t h;

  PT_BEGIN(pt);

  ch = is_ext ? dsme_tacfpext_ext_channel(link->slot_id, link->sd_index,
                                          link->channel_off, dsme_time.pan_bsn)
              : dsme_gts_channel(link->slot_id, link->sd_index,
                                 link->channel_off, dsme_time.pan_bsn);

  DSME_LOG_DBG("CH|asn=%lu|n=%u|sd=%u|slot=%u|ext=%u|dir=%u|want=%u|peer=%u",
               (unsigned long)dsme_time.asn, dsme_short_addr, link->sd_index,
               link->slot_id, is_ext, link->direction, ch, link->peer);

  if(link->direction == DSME_DIRECTION_TX) {
    pkt = dsme_queue_peek(&dsme_queue);
    if(pkt == NULL || pkt->dst != link->peer) {
      /* Nothing to send: the slot goes unused and the idle counter that
       * drives expiry will advance at the end of the multi-superframe. */
      if(is_ext) {
        dsme_stats.extgts_slot_idle++;
      }
      PT_EXIT(pt);
    }
    if(is_ext) {
      dsme_stats.extgts_slot_used++;
    }

    DSME_YIELD_AT(pt, slot_ref, DSME_TS_TX_OFFSET_US);
    set_channel(ch);
    NETSTACK_RADIO.on();
    DSME_LOG_DBG("CHTX|asn=%lu|n=%u|want=%u|radio=%u",
                 (unsigned long)dsme_time.asn, dsme_short_addr, ch,
                 radio_channel());
    len = dsme_pkt_build_data(tx_buf, data_seqno++, dsme_short_addr,
                              pkt->dst, pkt->len);
    NETSTACK_RADIO.send(tx_buf, len);
    dsme_stats.tx_attempts++;

    DSME_YIELD_AT(pt, slot_ref, DSME_TS_ACK_DEADLINE_US);
    rx_len = read_frame();
    if(rx_len > 0 && dsme_pkt_parse_hdr(rx_buf, (uint8_t)rx_len, &h) &&
       h.type == DSME_FRAME_ACK && h.dst == dsme_short_addr) {
      uint32_t delay = (uint32_t)dsme_now_us() - pkt->enqueue_us;
      dsme_stats.tx_ok++;
      dsme_stats.delay_sum_us += delay;
      dsme_stats.delay_count++;
      if(delay > dsme_stats.delay_max_us) {
        dsme_stats.delay_max_us = delay;
      }
      dsme_queue_pop(&dsme_queue);
      link->idle_counter = 0;
    } else {
      dsme_stats.tx_noack++;
      pkt->tx_count++;
      if(pkt->tx_count > DSME_MAC_MAX_FRAME_RETRIES) {
        dsme_queue_pop(&dsme_queue);
        dsme_stats.dropped_no_gts++;
        DSME_LOG("DROP|asn=%lu|n=%u|why=retries",
                 (unsigned long)dsme_time.asn, dsme_short_addr);
      }
    }
  } else {
    DSME_YIELD_AT(pt, slot_ref, DSME_TS_RX_OFFSET_US);
    set_channel(ch);
    NETSTACK_RADIO.on();

    DSME_YIELD_AT(pt, slot_ref, DSME_TS_RX_READ_OFFSET_US);
    rx_len = read_frame();
    if(rx_len > 0 && dsme_pkt_parse_hdr(rx_buf, (uint8_t)rx_len, &h) &&
       h.type == DSME_FRAME_DATA && h.dst == dsme_short_addr) {
      process_frame(rx_buf, rx_len);
      link->idle_counter = 0;

      DSME_YIELD_AT(pt, slot_ref, DSME_TS_ACK_TX_OFFSET_US);
      len = dsme_pkt_build_ack(tx_buf, h.seqno, dsme_short_addr, h.src);
      NETSTACK_RADIO.send(tx_buf, len);
    }
  }

  PT_END(pt);
}
/*---------------------------------------------------------------------------*/
/* Main slot loop                                                             */
/*---------------------------------------------------------------------------*/
static
PT_THREAD(slot_operation_thread(struct rtimer *t, void *ptr))
{
  static uint8_t slot, sd, role;
  static int8_t cfp_slot;
  static dsme_act_entry_t *link;

  PT_BEGIN(&slot_pt);

  while(running) {
    /* ---- Passive scan, until a beacon pins us to the slot grid ---- */
    if(!dsme_joined) {
      set_channel(DSME_CONTROL_CHANNEL);
      NETSTACK_RADIO.on();
      process_frame(rx_buf, read_frame());
      if(just_joined) {
        just_joined = 0;
        DSME_YIELD_AT(&slot_pt, slot_ref, 0);
        continue;
      }
      slot_ref = RTIMER_NOW();
      DSME_YIELD_AT(&slot_pt, slot_ref, 1000);
      continue;
    }

    dsme_slot_start = slot_ref;
    slot = dsme_slot_of(dsme_time.asn);
    sd = dsme_sd_index_of(dsme_time.asn);

    /* Superframe and multi-superframe boundaries. Pure computation: it never
     * blocks, so running it inside the slot machine cannot disturb timing. */
    if(slot == 0) {
      if(sd == 0) {
        dsme_gts_multisuperframe_tick();
      }
      dsme_gts_superframe_tick();
    }

    role = dsme_slot_role(sd, slot);

    if(role == DSME_SLOT_ROLE_BEACON) {
      PT_SPAWN(&slot_pt, &sub_pt, pt_beacon(&sub_pt));
    } else if(role == DSME_SLOT_ROLE_CAP) {
      PT_SPAWN(&slot_pt, &sub_pt, pt_cap(&sub_pt, 1));
    } else if(role == DSME_SLOT_ROLE_LOP) {
      PT_SPAWN(&slot_pt, &sub_pt, pt_cap(&sub_pt, 0));
    } else if(role == DSME_SLOT_ROLE_CFP) {
      cfp_slot = dsme_phys_to_cfp_slot(sd, slot);
      link = cfp_slot < 0 ? NULL : dsme_act_lookup(sd, (uint8_t)cfp_slot, 0);
      if(link != NULL) {
        PT_SPAWN(&slot_pt, &sub_pt, pt_gts(&sub_pt, link, 0));
      }
    } else if(role == DSME_SLOT_ROLE_EXTCFP) {
      link = dsme_act_lookup(sd, (uint8_t)(slot - DSME_CAP_FIRST_SLOT), 1);
      if(link != NULL) {
        PT_SPAWN(&slot_pt, &sub_pt, pt_gts(&sub_pt, link, 1));
      }
    }

    NETSTACK_RADIO.off();

    dsme_time.asn++;
    slot_ref += DSME_SLOT_DURATION;
    DSME_YIELD_AT(&slot_pt, slot_ref, 0);
  }

  PT_END(&slot_pt);
}
/*---------------------------------------------------------------------------*/
static char
slot_operation(struct rtimer *t, void *ptr)
{
  return slot_operation_thread(t, ptr);
}
/*---------------------------------------------------------------------------*/
static void
slot_timer_callback(struct rtimer *t, void *ptr)
{
  slot_operation(t, ptr);
}
/*---------------------------------------------------------------------------*/
void
dsme_slot_operation_start(void)
{
  radio_value_t rx_mode = 0;

  running = 1;
  just_joined = 0;
  PT_INIT(&slot_pt);
  PT_INIT(&sub_pt);

  /* Poll mode: the MAC reads the radio itself at the instants the slot
   * template dictates, instead of letting the platform's radio process drain
   * the buffer into packetbuf at an arbitrary moment. */
  NETSTACK_RADIO.get_value(RADIO_PARAM_RX_MODE, &rx_mode);
  rx_mode &= ~(RADIO_RX_MODE_ADDRESS_FILTER | RADIO_RX_MODE_AUTOACK);
  rx_mode |= RADIO_RX_MODE_POLL_MODE;
  NETSTACK_RADIO.set_value(RADIO_PARAM_RX_MODE, rx_mode);
  /* All carrier sensing is done explicitly by the CAP backoff machine. */
  NETSTACK_RADIO.set_value(RADIO_PARAM_TX_MODE, 0);

  if(dsme_is_pan_coordinator) {
    dsme_joined = 1;
    dsme_time.asn = 0;
    dsme_time.pan_bsn = 0;
    slot_ref = RTIMER_NOW() + US(10000);
  } else {
    dsme_joined = 0;
    /* Must be strictly in the future: an rtimer set for the current instant
     * is dropped, and nothing else would ever enter the protothread. */
    slot_ref = RTIMER_NOW() + US(1000);
  }
  schedule_at(slot_ref, 0);
}
/*---------------------------------------------------------------------------*/
void
dsme_slot_operation_stop(void)
{
  running = 0;
}
/*---------------------------------------------------------------------------*/
