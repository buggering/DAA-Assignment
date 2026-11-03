/*
 * dsme-act.h -- DSME Allocation Counter Table (ACT) and its traffic-adaptive
 *               counterpart (taACT).
 *
 * One table backs both: entries with DSME_ACT_IS_EXT describe extGTSs inside
 * an extCFP and are the taACT, the rest are ordinary GTSs inside the CFP.
 * Reference C keeps taACT field-compatible with the legacy ACT for exactly
 * this reason.
 */

#ifndef DSME_ACT_H_
#define DSME_ACT_H_

#include "dsme-types.h"

void dsme_act_init(void);

/** \return the new entry, or NULL when the table is full. */
dsme_act_entry_t *dsme_act_add(uint16_t peer, uint8_t sd, uint8_t slot,
                               uint8_t channel_off, uint8_t direction,
                               uint8_t is_ext);

/** Entry occupying (sd, slot) of the CFP (is_ext = 0) or extCFP (is_ext = 1). */
dsme_act_entry_t *dsme_act_lookup(uint8_t sd, uint8_t slot, uint8_t is_ext);

void dsme_act_remove(dsme_act_entry_t *e);
/** Drop every entry towards \p peer in \p direction. \return number removed. */
uint8_t dsme_act_remove_peer(uint16_t peer, uint8_t direction, uint8_t is_ext);

/** Count entries matching a direction; \p is_ext of 0xFF matches both kinds. */
uint8_t dsme_act_count(uint8_t direction, uint8_t is_ext);

/** Superframe IDs in which this node holds at least one extGTS (taACT.sid). */
uint8_t dsme_act_ext_superframes(uint8_t *out_sd);

/**
 * Advance every idle counter by one multi-superframe and report the entries
 * that have expired. Reference B: a GTS expires after
 * macDSMEGTSExpirationTime idle multi-superframes; Reference C halves that
 * for extGTSs so that extCFP space is reclaimed before CFP space.
 * Expired entries are written to \p expired and removed from the table.
 * \return number of expired entries.
 */
uint8_t dsme_act_age_and_expire(dsme_act_entry_t *expired, uint8_t max);

/** Clear the idle counter of the entry covering (sd, slot). */
void dsme_act_mark_used(uint8_t sd, uint8_t slot, uint8_t is_ext);

#endif /* DSME_ACT_H_ */
