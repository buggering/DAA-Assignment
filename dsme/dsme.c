/*
 * dsme.c -- IEEE 802.15.4 DSME MAC for Contiki-NG: shared state, application
 *           interface and MAC driver registration.
 */

#include "contiki.h"
#include "net/netstack.h"
#include "sys/node-id.h"
#if CONTIKI_TARGET_COOJA
#include "dev/moteid.h"
#endif

#include "dsme.h"
#include "dsme-slot-operation.h"
#include "dsme-queue.h"
#include "dsme-act.h"
#include "dsme-sab.h"
#include "dsme-gts.h"
#include "dsme-tacfpext.h"
#include "dsme-predictor.h"
#include "dsme-packet.h"
#include "dsme-log.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
dsme_time_t dsme_time;
dsme_sab_t dsme_sab;
dsme_tasab_t dsme_tasab;
dsme_ccb_t dsme_ccb;
dsme_act_entry_t dsme_act[DSME_MAX_ACT_ENTRIES];
dsme_queue_t dsme_queue;
dsme_cmd_queue_t dsme_cmd_queue;
dsme_txn_t dsme_txn[2];
dsme_predictor_t dsme_pred;
dsme_stats_t dsme_stats;

uint16_t dsme_short_addr;
uint8_t dsme_is_pan_coordinator;
uint8_t dsme_joined;
uint8_t dsme_channel_offset;
uint16_t dsme_tx_peer;
uint8_t dsme_alloc_fail_count;
uint8_t dsme_demand_slots;
rtimer_clock_t dsme_slot_start;

static uint16_t app_seqno;

PROCESS(dsme_process, "DSME MAC");

