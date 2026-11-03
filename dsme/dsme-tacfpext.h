/*
 * dsme-tacfpext.h -- Traffic-adaptive CFP extension (Reference C).
 *
 * Two procedures, both executed in a fully distributed manner:
 *
 *   1. Changeable CAP selection (Reference C, Algorithm 1): decide which CAP
 *      of the multi-superframe should become an extCFP, preferring superframes
 *      already converted, then a neighbour's listen-only periods, then
 *      untouched CAPs -- so that the number of CAPs the neighbourhood loses is
 *      as small as possible. The CAP of superframe 0 is never eligible, which
 *      is what guarantees every node keeps one common CAP for control traffic.
 *
 *   2. CCB update (Reference C, Algorithm 2): keep the per-superframe
 *      CAP / LOP / extCFP view consistent as extGTSs are allocated and
 *      deallocated around the node.
 *
 * The extCFP hopping sequence excludes the control channel, so channel
 * offsets are taken modulo (DSME_NUM_CHANNELS - 1) here, per Reference C
 * Eq. (3).
 */

#ifndef DSME_TACFPEXT_H_
#define DSME_TACFPEXT_H_

#include "dsme-types.h"

#define DSME_EXT_HOPPING_LEN   (DSME_NUM_CHANNELS - 1)

/**
 * Reference C, Algorithm 1.
 * \param num_slots extGTSs that must be accommodated
 * \param out_sid   receives the selected superframe IDs, in priority order
 * \return number of superframe IDs written, or 0 if even all eligible CAPs
 *         together cannot supply \p num_slots slots.
 */
uint8_t dsme_tacfpext_select_changeable_caps(uint8_t num_slots, uint8_t *out_sid);

/**
 * Reference C, Algorithm 2.
 * \param sid        the changeable CAP the command referred to
 * \param mgmt       DSME_MGMT_ALLOCATION or DSME_MGMT_DEALLOCATION
 * \param involves_me non-zero if this node is one of the two endpoints
 */
void dsme_tacfpext_ccb_update(uint8_t sid, uint8_t mgmt, uint8_t involves_me);

/** Slots of superframe \p sid's extCFP that are free on our channel offset. */
uint8_t dsme_tacfpext_free_ext_slots(uint8_t sid);

/** Total extCFP slots free across every superframe already converted. */
uint8_t dsme_tacfpext_free_ext_slots_total(void);

/**
 * Physical channel for an extGTS, Reference C Eq. (3): the hopping sequence
 * with the control channel removed.
 */
uint8_t dsme_tacfpext_ext_channel(uint8_t ext_slot, uint8_t sd_index,
                                  uint8_t channel_off, uint8_t pan_bsn);

/** Physical channel for a GTS in the CFP, Reference C Eq. (1). */
uint8_t dsme_gts_channel(uint8_t cfp_slot, uint8_t sd_index,
                         uint8_t channel_off, uint8_t pan_bsn);

#endif /* DSME_TACFPEXT_H_ */
