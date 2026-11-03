#include "dsme-tacfpext.h"
#include "dsme-sab.h"
#include "dsme-act.h"
#include "dsme.h"
#include "dsme-log.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Channel hopping                                                            */
/*---------------------------------------------------------------------------*/
/*
 * The hopping sequence is the 16 IEEE 802.15.4 channels in order. The extCFP
 * sequence is the same list with the control channel removed, so that extGTS
 * traffic never collides with the beacons and CAP commands that keep the PAN
 * running (Reference C, Section III-C).
 */
static uint8_t
hopping_channel(uint8_t index)
{
  return (uint8_t)(DSME_CHANNEL_MIN + (index % DSME_NUM_CHANNELS));
}
/*---------------------------------------------------------------------------*/
static uint8_t
ext_hopping_channel(uint8_t index)
{
  uint8_t ch = (uint8_t)(DSME_CHANNEL_MIN + (index % DSME_EXT_HOPPING_LEN));
  /* The list skips the control channel; with the control channel at the top
   * of the band, indices 0..14 already avoid it. Guard anyway so that the
   * constant can be moved without silently reintroducing the collision. */
  if(ch == DSME_CONTROL_CHANNEL) {
    ch = (uint8_t)(DSME_CHANNEL_MIN + DSME_EXT_HOPPING_LEN);
  }
  return ch;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_gts_channel(uint8_t cfp_slot, uint8_t sd_index, uint8_t channel_off,
                 uint8_t pan_bsn)
{
  /* Reference C Eq. (1):
   *   m = (i + j*l + macChannelOffset + macPANCoordinatorBSN)
   *         % macHoppingSequenceLength                                     */
  uint32_t m = (uint32_t)cfp_slot +
               (uint32_t)sd_index * DSME_CFP_SLOTS +
               channel_off + pan_bsn;
  return hopping_channel((uint8_t)(m % DSME_NUM_CHANNELS));
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_tacfpext_ext_channel(uint8_t ext_slot, uint8_t sd_index,
                          uint8_t channel_off, uint8_t pan_bsn)
{
  /* Reference C Eq. (3): l = 8 slots per extCFP, modulo the shortened
   * sequence length. */
  uint32_t m = (uint32_t)ext_slot +
               (uint32_t)sd_index * DSME_EXTCFP_SLOTS +
               channel_off + pan_bsn;
  return ext_hopping_channel((uint8_t)(m % DSME_EXT_HOPPING_LEN));
}
/*---------------------------------------------------------------------------*/
/* Algorithm 1: changeable CAP selection                                      */
/*---------------------------------------------------------------------------*/
static uint8_t
contains(const uint8_t *arr, uint8_t n, uint8_t v)
{
  uint8_t i;
  for(i = 0; i < n; i++) {
    if(arr[i] == v) {
      return 1;
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_tacfpext_select_changeable_caps(uint8_t num_slots, uint8_t *out_sid)
{
  uint8_t ocp_sc[DSME_SF_PER_MS];
  uint8_t ext_cfp[DSME_SF_PER_MS], n_ext = 0;
  uint8_t lop[DSME_SF_PER_MS], n_lop = 0;
  uint8_t cap[DSME_SF_PER_MS], n_cap = 0;
  uint8_t con[3 * DSME_SF_PER_MS], n_con = 0;
  uint8_t n_sid = 0;
  uint16_t slot_cnt = 0;
  uint8_t own_k = (uint8_t)(dsme_channel_offset % DSME_EXT_HOPPING_LEN);
  uint8_t i, j, k;

  memset(ocp_sc, 0, sizeof(ocp_sc));

  /* Lines 3-7: superframes in which this node already holds an extGTS. */
  n_ext = dsme_act_ext_superframes(ext_cfp);

  /* Lines 9-18: walk the taSAB. A set bit in a superframe this node has not
   * converted belongs to a one-hop neighbour, which makes that superframe a
   * listen-only period for us. Separately, a bit on our own channel offset
   * occupies a slot we could otherwise have used, so it counts towards
   * ocpSC. */
  for(i = 0; i < DSME_SF_PER_MS; i++) {
    for(j = 0; j < DSME_EXTCFP_SLOTS; j++) {
      for(k = 0; k < DSME_EXT_HOPPING_LEN; k++) {
        if(!dsme_tasab_test(&dsme_tasab, i, j, k)) {
          continue;
        }
        if(!contains(ext_cfp, n_ext, i) && !contains(lop, n_lop, i)) {
          lop[n_lop++] = i;
        }
        if(k == own_k) {
          ocp_sc[i]++;
        }
      }
    }
  }

  /* Lines 19-23: everything else is still an untouched CAP, except the CAP of
   * superframe 0, which the protocol never converts so that the network keeps
   * one CAP in common. */
  for(i = 0; i < DSME_SF_PER_MS; i++) {
    if(i != 0 && !contains(ext_cfp, n_ext, i) && !contains(lop, n_lop, i)) {
      cap[n_cap++] = i;
    }
  }

  /* Line 24: extCFP first (no new CAP is lost), then LOP (the neighbourhood
   * has already given that CAP up), then a fresh CAP as a last resort. */
  for(i = 0; i < n_ext; i++) {
    if(ext_cfp[i] != 0) {
      con[n_con++] = ext_cfp[i];
    }
  }
  for(i = 0; i < n_lop; i++) {
    if(lop[i] != 0) {
      con[n_con++] = lop[i];
    }
  }
  for(i = 0; i < n_cap; i++) {
    con[n_con++] = cap[i];
  }

  /* Lines 26-35. Note that Algorithm 1 as printed breaks out of this loop
   * when slotCnt is still below numSlots; the accompanying text ("if the
   * number of available slots is less than numSlots, the superframe ID of the
   * next extCFP in conArray is appended to SID in the same way") makes clear
   * the intent is the opposite -- keep appending until the demand is covered,
   * then stop. The corrected sense is used here. */
  for(i = 0; i < n_con; i++) {
    uint8_t sid = con[i];
    if(ocp_sc[sid] < DSME_EXTCFP_SLOTS) {
      out_sid[n_sid++] = sid;
      slot_cnt = (uint16_t)(slot_cnt + (DSME_EXTCFP_SLOTS - ocp_sc[sid]));
    }
    if(slot_cnt >= num_slots) {
      break;
    }
  }

  /* Lines 36-38: an all-or-nothing result. Converting CAPs that still cannot
   * carry the demand would cost the neighbourhood contention bandwidth for
   * nothing. */
  if(slot_cnt < num_slots) {
    return 0;
  }
  return n_sid;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_tacfpext_free_ext_slots(uint8_t sid)
{
  uint8_t own_k = (uint8_t)(dsme_channel_offset % DSME_EXT_HOPPING_LEN);
  uint8_t j, occupied = 0;

  if(sid >= DSME_SF_PER_MS) {
    return 0;
  }
  for(j = 0; j < DSME_EXTCFP_SLOTS; j++) {
    if(dsme_tasab_test(&dsme_tasab, sid, j, own_k)) {
      occupied++;
    }
  }
  return (uint8_t)(DSME_EXTCFP_SLOTS - occupied);
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_tacfpext_free_ext_slots_total(void)
{
  uint8_t i, total = 0;
  for(i = 0; i < DSME_SF_PER_MS; i++) {
    if(dsme_ccb_get(&dsme_ccb, i) == DSME_CCB_EXTCFP) {
      total = (uint8_t)(total + dsme_tacfpext_free_ext_slots(i));
    }
  }
  return total;
}
/*---------------------------------------------------------------------------*/
/* Algorithm 2: CCB update                                                    */
/*---------------------------------------------------------------------------*/
void
dsme_tacfpext_ccb_update(uint8_t sid, uint8_t mgmt, uint8_t involves_me)
{
  uint8_t node_present = 0;
  uint8_t node_cnt = 0;
  uint16_t all_cnt = 0;
  uint8_t sids[DSME_SF_PER_MS];
  uint8_t n, i, j, k;

  if(sid >= DSME_SF_PER_MS) {
    return;
  }

  if(mgmt == DSME_MGMT_ALLOCATION) {
    /* Lines 3-10. */
    if(involves_me) {
      dsme_ccb_set(&dsme_ccb, sid, DSME_CCB_EXTCFP);
    } else if(dsme_ccb_get(&dsme_ccb, sid) == DSME_CCB_CAP) {
      dsme_ccb_set(&dsme_ccb, sid, DSME_CCB_LOP);
    }
    return;
  }

  /* Lines 12-30: after a deallocation the state has to be recomputed from
   * what is left in taACT and taSAB. */
  n = dsme_act_ext_superframes(sids);
  for(i = 0; i < n; i++) {
    if(sids[i] == sid) {
      node_present = 1;
    }
  }
  for(i = 0; i < DSME_MAX_ACT_ENTRIES; i++) {
    dsme_act_entry_t *e = &dsme_act[i];
    if((e->flags & DSME_ACT_IN_USE) && (e->flags & DSME_ACT_IS_EXT) &&
       e->sd_index == sid) {
      node_cnt++;
    }
  }
  for(j = 0; j < DSME_EXTCFP_SLOTS; j++) {
    for(k = 0; k < DSME_EXT_HOPPING_LEN; k++) {
      if(dsme_tasab_test(&dsme_tasab, sid, j, k)) {
        all_cnt++;
      }
    }
  }

  if(node_present) {
    dsme_ccb_set(&dsme_ccb, sid, DSME_CCB_EXTCFP);
  } else if(all_cnt > node_cnt) {
    dsme_ccb_set(&dsme_ccb, sid, DSME_CCB_LOP);
  } else {
    dsme_ccb_set(&dsme_ccb, sid, DSME_CCB_CAP);
  }
}
/*---------------------------------------------------------------------------*/