/*---------------------------------------------------------------------------*/
uint16_t
dsme_node_addr(void)
{
#if CONTIKI_TARGET_COOJA
  return (uint16_t)simMoteID;
#else
  return node_id;
#endif
}
/*---------------------------------------------------------------------------*/
uint64_t
dsme_now_us(void)
{
  /* The Cooja rtimer is a 1 MHz microsecond counter that does not wrap within
   * any run length used here, so it serves directly as the absolute clock. */
  return (uint64_t)RTIMER_NOW();
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_slot_role(uint8_t sd_index, uint8_t slot)
{
  if(slot == DSME_BEACON_SLOT) {
    return DSME_SLOT_ROLE_BEACON;
  }

#if DSME_MODE == DSME_MODE_CAP_REDUCTION
  /* Reference B, Section 5.1.1: with CAP reduction only the first superframe
   * of the multi-superframe keeps its CAP; the others carry 15 GTSs. */
  if(sd_index != 0) {
    return DSME_SLOT_ROLE_CFP;
  }
  return slot <= DSME_CAP_LAST_SLOT ? DSME_SLOT_ROLE_CAP : DSME_SLOT_ROLE_CFP;
#else
  if(slot >= DSME_CFP_FIRST_SLOT) {
    return DSME_SLOT_ROLE_CFP;
  }
#if DSME_MODE == DSME_MODE_TACFPEXT || DSME_MODE == DSME_MODE_PREDICTIVE
  {
    uint8_t state = dsme_ccb_get(&dsme_ccb, sd_index);
    if(state == DSME_CCB_EXTCFP) {
      return DSME_SLOT_ROLE_EXTCFP;
    }
    if(state == DSME_CCB_LOP) {
      return DSME_SLOT_ROLE_LOP;
    }
  }
#endif
  return DSME_SLOT_ROLE_CAP;
#endif
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_allocated_tx_slots(void)
{
  return dsme_act_count(DSME_DIRECTION_TX, 0xFF);
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_allocated_ext_tx_slots(void)
{
  return dsme_act_count(DSME_DIRECTION_TX, 1);
}
/*---------------------------------------------------------------------------*/
int
dsme_app_send(uint16_t dst, uint8_t len)
{
  dsme_packet_t p;

  dsme_stats.app_generated++;

  p.dst = dst;
  p.seqno = app_seqno++;
  p.enqueue_us = (uint32_t)dsme_now_us();
  p.deadline_ms = dsme_ms_of(dsme_time.asn) + DSME_PACKET_DEADLINE_MS;
  p.len = len;
  p.tx_count = 0;

  /* The estimator and the last-value demand counter both observe the arrival,
   * not the successful enqueue: a packet dropped for want of queue space is
   * still evidence of offered load. */
  dsme_predictor_on_arrival(&dsme_pred, 1);
  dsme_gts_note_arrival();

  if(!dsme_queue_push(&dsme_queue, &p)) {
    dsme_stats.dropped_queue_full++;
    DSME_LOG("DROP|asn=%lu|n=%u|why=queue",
             (unsigned long)dsme_time.asn, dsme_short_addr);
    return 0;
  }
  dsme_stats.enqueued++;
  return 1;
}
/*---------------------------------------------------------------------------*/
void
dsme_set_demand(uint8_t packets_per_multisuperframe)
{
#if DSME_DEMAND_SRC == DSME_DEMAND_ORACLE
  dsme_demand_slots = packets_per_multisuperframe > DSME_SAB_TOTAL_SLOTS
                        ? DSME_SAB_TOTAL_SLOTS
                        : packets_per_multisuperframe;
#else
  /* The demand is learned, not declared; the application's figure is ignored
   * so that the estimator is what carries the decision. */
  (void)packets_per_multisuperframe;
#endif
}
/*---------------------------------------------------------------------------*/
void
dsme_dump_stats(void)
{
  uint32_t avg = dsme_stats.delay_count > 0
    ? (uint32_t)(dsme_stats.delay_sum_us / dsme_stats.delay_count) : 0;

  DSME_LOG("STATS|n=%u|gen=%lu|enq=%lu|dropq=%lu|dropg=%lu|txok=%lu|txnoack=%lu"
           "|rx=%lu|greq=%lu|gok=%lu|gfail=%lu|ereq=%lu|eok=%lu|efail=%lu"
           "|trigr=%lu|trigp=%lu|conv=%lu|eused=%lu|eidle=%lu|cmdtx=%lu"
           "|ccafail=%lu|cmdfull=%lu|dropdl=%lu|delayavg=%lu|delaymax=%lu"
           "|slots=%u|ext=%u",
           dsme_short_addr,
           (unsigned long)dsme_stats.app_generated,
           (unsigned long)dsme_stats.enqueued,
           (unsigned long)dsme_stats.dropped_queue_full,
           (unsigned long)dsme_stats.dropped_no_gts,
           (unsigned long)dsme_stats.tx_ok,
           (unsigned long)dsme_stats.tx_noack,
           (unsigned long)dsme_stats.rx_data,
           (unsigned long)dsme_stats.gts_requests_sent,
           (unsigned long)dsme_stats.gts_alloc_ok,
           (unsigned long)dsme_stats.gts_alloc_fail,
           (unsigned long)dsme_stats.extgts_requests_sent,
           (unsigned long)dsme_stats.extgts_alloc_ok,
           (unsigned long)dsme_stats.extgts_alloc_fail,
           (unsigned long)dsme_stats.trig_reactive,
           (unsigned long)dsme_stats.trig_predictive,
           (unsigned long)dsme_stats.cap_converted,
           (unsigned long)dsme_stats.extgts_slot_used,
           (unsigned long)dsme_stats.extgts_slot_idle,
           (unsigned long)dsme_stats.cmd_tx,
           (unsigned long)dsme_stats.cmd_cca_fail,
           (unsigned long)dsme_stats.cmd_queue_full,
           (unsigned long)dsme_stats.dropped_deadline,
           (unsigned long)avg,
           (unsigned long)dsme_stats.delay_max_us,
           dsme_allocated_tx_slots(),
           dsme_allocated_ext_tx_slots());
}
/*---------------------------------------------------------------------------*/
/* MAC driver                                                                 */
/*---------------------------------------------------------------------------*/
static void
dsme_init(void)
{
  memset(&dsme_time, 0, sizeof(dsme_time));
  memset(&dsme_stats, 0, sizeof(dsme_stats));
  memset(dsme_txn, 0, sizeof(dsme_txn));

  dsme_joined = 0;
  dsme_alloc_fail_count = 0;
  dsme_demand_slots = 0;
  app_seqno = 0;

  dsme_sab_init(&dsme_sab);
  dsme_tasab_init(&dsme_tasab);
  dsme_ccb_init(&dsme_ccb);
  dsme_act_init();
  dsme_queue_init(&dsme_queue);
  dsme_cmd_queue_init(&dsme_cmd_queue);
  dsme_gts_init();
  dsme_predictor_init(&dsme_pred);
}
/*---------------------------------------------------------------------------*/
void
dsme_start(uint8_t is_pan_coordinator, uint16_t tx_peer)
{
  dsme_short_addr = dsme_node_addr();
  /* Reference B, Section 5.1.3: the channel offset is chosen at association
   * and neighbours must not share one. With the whole deployment inside a few
   * hops, spreading offsets over the hopping sequence by address is the
   * simplest assignment that satisfies that. */
  dsme_channel_offset = (uint8_t)(dsme_short_addr % DSME_NUM_CHANNELS);
  dsme_is_pan_coordinator = is_pan_coordinator;
  dsme_tx_peer = tx_peer;

  DSME_LOG("INIT|n=%u|mode=%d|demand=%d|so=%d|mo=%d|bo=%d|slotus=%lu"
           "|sfperms=%d|msperbi=%d|cfp=%d|chanoff=%u|failthr=%d",
           dsme_short_addr, DSME_MODE, DSME_DEMAND_SRC,
           DSME_SO, DSME_MO, DSME_BO,
           (unsigned long)DSME_SLOT_DURATION_US,
           DSME_SF_PER_MS, DSME_MS_PER_BI, DSME_CFP_SLOTS,
           dsme_channel_offset, DSME_FAIL_THRESHOLD);

  process_start(&dsme_process, NULL);
}
/*---------------------------------------------------------------------------*/
static void
dsme_send(mac_callback_t sent, void *ptr)
{
  /* The study drives the MAC through dsme_app_send(); the netstack path is
   * present so that the driver is a well-formed mac_driver. */
  mac_call_sent_callback(sent, ptr, MAC_TX_ERR, 0);
}
/*---------------------------------------------------------------------------*/
static void
dsme_input(void)
{
  /* Unused: the slot machine reads the radio directly in poll mode. */
}
/*---------------------------------------------------------------------------*/
static int
dsme_on(void)
{
  return 1;
}
/*---------------------------------------------------------------------------*/
static int
dsme_off(void)
{
  return 1;
}
/*---------------------------------------------------------------------------*/
static int
dsme_max_payload(void)
{
  return DSME_DATA_FRAME_LEN - DSME_HDR_LEN;
}
/*---------------------------------------------------------------------------*/
const struct mac_driver dsme_driver = {
  "DSME",
  dsme_init,
  dsme_send,
  dsme_input,
  dsme_on,
  dsme_off,
  dsme_max_payload,
};
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(dsme_process, ev, data)
{
  PROCESS_BEGIN();
  dsme_slot_operation_start();
  while(1) {
    PROCESS_YIELD();
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
