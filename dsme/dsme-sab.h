/*
 * dsme-sab.h -- Slot Allocation Bitmap (SAB), traffic-adaptive SAB (taSAB)
 *               and Changeable CAP Bitmap (CCB), plus the slot geometry the
 *               bitmaps index.
 */

#ifndef DSME_SAB_H_
#define DSME_SAB_H_

#include "dsme-types.h"

/*---------------------------------------------------------------------------*/
/* Slot geometry                                                              */
/*                                                                            */
/* Without CAP reduction every superframe has 7 GTSs in physical slots 9..15. */
/* With CAP reduction every superframe but SD index 0 drops its CAP and has   */
/* 15 GTSs in physical slots 1..15.                                           */
/*---------------------------------------------------------------------------*/
static inline uint8_t
dsme_cfp_slot_count(uint8_t sd_index)
{
#if DSME_MODE == DSME_MODE_CAP_REDUCTION
  return sd_index == 0 ? DSME_CFP_SLOTS : DSME_CFP_SLOTS_REDUCED;
#else
  (void)sd_index;
  return DSME_CFP_SLOTS;
#endif
}

static inline uint8_t
dsme_cfp_slot_to_phys(uint8_t sd_index, uint8_t cfp_slot)
{
#if DSME_MODE == DSME_MODE_CAP_REDUCTION
  return sd_index == 0 ? (uint8_t)(DSME_CFP_FIRST_SLOT + cfp_slot)
                       : (uint8_t)(DSME_CAP_FIRST_SLOT + cfp_slot);
#else
  (void)sd_index;
  return (uint8_t)(DSME_CFP_FIRST_SLOT + cfp_slot);
#endif
}

/** \return the CFP slot index of physical slot \p phys, or -1 if not a GTS. */
static inline int8_t
dsme_phys_to_cfp_slot(uint8_t sd_index, uint8_t phys)
{
#if DSME_MODE == DSME_MODE_CAP_REDUCTION
  if(sd_index == 0) {
    return phys >= DSME_CFP_FIRST_SLOT ? (int8_t)(phys - DSME_CFP_FIRST_SLOT) : -1;
  }
  return phys >= DSME_CAP_FIRST_SLOT ? (int8_t)(phys - DSME_CAP_FIRST_SLOT) : -1;
#else
  (void)sd_index;
  return phys >= DSME_CFP_FIRST_SLOT ? (int8_t)(phys - DSME_CFP_FIRST_SLOT) : -1;
#endif
}

/** Physical slot of extGTS \p ext_slot: the extCFP reuses the CAP slots. */
static inline uint8_t
dsme_extcfp_slot_to_phys(uint8_t ext_slot)
{
  return (uint8_t)(DSME_CAP_FIRST_SLOT + ext_slot);
}

/*---------------------------------------------------------------------------*/
/* SAB                                                                        */
/*---------------------------------------------------------------------------*/
void dsme_sab_init(dsme_sab_t *sab);
void dsme_sab_set(dsme_sab_t *sab, uint8_t sd, uint8_t slot);
void dsme_sab_clear(dsme_sab_t *sab, uint8_t sd, uint8_t slot);
int dsme_sab_test(const dsme_sab_t *sab, uint8_t sd, uint8_t slot);
/** Number of GTS positions in the multi-superframe still free in \p sab. */
uint8_t dsme_sab_free_count(const dsme_sab_t *sab);

/**
 * Find up to \p want GTS positions free in both \p mine and \p theirs,
 * starting the scan at (\p pref_sd, \p pref_slot) and wrapping.
 * \return the number found; (sd,slot) pairs are written to the out arrays.
 */
uint8_t dsme_sab_find_common_free(const dsme_sab_t *mine,
                                  const dsme_sab_t *theirs,
                                  uint8_t pref_sd, uint8_t pref_slot,
                                  uint8_t want,
                                  uint8_t *out_sd, uint8_t *out_slot);

/*---------------------------------------------------------------------------*/
/* taSAB                                                                      */
/*---------------------------------------------------------------------------*/
void dsme_tasab_init(dsme_tasab_t *t);
void dsme_tasab_set(dsme_tasab_t *t, uint8_t sd, uint8_t slot, uint8_t ch);
void dsme_tasab_clear(dsme_tasab_t *t, uint8_t sd, uint8_t slot, uint8_t ch);
int dsme_tasab_test(const dsme_tasab_t *t, uint8_t sd, uint8_t slot, uint8_t ch);
/** Any node at all using (sd, slot), on any channel offset. */
int dsme_tasab_slot_busy(const dsme_tasab_t *t, uint8_t sd, uint8_t slot);

/**
 * Find up to \p want extGTS positions in superframe \p sd that are free in
 * both taSABs on channel offset \p ch.
 */
uint8_t dsme_tasab_find_common_free(const dsme_tasab_t *mine,
                                    const dsme_tasab_t *theirs,
                                    uint8_t sd, uint8_t ch,
                                    uint8_t pref_slot, uint8_t want,
                                    uint8_t *out_slot);

/*---------------------------------------------------------------------------*/
/* CCB                                                                        */
/*---------------------------------------------------------------------------*/
void dsme_ccb_init(dsme_ccb_t *c);
static inline uint8_t dsme_ccb_get(const dsme_ccb_t *c, uint8_t sd) { return c->state[sd]; }
static inline void dsme_ccb_set(dsme_ccb_t *c, uint8_t sd, uint8_t v) { c->state[sd] = v; }

#endif /* DSME_SAB_H_ */
