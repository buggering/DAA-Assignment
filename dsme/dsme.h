/*
 * dsme.h -- IEEE 802.15.4 DSME MAC for Contiki-NG: public interface and
 *           shared MAC state.
 */

#ifndef DSME_H_
#define DSME_H_

#include "contiki.h"
#include "net/mac/mac.h"
#include "dsme-conf.h"
#include "dsme-const.h"
#include "dsme-types.h"

/*---------------------------------------------------------------------------*/
/* Shared MAC state. Written by the slot-operation rtimer task and read by    */
/* the DSME process, so anything the process mutates while the slot machine   */
/* may run is kept to single-word writes.                                     */
/*---------------------------------------------------------------------------*/
extern dsme_time_t dsme_time;
extern dsme_sab_t dsme_sab;
extern dsme_tasab_t dsme_tasab;
extern dsme_ccb_t dsme_ccb;
extern dsme_act_entry_t dsme_act[DSME_MAX_ACT_ENTRIES];
extern dsme_queue_t dsme_queue;
extern dsme_cmd_queue_t dsme_cmd_queue;
/* One in-flight handshake each for ordinary GTSs and for extGTSs. They
 * reserve different resources -- the CFP and a converted CAP -- so making a
 * node wait for one before it may start the other is an artificial coupling.
 * Indexed by is_ext. */
extern dsme_txn_t dsme_txn[2];
extern dsme_predictor_t dsme_pred;
extern dsme_stats_t dsme_stats;

/** Short address of this node; equal to the Cooja mote id. */
extern uint16_t dsme_short_addr;
/** This node started the PAN and owns superframe 0 of every beacon interval. */
extern uint8_t dsme_is_pan_coordinator;
/** Set once the node is synchronised to the multi-superframe structure. */
extern uint8_t dsme_joined;
/** macChannelOffset of this node; the sender uses the receiver's offset. */
extern uint8_t dsme_channel_offset;
/** Peer this node transmits data to, 0 if this node is a pure receiver. */
extern uint16_t dsme_tx_peer;
/** Consecutive GTS allocation failures -- the TaCFPext reactive trigger. */
extern uint8_t dsme_alloc_fail_count;
/** Number of GTSs the application currently wants in the CFP. */
extern uint8_t dsme_demand_slots;
/** rtimer time at which the current slot started. */
extern rtimer_clock_t dsme_slot_start;

/*---------------------------------------------------------------------------*/
/* MAC driver                                                                 */
/*---------------------------------------------------------------------------*/
extern const struct mac_driver dsme_driver;

/*---------------------------------------------------------------------------*/
/* Application interface                                                      */
/*---------------------------------------------------------------------------*/

/**
 * Short address of this node.
 *
 * Contiki-NG's node_id is unusable here: contiki-main.c calls netstack_init()
 * -- and therefore the MAC's init() -- one line before node_id_init(), and on
 * the Cooja platform without IPv6 the mote id is written into the first two
 * octets of an eight-octet link address while node_id_init() reads the last
 * two, so node_id stays 0 regardless. The simulator's mote id is read
 * directly instead.
 */
uint16_t dsme_node_addr(void);

/**
 * Start the DSME MAC.
 * \param is_pan_coordinator non-zero on the node that starts the PAN.
 * \param tx_peer short address this node sends data to, or 0 for none.
 */
void dsme_start(uint8_t is_pan_coordinator, uint16_t tx_peer);

/**
 * Offer one application data frame of \p len octets to the MAC.
 * \return 1 if the frame was queued, 0 if it was dropped (queue full).
 */
int dsme_app_send(uint16_t dst, uint8_t len);

/**
 * Declare how many packets per multi-superframe the application intends to
 * send. The MAC turns this into a GTS demand and, under P-TaCFPext, it is
 * also the signal the traffic estimator learns from.
 */
void dsme_set_demand(uint8_t packets_per_multisuperframe);

/** Current number of TX GTSs (and extGTSs) allocated to this node. */
uint8_t dsme_allocated_tx_slots(void);
/** Of those, how many sit in an extCFP. */
uint8_t dsme_allocated_ext_tx_slots(void);

/** Dump the per-node counters to the log. */
void dsme_dump_stats(void);

/*---------------------------------------------------------------------------*/
/* Internal helpers shared between DSME source files                          */
/*---------------------------------------------------------------------------*/

/** Microseconds since the PAN started, derived from the ASN and slot phase. */
uint64_t dsme_now_us(void);

/**
 * Role of slot \p slot in superframe \p sd_index for this node, taking CAP
 * reduction and any CAP-to-extCFP conversion into account.
 */
enum {
  DSME_SLOT_ROLE_BEACON,
  DSME_SLOT_ROLE_CAP,
  DSME_SLOT_ROLE_CFP,
  DSME_SLOT_ROLE_EXTCFP,
  DSME_SLOT_ROLE_LOP,       /* a neighbour's extCFP: listen only, stay quiet */
};
uint8_t dsme_slot_role(uint8_t sd_index, uint8_t slot);

#endif /* DSME_H_ */
