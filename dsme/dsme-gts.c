#include "dsme-gts.h"
#include "dsme.h"
#include "dsme-act.h"
#include "dsme-sab.h"
#include "dsme-queue.h"
#include "dsme-tacfpext.h"
#include "dsme-predictor.h"
#include "dsme-log.h"
#include "lib/random.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Local state                                                                */
/*---------------------------------------------------------------------------*/
static uint8_t cmd_seqno;
/* Superframe to name as "preferred" in the next REQUEST. It advances after
 * every attempt so that consecutive requests probe different CFPs, which is
 * the behaviour that makes 2^(MO-SO)/k consecutive refusals meaningful. */
static uint8_t next_pref_sd;
/* Arrivals counted over the multi-superframe now in progress, used by the
 * LASTVAL demand estimator. */
static uint16_t arrivals_this_ms;
static uint8_t last_reported_slots = 0xFF;

/*---------------------------------------------------------------------------*/
static void
log_alloc_state(const char *why)
{
  uint8_t tot = dsme_allocated_tx_slots();
  uint8_t ext = dsme_allocated_ext_tx_slots();
  last_reported_slots = tot;
  DSME_LOG("ALLOC|asn=%lu|n=%u|slots=%u|ext=%u|q=%u|why=%s",
           (unsigned long)dsme_time.asn, dsme_short_addr, tot, ext,
           dsme_queue_count(&dsme_queue), why != NULL ? why : "-");
}
/*---------------------------------------------------------------------------*/
static int
queue_cmd(uint16_t mac_dst, const dsme_cmd_info_t *info)
{
  dsme_cmd_t c;
  const dsme_sab_t *sab = dsme_cmd_is_ext(info->cmd) ? NULL : &dsme_sab;
  const dsme_tasab_t *tasab = dsme_cmd_is_ext(info->cmd) ? &dsme_tasab : NULL;
  uint8_t urgent = (info->cmd == DSME_CMD_GTS_REPLY ||
                    info->cmd == DSME_CMD_EXTGTS_REPLY);
  int ok;

  memset(&c, 0, sizeof(c));
  c.dst = mac_dst;
  c.len = dsme_pkt_build_cmd(c.buf, cmd_seqno++, dsme_short_addr, mac_dst,
                             info, sab, tasab);
  if(c.len > DSME_MAX_CMD_LEN) {
    return 0;
  }
  ok = urgent ? dsme_cmd_queue_push_front(&dsme_cmd_queue, &c)
              : dsme_cmd_queue_push(&dsme_cmd_queue, &c);
  if(!ok) {
    dsme_stats.cmd_queue_full++;
  }
  return ok;
}
/*---------------------------------------------------------------------------*/
/* Bitmap bookkeeping for one allocated or released slot                      */
/*---------------------------------------------------------------------------*/
static void
mark_slot(uint8_t is_ext, uint8_t sd, uint8_t slot, uint8_t channel_off,
          uint8_t allocate)
{
  if(is_ext) {
    uint8_t k = (uint8_t)(channel_off % DSME_EXT_HOPPING_LEN);
    if(allocate) {
      dsme_tasab_set(&dsme_tasab, sd, slot, k);
    } else {
      dsme_tasab_clear(&dsme_tasab, sd, slot, k);
    }
  } else {
    if(allocate) {
      dsme_sab_set(&dsme_sab, sd, slot);
    } else {
      dsme_sab_clear(&dsme_sab, sd, slot);
    }
  }
}
/*---------------------------------------------------------------------------*/
void
dsme_gts_init(void)
{
  cmd_seqno = 0;
  next_pref_sd = 0;
  arrivals_this_ms = 0;
  last_reported_slots = 0xFF;
  memset(dsme_txn, 0, sizeof(dsme_txn));
}
/*---------------------------------------------------------------------------*/
/* Requester side                                                             */
/*---------------------------------------------------------------------------*/
static void
start_request(uint16_t peer, uint8_t num_slots, uint8_t is_ext, uint8_t pref_sd)
{
  dsme_cmd_info_t info;

  if(num_slots == 0 || num_slots > DSME_MAX_SLOTS_PER_REQUEST) {
    num_slots = num_slots == 0 ? 1 : DSME_MAX_SLOTS_PER_REQUEST;
  }
  memset(&info, 0, sizeof(info));
  info.cmd = is_ext ? DSME_CMD_EXTGTS_REQUEST : DSME_CMD_GTS_REQUEST;
  info.mgmt = DSME_MGMT_ALLOCATION;
  info.direction = DSME_DIRECTION_TX;
  info.status = 0;
  info.target = peer;
  /* In channel hopping the sender transmits on the receiver's channel
   * offset, so the receiver's offset is what the pair must agree on. It is
   * learned from the REPLY; until then the request carries our own so the
   * responder can detect a conflict. */
  info.channel_off = dsme_channel_offset;
  info.num_slots = num_slots;
  info.pref_sd = pref_sd;
  /* The requester is free to name any preferred slot (Reference B, Fig. 12a).
   * Spreading that choice at random matters: when several pairs request in
   * the same CAP they commit before seeing each other's REPLY, and if they
   * all prefer slot 0 the result is a duplicated allocation that both pairs
   * then have to undo. */
  info.pref_slot = (uint8_t)(random_rand() %
                             (dsme_cfp_slot_count(pref_sd) ?
                              dsme_cfp_slot_count(pref_sd) : 1));
  info.n_alloc = 0;
  info.n_sub = DSME_SAB_SUBBLOCKS_PER_REQUEST;
  info.sub_start = pref_sd;

  if(!queue_cmd(peer, &info)) {
    return;
  }
  {
    dsme_txn_t *t = &dsme_txn[is_ext ? 1 : 0];
    t->state = DSME_TXN_REQUEST_QUEUED;
    t->is_ext = is_ext;
    t->peer = peer;
    t->num_slots = num_slots;
    t->pref_sd = pref_sd;
    t->pref_slot = info.pref_slot;
    t->started_us = (uint32_t)dsme_now_us();
    t->deadline_sf = dsme_sf_of(dsme_time.asn) + DSME_REPLY_WAIT_SUPERFRAMES + 1;
  }

  if(is_ext) {
    dsme_stats.extgts_requests_sent++;
  } else {
    dsme_stats.gts_requests_sent++;
  }
  DSME_LOG("REQ|asn=%lu|n=%u|to=%u|slots=%u|ext=%u|sd=%u",
           (unsigned long)dsme_time.asn, dsme_short_addr, peer,
           num_slots, is_ext, pref_sd);
}
/*---------------------------------------------------------------------------*/
void
dsme_gts_request_sent(uint8_t is_ext)
{
  dsme_txn_t *t = &dsme_txn[is_ext ? 1 : 0];

  if(t->state == DSME_TXN_REQUEST_QUEUED) {
    t->state = DSME_TXN_WAIT_REPLY;
    /* The reply window starts when the request actually reaches the air, not
     * when it was queued; CAP contention can hold it back for superframes. */
    t->deadline_sf = dsme_sf_of(dsme_time.asn) + DSME_REPLY_WAIT_SUPERFRAMES;
  }
}
/*---------------------------------------------------------------------------*/
static void
transaction_failed(uint8_t is_ext, const char *why)
{
  if(is_ext) {
    dsme_stats.extgts_alloc_fail++;
    /* A refused extension must not become a per-superframe retry loop. */
    if(dsme_pred.cooldown < 2) {
      dsme_pred.cooldown = 2;
    }
  } else {
    dsme_stats.gts_alloc_fail++;
    if(dsme_alloc_fail_count < 255) {
      dsme_alloc_fail_count++;
    }
  }
  DSME_LOG("FAIL|asn=%lu|n=%u|ext=%u|fails=%u|why=%s",
           (unsigned long)dsme_time.asn, dsme_short_addr, is_ext,
           dsme_alloc_fail_count, why);
  memset(&dsme_txn[is_ext ? 1 : 0], 0, sizeof(dsme_txn_t));
  if(!is_ext) {
    /* Probe the next CFP of the multi-superframe on the following attempt. */
    next_pref_sd = (uint8_t)((next_pref_sd + 1) % DSME_SF_PER_MS);
  }
}
/*---------------------------------------------------------------------------*/
/* Responder side                                                             */
/*---------------------------------------------------------------------------*/
static void
handle_request(const dsme_hdr_t *h, const dsme_cmd_info_t *req)
{
  dsme_cmd_info_t rep;
  uint8_t out_sd[DSME_MAX_SLOTS_PER_REQUEST];
  uint8_t out_slot[DSME_MAX_SLOTS_PER_REQUEST];
  uint8_t found = 0;
  uint8_t is_ext = dsme_cmd_is_ext(req->cmd);
  uint8_t i;

  memset(&rep, 0, sizeof(rep));
  rep.cmd = is_ext ? DSME_CMD_EXTGTS_REPLY : DSME_CMD_GTS_REPLY;
  rep.mgmt = DSME_MGMT_ALLOCATION;
  rep.direction = req->direction;
  rep.target = h->src;
  /* The pair will hop over the receiver's channel offset: ours. */
  rep.channel_off = dsme_channel_offset;
  rep.num_slots = req->num_slots;
  rep.pref_sd = req->pref_sd;
  rep.pref_slot = req->pref_slot;
  rep.n_sub = DSME_SAB_SUBBLOCKS_PER_REQUEST;
  rep.sub_start = req->pref_sd;

  if(is_ext) {
    dsme_tasab_t theirs;
    uint8_t k = (uint8_t)(dsme_channel_offset % DSME_EXT_HOPPING_LEN);
    /* Only the sub-block the request carried is meaningful; the comparison
     * below never looks outside req->pref_sd. */
    memcpy(theirs.sub, req->tasab, sizeof(theirs.sub));
    found = dsme_tasab_find_common_free(&dsme_tasab, &theirs, req->pref_sd, k,
                                        req->pref_slot, req->num_slots,
                                        out_slot);
    for(i = 0; i < found; i++) {
      out_sd[i] = req->pref_sd;
    }
  } else {
    dsme_sab_t theirs;
    dsme_sab_init(&theirs);
    /* Only the sub-blocks the request actually carried are known; the rest of
     * the requester's multi-superframe is treated as occupied, which is what
     * forces the repeated exchanges Reference C describes. */
    for(i = 0; i < DSME_SF_PER_MS; i++) {
      uint8_t covered = 0;
      uint8_t j;
      for(j = 0; j < req->n_sub; j++) {
        if((uint8_t)((req->sub_start + j) % DSME_SF_PER_MS) == i) {
          covered = 1;
        }
      }
      theirs.sub[i] = covered ? req->sab[i] : 0xFFFF;
    }
    found = dsme_sab_find_common_free(&dsme_sab, &theirs, req->pref_sd,
                                      req->pref_slot, req->num_slots,
                                      out_sd, out_slot);
  }

  /* Reference B, Section 5.1.5: the allocation succeeds only when as many
   * common free slots as were requested are found. A partial match is a
   * refusal, and it is those refusals that TaCFPext counts. */
  if(found < req->num_slots) {
    rep.status = DSME_STATUS_DENIED_NO_SLOT;
    rep.n_alloc = 0;
    queue_cmd(DSME_ADDR_BROADCAST, &rep);
    DSME_LOG("DENY|asn=%lu|n=%u|to=%u|want=%u|have=%u|ext=%u",
             (unsigned long)dsme_time.asn, dsme_short_addr, h->src,
             req->num_slots, found, is_ext);
    return;
  }

  /* Commit: the responder is the receiver of this link. */
  for(i = 0; i < found; i++) {
    if(dsme_act_add(h->src, out_sd[i], out_slot[i], dsme_channel_offset,
                    DSME_DIRECTION_RX, is_ext) == NULL) {
      rep.status = DSME_STATUS_DENIED_NO_ACT;
      rep.n_alloc = 0;
      queue_cmd(DSME_ADDR_BROADCAST, &rep);
      return;
    }
    mark_slot(is_ext, out_sd[i], out_slot[i], dsme_channel_offset, 1);
    rep.alloc_sd[i] = out_sd[i];
    rep.alloc_slot[i] = out_slot[i];
  }
  rep.n_alloc = found;
  rep.status = DSME_STATUS_SUCCESS;

  if(is_ext) {
    dsme_tacfpext_ccb_update(out_sd[0], DSME_MGMT_ALLOCATION, 1);
  }
  queue_cmd(DSME_ADDR_BROADCAST, &rep);
  log_alloc_state("reply");
}
/*---------------------------------------------------------------------------*/
static void
handle_reply(const dsme_hdr_t *h, const dsme_cmd_info_t *rep)
{
  uint8_t is_ext = dsme_cmd_is_ext(rep->cmd);
  dsme_cmd_info_t ntf;
  uint8_t i;

  dsme_txn_t *txn = &dsme_txn[is_ext ? 1 : 0];

  if(txn->state != DSME_TXN_WAIT_REPLY || txn->peer != h->src) {
    return;
  }
  if(rep->status != DSME_STATUS_SUCCESS) {
    transaction_failed(is_ext, "denied");
    return;
  }

  for(i = 0; i < rep->n_alloc; i++) {
    /* The sender hops over the receiver's channel offset, which the REPLY
     * carries. */
    dsme_act_add(h->src, rep->alloc_sd[i], rep->alloc_slot[i],
                 rep->channel_off, DSME_DIRECTION_TX, is_ext);
    mark_slot(is_ext, rep->alloc_sd[i], rep->alloc_slot[i], rep->channel_off, 1);
  }
  if(is_ext) {
    dsme_stats.extgts_alloc_ok++;
    if(rep->n_alloc > 0) {
      dsme_tacfpext_ccb_update(rep->alloc_sd[0], DSME_MGMT_ALLOCATION, 1);
      dsme_stats.cap_converted++;
    }
    dsme_predictor_note_conversion(&dsme_pred);
  } else {
    dsme_stats.gts_alloc_ok++;
    dsme_alloc_fail_count = 0;
  }

  /* Step 3: NOTIFY, so that the requester's own one-hop neighbours learn
   * about the slots too (Reference B, Fig. 12c). */
  memset(&ntf, 0, sizeof(ntf));
  ntf.cmd = is_ext ? DSME_CMD_EXTGTS_NOTIFY : DSME_CMD_GTS_NOTIFY;
  ntf.mgmt = DSME_MGMT_ALLOCATION;
  ntf.direction = DSME_DIRECTION_TX;
  ntf.target = h->src;
  ntf.channel_off = rep->channel_off;
  ntf.num_slots = rep->n_alloc;
  ntf.n_alloc = rep->n_alloc;
  memcpy(ntf.alloc_sd, rep->alloc_sd, rep->n_alloc);
  memcpy(ntf.alloc_slot, rep->alloc_slot, rep->n_alloc);
  ntf.n_sub = 0;
  queue_cmd(DSME_ADDR_BROADCAST, &ntf);

  DSME_LOG("OK|asn=%lu|n=%u|peer=%u|got=%u|ext=%u|lat=%lu",
           (unsigned long)dsme_time.asn, dsme_short_addr, h->src,
           rep->n_alloc, is_ext,
           (unsigned long)((uint32_t)dsme_now_us() - txn->started_us));

  memset(txn, 0, sizeof(*txn));
  log_alloc_state("alloc");
}
/*---------------------------------------------------------------------------*/
/*
 * A REPLY or NOTIFY that names some other pair. Reference B: the overhearing
 * node updates its SAB, and raises a Duplicated Allocation Notification if the
 * slots clash with ones it already holds.
 */
