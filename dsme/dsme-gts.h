/*
 * dsme-gts.h -- DSME-GTS and extGTS allocation, deallocation and expiry,
 *               plus the trigger that decides between the two.
 */

#ifndef DSME_GTS_H_
#define DSME_GTS_H_

#include "dsme-types.h"
#include "dsme-packet.h"

void dsme_gts_init(void);

/** Dispatch a received GTS / extGTS command frame. */
void dsme_gts_rx_cmd(const dsme_hdr_t *h, const dsme_cmd_info_t *info);

/** One application packet was queued; feeds the LASTVAL demand estimator. */
void dsme_gts_note_arrival(void);

/** The slot machine put the pending REQUEST on air. */
void dsme_gts_request_sent(uint8_t is_ext);

/**
 * Once per superframe, at the beacon slot: run the trigger and start a
 * handshake if one is due.
 */
void dsme_gts_superframe_tick(void);

/**
 * Once per multi-superframe: age the ACT, deallocate what has expired, and
 * refresh the node's GTS demand.
 */
void dsme_gts_multisuperframe_tick(void);

/** Reason a CAP-to-extCFP conversion was started, for the logs. */
#define DSME_TRIG_REACTIVE   0
#define DSME_TRIG_PREDICTIVE 1

#endif /* DSME_GTS_H_ */
