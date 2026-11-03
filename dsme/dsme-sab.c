#include "dsme-sab.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
void
dsme_sab_init(dsme_sab_t *sab)
{
  memset(sab, 0, sizeof(*sab));
}
/*---------------------------------------------------------------------------*/
void
dsme_sab_set(dsme_sab_t *sab, uint8_t sd, uint8_t slot)
{
  if(sd < DSME_SF_PER_MS && slot < 16) {
    sab->sub[sd] |= (uint16_t)(1u << slot);
  }
}
/*---------------------------------------------------------------------------*/
void
dsme_sab_clear(dsme_sab_t *sab, uint8_t sd, uint8_t slot)
{
  if(sd < DSME_SF_PER_MS && slot < 16) {
    sab->sub[sd] &= (uint16_t)~(1u << slot);
  }
}
/*---------------------------------------------------------------------------*/
int
dsme_sab_test(const dsme_sab_t *sab, uint8_t sd, uint8_t slot)
{
  if(sd >= DSME_SF_PER_MS || slot >= 16) {
    return 1; /* out of range reads as occupied so callers never pick it */
  }
  return (sab->sub[sd] & (uint16_t)(1u << slot)) != 0;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_sab_free_count(const dsme_sab_t *sab)
{
  uint8_t sd, slot, n = 0;
  for(sd = 0; sd < DSME_SF_PER_MS; sd++) {
    uint8_t cnt = dsme_cfp_slot_count(sd);
    for(slot = 0; slot < cnt; slot++) {
      if(!dsme_sab_test(sab, sd, slot)) {
        n++;
      }
    }
  }
  return n;
}
/*---------------------------------------------------------------------------*/
/*
 * Reference B, Section 5.1.5, Step 2: the responder "compares the received
 * bitmap with its own, in order to find a number of common free slots equal
 * to the requested number of DSME-GTSs". The preferred superframe and slot
 * indicated by the requester are honoured first; "if the preferred slot is
 * not available, the next one is considered, and so on".
 *
 * The multi-superframe slot grid is ragged under CAP reduction (SD 0 has 7
 * GTSs, the others 15), so it is flattened once and then scanned circularly
 * from the preferred position.
 */
uint8_t
dsme_sab_find_common_free(const dsme_sab_t *mine, const dsme_sab_t *theirs,
                          uint8_t pref_sd, uint8_t pref_slot, uint8_t want,
                          uint8_t *out_sd, uint8_t *out_slot)
{
  uint8_t flat_sd[DSME_SF_PER_MS * 16];
  uint8_t flat_slot[DSME_SF_PER_MS * 16];
  uint8_t total = 0;
  uint8_t start = 0;
  uint8_t found = 0;
  uint8_t sd, slot, i;

  for(sd = 0; sd < DSME_SF_PER_MS; sd++) {
    uint8_t cnt = dsme_cfp_slot_count(sd);
    for(slot = 0; slot < cnt; slot++) {
      if(sd == pref_sd && slot == pref_slot) {
        start = total;
      }
      flat_sd[total] = sd;
      flat_slot[total] = slot;
      total++;
    }
  }

  for(i = 0; i < total && found < want; i++) {
    uint8_t k = (uint8_t)((start + i) % total);
    if(!dsme_sab_test(mine, flat_sd[k], flat_slot[k]) &&
       !dsme_sab_test(theirs, flat_sd[k], flat_slot[k])) {
      out_sd[found] = flat_sd[k];
      out_slot[found] = flat_slot[k];
      found++;
    }
  }
  return found;
}
/*---------------------------------------------------------------------------*/
void
dsme_tasab_init(dsme_tasab_t *t)
{
  memset(t, 0, sizeof(*t));
}
/*---------------------------------------------------------------------------*/
void
dsme_tasab_set(dsme_tasab_t *t, uint8_t sd, uint8_t slot, uint8_t ch)
{
  if(sd < DSME_SF_PER_MS && slot < DSME_EXTCFP_SLOTS && ch < DSME_NUM_CHANNELS) {
    t->sub[sd][slot] |= (uint16_t)(1u << ch);
  }
}
/*---------------------------------------------------------------------------*/
void
dsme_tasab_clear(dsme_tasab_t *t, uint8_t sd, uint8_t slot, uint8_t ch)
{
  if(sd < DSME_SF_PER_MS && slot < DSME_EXTCFP_SLOTS && ch < DSME_NUM_CHANNELS) {
    t->sub[sd][slot] &= (uint16_t)~(1u << ch);
  }
}
/*---------------------------------------------------------------------------*/
int
dsme_tasab_test(const dsme_tasab_t *t, uint8_t sd, uint8_t slot, uint8_t ch)
{
  if(sd >= DSME_SF_PER_MS || slot >= DSME_EXTCFP_SLOTS || ch >= DSME_NUM_CHANNELS) {
    return 1;
  }
  return (t->sub[sd][slot] & (uint16_t)(1u << ch)) != 0;
}
/*---------------------------------------------------------------------------*/
int
dsme_tasab_slot_busy(const dsme_tasab_t *t, uint8_t sd, uint8_t slot)
{
  if(sd >= DSME_SF_PER_MS || slot >= DSME_EXTCFP_SLOTS) {
    return 1;
  }
  return t->sub[sd][slot] != 0;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_tasab_find_common_free(const dsme_tasab_t *mine, const dsme_tasab_t *theirs,
                            uint8_t sd, uint8_t ch, uint8_t pref_slot,
                            uint8_t want, uint8_t *out_slot)
{
  uint8_t found = 0;
  uint8_t i;

  if(pref_slot >= DSME_EXTCFP_SLOTS) {
    pref_slot = 0;
  }
  for(i = 0; i < DSME_EXTCFP_SLOTS && found < want; i++) {
    uint8_t slot = (uint8_t)((pref_slot + i) % DSME_EXTCFP_SLOTS);
    if(!dsme_tasab_test(mine, sd, slot, ch) &&
       !dsme_tasab_test(theirs, sd, slot, ch)) {
      out_slot[found] = slot;
      found++;
    }
  }
  return found;
}
/*---------------------------------------------------------------------------*/
void
dsme_ccb_init(dsme_ccb_t *c)
{
  uint8_t i;
  for(i = 0; i < DSME_SF_PER_MS; i++) {
    c->state[i] = DSME_CCB_CAP;
  }
}
/*---------------------------------------------------------------------------*/