/*
 * Identity of a link, for breaking ties between two allocations made in the
 * same instant: the lower of the two endpoint addresses. Both sides of a
 * clash compute the same pair of values, so exactly one of them decides it is
 * the incumbent and raises the notification -- without this, each pair
 * cancels the other and both have to start over.
 */
static uint16_t
pair_key(uint16_t a, uint16_t b)
{
  return a < b ? a : b;
}
/*---------------------------------------------------------------------------*/
static void
handle_overheard(const dsme_hdr_t *h, const dsme_cmd_info_t *info)
{
  uint8_t is_ext = dsme_cmd_is_ext(info->cmd);
  uint8_t conflict = 0;
  uint8_t i;

  for(i = 0; i < info->n_alloc; i++) {
    dsme_act_entry_t *mine =
      dsme_act_lookup(info->alloc_sd[i], info->alloc_slot[i], is_ext);
    if(mine != NULL) {
      /* For an extGTS the clash is only real when the channel offsets
       * coincide; the extCFP is reusable across offsets. */
      if(!is_ext ||
         (uint8_t)(mine->channel_off % DSME_EXT_HOPPING_LEN) ==
         (uint8_t)(info->channel_off % DSME_EXT_HOPPING_LEN)) {
        if(pair_key(dsme_short_addr, mine->peer) <
           pair_key(h->src, info->target)) {
          conflict = 1;
        }
        continue;
      }
    }
    if(info->mgmt == DSME_MGMT_ALLOCATION) {
      mark_slot(is_ext, info->alloc_sd[i], info->alloc_slot[i],
                info->channel_off, 1);
    } else {
      mark_slot(is_ext, info->alloc_sd[i], info->alloc_slot[i],
                info->channel_off, 0);
    }
  }

  if(is_ext && info->n_alloc > 0) {
    dsme_tacfpext_ccb_update(info->alloc_sd[0], info->mgmt, 0);
  }

  if(conflict && info->mgmt == DSME_MGMT_ALLOCATION) {
    dsme_cmd_info_t dup;
    memset(&dup, 0, sizeof(dup));
    dup.cmd = is_ext ? DSME_CMD_EXT_DUPLICATED_ALLOC : DSME_CMD_DUPLICATED_ALLOC;
    dup.mgmt = DSME_MGMT_DEALLOCATION;
    dup.target = h->src;
    dup.channel_off = info->channel_off;
    dup.n_alloc = info->n_alloc;
    memcpy(dup.alloc_sd, info->alloc_sd, info->n_alloc);
    memcpy(dup.alloc_slot, info->alloc_slot, info->n_alloc);
    queue_cmd(DSME_ADDR_BROADCAST, &dup);
    DSME_LOG("DUP|asn=%lu|n=%u|src=%u|ext=%u",
             (unsigned long)dsme_time.asn, dsme_short_addr, h->src, is_ext);
  }
}
/*---------------------------------------------------------------------------*/
static void
handle_deallocation(const dsme_cmd_info_t *info, uint8_t involves_me)
{
  uint8_t is_ext = dsme_cmd_is_ext(info->cmd);
  uint8_t i;

  for(i = 0; i < info->n_alloc; i++) {
    if(involves_me) {
      dsme_act_entry_t *e =
        dsme_act_lookup(info->alloc_sd[i], info->alloc_slot[i], is_ext);
      if(e != NULL) {
        dsme_act_remove(e);
      }
    }
    mark_slot(is_ext, info->alloc_sd[i], info->alloc_slot[i],
              info->channel_off, 0);
  }
  if(is_ext && info->n_alloc > 0) {
    dsme_tacfpext_ccb_update(info->alloc_sd[0], DSME_MGMT_DEALLOCATION,
                             involves_me);
  }
  if(involves_me) {
    log_alloc_state("dealloc");
  }
}
/*---------------------------------------------------------------------------*/
void
dsme_gts_rx_cmd(const dsme_hdr_t *h, const dsme_cmd_info_t *info)
{
  uint8_t for_me = (info->target == dsme_short_addr);

  switch(info->cmd) {
  case DSME_CMD_GTS_REQUEST:
  case DSME_CMD_EXTGTS_REQUEST:
    if(h->dst == dsme_short_addr) {
      handle_request(h, info);
    }
    break;

  case DSME_CMD_GTS_REPLY:
  case DSME_CMD_EXTGTS_REPLY:
    if(for_me) {
      handle_reply(h, info);
    } else if(info->status == DSME_STATUS_SUCCESS) {
      handle_overheard(h, info);
    }
    break;

  case DSME_CMD_GTS_NOTIFY:
  case DSME_CMD_EXTGTS_NOTIFY:
    if(info->mgmt == DSME_MGMT_DEALLOCATION) {
      handle_deallocation(info, for_me);
    } else if(!for_me) {
      handle_overheard(h, info);
    }
    break;

  case DSME_CMD_DUPLICATED_ALLOC:
  case DSME_CMD_EXT_DUPLICATED_ALLOC:
    /* Both endpoints of the clashing allocation must undo it. */
    handle_deallocation(info, for_me || info->target == dsme_tx_peer);
    break;

  default:
    break;
  }
}
/*---------------------------------------------------------------------------*/
/* Deallocation on expiry                                                     */
/*---------------------------------------------------------------------------*/
static void
announce_deallocation(const dsme_act_entry_t *expired, uint8_t n)
{
  dsme_cmd_info_t info;
  uint8_t i, k;

  /* Expired GTSs and extGTSs are announced separately: the command carries a
   * single kind of bitmap. */
  for(k = 0; k < 2; k++) {
    uint8_t is_ext = k;
    uint8_t count = 0;

    memset(&info, 0, sizeof(info));
    for(i = 0; i < n && count < DSME_MAX_SLOTS_PER_REQUEST; i++) {
      if(((expired[i].flags & DSME_ACT_IS_EXT) != 0) != (is_ext != 0)) {
        continue;
      }
      info.alloc_sd[count] = expired[i].sd_index;
      info.alloc_slot[count] = expired[i].slot_id;
      info.channel_off = expired[i].channel_off;
      info.target = expired[i].peer;
      mark_slot(is_ext, expired[i].sd_index, expired[i].slot_id,
                expired[i].channel_off, 0);
      count++;
    }
    if(count == 0) {
      continue;
    }
    info.cmd = is_ext ? DSME_CMD_EXTGTS_NOTIFY : DSME_CMD_GTS_NOTIFY;
    info.mgmt = DSME_MGMT_DEALLOCATION;
    info.n_alloc = count;
    info.num_slots = count;
    info.n_sub = 0;
    queue_cmd(DSME_ADDR_BROADCAST, &info);
    if(is_ext) {
      dsme_tacfpext_ccb_update(info.alloc_sd[0], DSME_MGMT_DEALLOCATION, 1);
    }
    DSME_LOG("EXPIRE|asn=%lu|n=%u|cnt=%u|ext=%u",
             (unsigned long)dsme_time.asn, dsme_short_addr, count, is_ext);
  }
}
/*---------------------------------------------------------------------------*/
/* Demand and triggers                                                        */
/*---------------------------------------------------------------------------*/
void
dsme_gts_note_arrival(void)
{
  if(arrivals_this_ms < 0xFFFF) {
    arrivals_this_ms++;
  }
}
/*---------------------------------------------------------------------------*/
void
dsme_gts_multisuperframe_tick(void)
{
  dsme_act_entry_t expired[DSME_MAX_ACT_ENTRIES];
  uint8_t n;

  /* Shed packets the current multi-superframe could not carry. */
  {
    uint32_t now_ms = dsme_ms_of(dsme_time.asn);
    dsme_packet_t *head;
    while((head = dsme_queue_peek(&dsme_queue)) != NULL &&
          now_ms > head->deadline_ms) {
      dsme_queue_pop(&dsme_queue);
      dsme_stats.dropped_deadline++;
    }
  }

  n = dsme_act_age_and_expire(expired, DSME_MAX_ACT_ENTRIES);
  if(n > 0) {
    announce_deallocation(expired, n);
    log_alloc_state("expire");
  }

#if DSME_DEMAND_SRC == DSME_DEMAND_LASTVAL
  dsme_demand_slots = arrivals_this_ms > DSME_SAB_TOTAL_SLOTS
                        ? DSME_SAB_TOTAL_SLOTS
                        : (uint8_t)arrivals_this_ms;
#endif
  arrivals_this_ms = 0;
}
/*---------------------------------------------------------------------------*/
#if DSME_MODE == DSME_MODE_TACFPEXT || DSME_MODE == DSME_MODE_PREDICTIVE
static void
start_extension(uint8_t need, uint8_t reason)
{
  uint8_t sids[DSME_SF_PER_MS];
  uint8_t n_sid;
  uint8_t want;

  /* Reference C, Algorithm 1. */
  n_sid = dsme_tacfpext_select_changeable_caps(need, sids);
  if(n_sid == 0) {
    DSME_LOG("NOCAP|asn=%lu|n=%u|need=%u",
             (unsigned long)dsme_time.asn, dsme_short_addr, need);
    return;
  }

  if(reason == DSME_TRIG_PREDICTIVE) {
    dsme_stats.trig_predictive++;
  } else {
    dsme_stats.trig_reactive++;
  }
  DSME_LOG("TRIG|asn=%lu|n=%u|kind=%s|need=%u|sid=%u|fails=%u|fc=%ld|freecfp=%u",
           (unsigned long)dsme_time.asn, dsme_short_addr,
           reason == DSME_TRIG_PREDICTIVE ? "pred" : "react",
           need, sids[0], dsme_alloc_fail_count,
           (long)(dsme_pred.last_forecast >> 10), dsme_sab_free_count(&dsme_sab));

  /* Allocate inside the highest-priority changeable CAP first. */
  want = dsme_tacfpext_free_ext_slots(sids[0]);
  if(want > need) {
    want = need;
  }
  if(want == 0) {
    want = 1;
  }
  start_request(dsme_tx_peer, want, 1, sids[0]);
  /* The reactive counter has done its job; clear it so a successful extension
   * is not immediately followed by another. */
  dsme_alloc_fail_count = 0;
}
#endif /* TaCFPext or P-TaCFPext */
/*---------------------------------------------------------------------------*/
void
dsme_gts_superframe_tick(void)
{
  uint8_t g_alloc, need;
#if DSME_MODE == DSME_MODE_PREDICTIVE
  dsme_pred_action_t action;
  uint8_t free_cfp;
#endif

  if(!dsme_joined || dsme_tx_peer == 0) {
    return;
  }

  g_alloc = dsme_allocated_tx_slots();

  /* One time-resolved sample per superframe. Everything the offline analysis
   * needs to reconstruct a node's trajectory through a burst -- allocation,
   * backlog, losses and accumulated delay -- is a difference of consecutive
   * samples, so no per-packet logging is required. */
  DSME_LOG("SAMP|asn=%lu|n=%u|slots=%u|ext=%u|q=%u|dem=%u|fails=%u"
           "|txok=%lu|noack=%lu|dq=%lu|dg=%lu|gen=%lu|rx=%lu"
           "|dsum=%lu|dcnt=%lu|fc=%ld|cmd=%lu",
           (unsigned long)dsme_time.asn, dsme_short_addr,
           g_alloc, dsme_allocated_ext_tx_slots(),
           dsme_queue_count(&dsme_queue), dsme_demand_slots,
           dsme_alloc_fail_count,
           (unsigned long)dsme_stats.tx_ok,
           (unsigned long)dsme_stats.tx_noack,
           (unsigned long)(dsme_stats.dropped_queue_full +
                           dsme_stats.dropped_deadline),
           (unsigned long)dsme_stats.dropped_no_gts,
           (unsigned long)dsme_stats.app_generated,
           (unsigned long)dsme_stats.rx_data,
           (unsigned long)(dsme_stats.delay_sum_us / 1000u),
           (unsigned long)dsme_stats.delay_count,
           (long)(dsme_pred.last_forecast >> 10),
           (unsigned long)dsme_stats.cmd_tx);

#if DSME_MODE == DSME_MODE_PREDICTIVE
  free_cfp = dsme_sab_free_count(&dsme_sab);
  /* The estimator must advance every superframe, including those in which no
   * action is possible, or its sampling interval becomes irregular. */
  action = dsme_predictor_update(&dsme_pred, g_alloc, free_cfp,
                                 dsme_queue_count(&dsme_queue));
#if DSME_DEMAND_SRC == DSME_DEMAND_FORECAST
  dsme_demand_slots = dsme_pred.last_g_req;
#endif
  DSME_LOG_DBG("PRED|asn=%lu|n=%u|fc=%ld|greq=%u|galloc=%u|free=%u|onset=%u|act=%d",
               (unsigned long)dsme_time.asn, dsme_short_addr,
               (long)(dsme_pred.last_forecast >> 10), dsme_pred.last_g_req,
               g_alloc, free_cfp, dsme_pred.last_onset, (int)action);
#endif

  /* Time out whichever handshakes have outrun macMaxFrameTotalWaitTime. */
  {
    uint8_t k;
    for(k = 0; k < 2; k++) {
      if(dsme_txn[k].state == DSME_TXN_WAIT_REPLY &&
         dsme_sf_of(dsme_time.asn) > dsme_txn[k].deadline_sf) {
        transaction_failed(k, "timeout");
      }
    }
  }

  need = dsme_demand_slots > g_alloc ? (uint8_t)(dsme_demand_slots - g_alloc) : 0;

#if DSME_MODE == DSME_MODE_PREDICTIVE
  if(action == DSME_PRED_EXTEND_CFP && dsme_txn[1].state == DSME_TXN_IDLE) {
    uint8_t d = dsme_predictor_deficit(&dsme_pred);
    start_extension(d > need ? d : (need > 0 ? need : 1), DSME_TRIG_PREDICTIVE);
    return;
  }
  /* Ask for ordinary GTSs ahead of the application's own demand. Note that
   * this only has an effect when the demand comes from somewhere other than
   * the estimator: under DSME_DEMAND_FORECAST, dsme_demand_slots is already
   * the estimator's g_req, so `need` and the deficit are the same number and
   * this branch cannot fire. It matters for DSME_DEMAND_ORACLE, where the
   * forecast may run ahead of the rate the application has declared. */
  if(need == 0 && action == DSME_PRED_EARLY_GTS) {
    need = dsme_predictor_deficit(&dsme_pred);
  }
#endif

  if(need == 0) {
    return;
  }

#if DSME_MODE == DSME_MODE_TACFPEXT || DSME_MODE == DSME_MODE_PREDICTIVE
  /* The published reactive trigger. Under P-TaCFPext it remains as a
   * backstop for bursts the estimator did not see coming. */
  if(dsme_alloc_fail_count >= DSME_FAIL_THRESHOLD &&
     dsme_txn[1].state == DSME_TXN_IDLE) {
    start_extension(need, DSME_TRIG_REACTIVE);
    return;
  }
#endif

  if(dsme_txn[0].state != DSME_TXN_IDLE) {
    return;
  }

  {
    /* Never ask for more slots than our own SAB says could be free in the
     * superframe being probed: the responder would refuse, and the refusal
     * would carry no information. Asking for at least one keeps a genuinely
     * full superframe visible to the reactive counter. */
    uint8_t own_free = 0;
    uint8_t s;
    uint8_t cnt = dsme_cfp_slot_count(next_pref_sd);
    for(s = 0; s < cnt; s++) {
      if(!dsme_sab_test(&dsme_sab, next_pref_sd, s)) {
        own_free++;
      }
    }
    if(own_free > need) {
      own_free = need;
    }
    if(own_free == 0) {
      own_free = 1;
    }
    if(own_free > DSME_MAX_SLOTS_PER_REQUEST) {
      own_free = DSME_MAX_SLOTS_PER_REQUEST;
    }
    start_request(dsme_tx_peer, own_free, 0, next_pref_sd);
  }
}
/*---------------------------------------------------------------------------*/
