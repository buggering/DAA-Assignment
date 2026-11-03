#include "dsme-act.h"
#include "dsme.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
void
dsme_act_init(void)
{
  memset(dsme_act, 0, sizeof(dsme_act));
}
/*---------------------------------------------------------------------------*/
dsme_act_entry_t *
dsme_act_add(uint16_t peer, uint8_t sd, uint8_t slot, uint8_t channel_off,
             uint8_t direction, uint8_t is_ext)
{
  uint8_t i;
  dsme_act_entry_t *e;

  /* An existing entry for the same position is refreshed rather than
   * duplicated; this is what happens when a NOTIFY repeats a REPLY. */
  e = dsme_act_lookup(sd, slot, is_ext);
  if(e != NULL) {
    e->peer = peer;
    e->channel_off = channel_off;
    e->direction = direction;
    e->idle_counter = 0;
    return e;
  }
  for(i = 0; i < DSME_MAX_ACT_ENTRIES; i++) {
    if(!(dsme_act[i].flags & DSME_ACT_IN_USE)) {
      e = &dsme_act[i];
      e->peer = peer;
      e->sd_index = sd;
      e->slot_id = slot;
      e->channel_off = channel_off;
      e->direction = direction;
      e->idle_counter = 0;
      e->flags = (uint8_t)(DSME_ACT_IN_USE | (is_ext ? DSME_ACT_IS_EXT : 0));
      return e;
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
dsme_act_entry_t *
dsme_act_lookup(uint8_t sd, uint8_t slot, uint8_t is_ext)
{
  uint8_t i;
  for(i = 0; i < DSME_MAX_ACT_ENTRIES; i++) {
    dsme_act_entry_t *e = &dsme_act[i];
    if((e->flags & DSME_ACT_IN_USE) &&
       e->sd_index == sd && e->slot_id == slot &&
       (((e->flags & DSME_ACT_IS_EXT) != 0) == (is_ext != 0))) {
      return e;
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
void
dsme_act_remove(dsme_act_entry_t *e)
{
  if(e != NULL) {
    memset(e, 0, sizeof(*e));
  }
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_act_remove_peer(uint16_t peer, uint8_t direction, uint8_t is_ext)
{
  uint8_t i, n = 0;
  for(i = 0; i < DSME_MAX_ACT_ENTRIES; i++) {
    dsme_act_entry_t *e = &dsme_act[i];
    if((e->flags & DSME_ACT_IN_USE) && e->peer == peer &&
       e->direction == direction &&
       (((e->flags & DSME_ACT_IS_EXT) != 0) == (is_ext != 0))) {
      memset(e, 0, sizeof(*e));
      n++;
    }
  }
  return n;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_act_count(uint8_t direction, uint8_t is_ext)
{
  uint8_t i, n = 0;
  for(i = 0; i < DSME_MAX_ACT_ENTRIES; i++) {
    dsme_act_entry_t *e = &dsme_act[i];
    if(!(e->flags & DSME_ACT_IN_USE) || e->direction != direction) {
      continue;
    }
    if(is_ext != 0xFF && (((e->flags & DSME_ACT_IS_EXT) != 0) != (is_ext != 0))) {
      continue;
    }
    n++;
  }
  return n;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_act_ext_superframes(uint8_t *out_sd)
{
  uint8_t i, j, n = 0;
  for(i = 0; i < DSME_MAX_ACT_ENTRIES; i++) {
    dsme_act_entry_t *e = &dsme_act[i];
    if(!(e->flags & DSME_ACT_IN_USE) || !(e->flags & DSME_ACT_IS_EXT)) {
      continue;
    }
    for(j = 0; j < n; j++) {
      if(out_sd[j] == e->sd_index) {
        break;
      }
    }
    if(j == n) {
      out_sd[n++] = e->sd_index;
    }
  }
  return n;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_act_age_and_expire(dsme_act_entry_t *expired, uint8_t max)
{
  uint8_t i, n = 0;
  for(i = 0; i < DSME_MAX_ACT_ENTRIES; i++) {
    dsme_act_entry_t *e = &dsme_act[i];
    uint8_t limit;

    if(!(e->flags & DSME_ACT_IN_USE)) {
      continue;
    }
    if(e->idle_counter < 255) {
      e->idle_counter++;
    }
    limit = (e->flags & DSME_ACT_IS_EXT) ? DSME_EXTGTS_EXPIRATION_TIME
                                         : DSME_GTS_EXPIRATION_TIME;
    if(e->idle_counter > limit) {
      if(n < max) {
        memcpy(&expired[n], e, sizeof(dsme_act_entry_t));
        n++;
      }
      memset(e, 0, sizeof(*e));
    }
  }
  return n;
}
/*---------------------------------------------------------------------------*/
void
dsme_act_mark_used(uint8_t sd, uint8_t slot, uint8_t is_ext)
{
  dsme_act_entry_t *e = dsme_act_lookup(sd, slot, is_ext);
  if(e != NULL) {
    e->idle_counter = 0;
  }
}
/*---------------------------------------------------------------------------*/
