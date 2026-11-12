/*
 * node.c -- Alternating low / burst traffic generator for the DSME
 *           CFP-extension experiments.
 *
 * Topology follows Reference C: node 1 is the PAN coordinator, and the
 * remaining nodes form sender-receiver pairs with a one-hop neighbour
 * (2->3, 4->5, ...).
 *
 * Traffic follows the dynamic scenario of Reference C, Section IV-A: the
 * senders are split into two equal groups with a period of eight beacon
 * intervals. One group generates high traffic in the third and fourth beacon
 * intervals of the period, the other in the seventh and eighth; outside their
 * burst both groups generate one packet per multi-superframe.
 *
 * Packets are released at a constant rate across the multi-superframe rather
 * than all at once, so that the per-superframe arrival process the predictor
 * samples is the real one and not an artefact of the generator.
 */

#include "contiki.h"
#include "sys/node-id.h"
#include "sys/etimer.h"
#include "dsme.h"
#include "dsme-log.h"

#include <stdio.h>

/* Classroom demo only: drive the mote LEDs from the node's DSME state so the
 * Cooja network view shows the burst, the refusals and the extended CFP.
 * Off by default; the experiment builds never set it. LEDs are write-only
 * from the firmware's side, so turning this on changes nothing the protocol
 * sees. */
#ifndef APP_DEMO_LEDS
#define APP_DEMO_LEDS               0
#endif
#if APP_DEMO_LEDS
#include "dev/leds.h"
#endif

/*---------------------------------------------------------------------------*/
/* Offered load, in packets per multi-superframe. Reference C uses 1 and 21.  */
#ifndef APP_LOW_RATE
#define APP_LOW_RATE                1
#endif
#ifndef APP_BURST_RATE
#define APP_BURST_RATE              21
#endif
/* Length of the traffic period, in beacon intervals. */
#ifndef APP_PERIOD_BI
#define APP_PERIOD_BI               8
#endif
/* Length of each burst, in beacon intervals. Reference C bursts for two. */
#ifndef APP_BURST_BI
#define APP_BURST_BI                2
#endif
/* Phase at which group 0 starts bursting; group 1 starts half a period later,
 * so the two halves of the network never burst together. */
#define APP_BURST_PHASE_0           2
#define APP_BURST_PHASE_1 \
  ((APP_BURST_PHASE_0 + APP_PERIOD_BI / 2) % APP_PERIOD_BI)
#ifndef APP_RUN_SECONDS
#define APP_RUN_SECONDS             60
#endif
/* Generator resolution. Fine enough that the instant a burst starts is known
 * to well inside one superframe. */
#define APP_TICK_MS                 5
#define APP_TICK_US                 (APP_TICK_MS * 1000UL)

/*---------------------------------------------------------------------------*/
static uint8_t is_sender;
static uint16_t peer;
static uint8_t group;
static uint16_t me;

PROCESS(app_process, "DSME traffic generator");
PROCESS(stats_process, "DSME run control");
AUTOSTART_PROCESSES(&app_process, &stats_process);

/*---------------------------------------------------------------------------*/
static uint8_t
current_rate(void)
{
  uint32_t bi = dsme_sf_of(dsme_time.asn) / DSME_SF_PER_BI;
  uint8_t phase = (uint8_t)(bi % APP_PERIOD_BI);

  uint8_t start = group == 0 ? APP_BURST_PHASE_0 : APP_BURST_PHASE_1;
  uint8_t off = (uint8_t)((phase + APP_PERIOD_BI - start) % APP_PERIOD_BI);

  return off < APP_BURST_BI ? APP_BURST_RATE : APP_LOW_RATE;
}
/*---------------------------------------------------------------------------*/
#if APP_DEMO_LEDS
/*   red     this node is in its traffic burst
 *   yellow  its GTS requests are being refused (failure counter > 0)
 *   green   it holds an extGTS: a CAP has been converted to extended CFP */
static void
demo_leds(uint8_t rate)
{
  leds_set((rate == APP_BURST_RATE ? LEDS_RED : 0)
           | (dsme_alloc_fail_count > 0 ? LEDS_YELLOW : 0)
           | (dsme_allocated_ext_tx_slots() > 0 ? LEDS_GREEN : 0));
}
#endif
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(app_process, ev, data)
{
  static struct etimer et;
  static uint32_t credit;
  static uint8_t rate, last_rate;

  PROCESS_BEGIN();

  me = dsme_node_addr();
  /* Node 1 starts the PAN. Even addresses send to the next odd address. */
  is_sender = (me > 1) && ((me % 2) == 0);
  peer = is_sender ? (uint16_t)(me + 1) : 0;
  /* Alternate consecutive pairs between the two traffic groups so the halves
   * are equal in size and interleaved in space. */
  group = (uint8_t)((me / 2) % 2);

  dsme_start(me == 1, peer);

  DSME_LOG("ROLE|n=%u|sender=%u|peer=%u|group=%u|pan=%u",
           me, is_sender, peer, group, me == 1);
#if APP_DEMO_LEDS
  /* Colour the mote by role in the Cooja network view: coordinator blue,
   * senders lilac, receivers grey -- none of them a LED colour. */
  printf("#A color=%s\n",
         me == 1 ? "BLUE" : (is_sender ? "#D9B3FF" : "LIGHTGRAY"));
#endif

  /* Stagger the generators so that every sender does not enqueue on the same
   * microsecond. */
  etimer_set(&et, (CLOCK_SECOND / 4) + (me % 32));
  PROCESS_WAIT_UNTIL(etimer_expired(&et));

  if(!is_sender) {
    PROCESS_EXIT();
  }

  credit = 0;
  last_rate = 0xFF;

  while(1) {
    etimer_set(&et, (CLOCK_SECOND * APP_TICK_MS) / 1000);
    PROCESS_WAIT_UNTIL(etimer_expired(&et));

    if(!dsme_joined) {
      continue;
    }

    rate = current_rate();
    if(rate != last_rate) {
      /* The onset marker is what the offline analysis measures the trigger
       * and allocation latencies from. */
      DSME_LOG("%s|asn=%lu|n=%u|rate=%u|prev=%u|slots=%u",
               last_rate == 0xFF ? "START" : (rate > last_rate ? "ONSET" : "OFFSET"),
               (unsigned long)dsme_time.asn, me,
               rate, last_rate == 0xFF ? 0 : last_rate,
               dsme_allocated_tx_slots());
      last_rate = rate;
      credit = 0;
      /* Honoured only when DSME_DEMAND_SRC is DSME_DEMAND_ORACLE. */
      dsme_set_demand(rate);
    }
#if APP_DEMO_LEDS
    demo_leds(rate);
#endif

    credit += (uint32_t)rate * APP_TICK_US;
    while(credit >= DSME_MS_DURATION_US) {
      credit -= DSME_MS_DURATION_US;
      dsme_app_send(peer, DSME_DATA_FRAME_LEN);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(stats_process, ev, data)
{
  static struct etimer et;

  PROCESS_BEGIN();

  etimer_set(&et, CLOCK_SECOND * APP_RUN_SECONDS);
  PROCESS_WAIT_UNTIL(etimer_expired(&et));
  dsme_dump_stats();
  DSME_LOG("DONE|n=%u", dsme_node_addr());

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
