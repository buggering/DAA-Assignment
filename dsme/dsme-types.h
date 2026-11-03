/*
 * dsme-types.h -- Data structures of the DSME MAC and of the TaCFPext /
 *                 P-TaCFPext CFP-extension protocols.
 */

#ifndef DSME_TYPES_H_
#define DSME_TYPES_H_

#include "contiki.h"
#include "dsme-conf.h"
#include "dsme-const.h"
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Absolute network time.                                                     */
/*                                                                            */
/* A single absolute slot number carries the whole multi-superframe position: */
/*   slot     = asn % 16                                                      */
/*   sd_index = (asn / 16) % DSME_SF_PER_MS                                   */
/*   ms_index = (asn / 16 / DSME_SF_PER_MS) % DSME_MS_PER_BI                  */
/*---------------------------------------------------------------------------*/
typedef struct {
  uint32_t asn;        /* absolute slot number since PAN start */
  uint8_t pan_bsn;     /* macPANCoordinatorBsn, from the latest EB */
} dsme_time_t;

static inline uint8_t dsme_slot_of(uint32_t asn)
{
  return (uint8_t)(asn % DSME_SLOTS_PER_SF);
}
static inline uint32_t dsme_sf_of(uint32_t asn)
{
  return asn / DSME_SLOTS_PER_SF;
}
static inline uint8_t dsme_sd_index_of(uint32_t asn)
{
  return (uint8_t)(dsme_sf_of(asn) % DSME_SF_PER_MS);
}
static inline uint32_t dsme_ms_of(uint32_t asn)
{
  return dsme_sf_of(asn) / DSME_SF_PER_MS;
}
static inline uint8_t dsme_ms_index_of(uint32_t asn)
{
  return (uint8_t)(dsme_ms_of(asn) % DSME_MS_PER_BI);
}

/*---------------------------------------------------------------------------*/
/* DSME Allocation Counter Table (ACT) entry.                                 */
/*                                                                            */
/* The same structure backs the ACT (GTSs inside the CFP) and the taACT       */
/* (extGTSs inside an extCFP); the DSME_ACT_IS_EXT flag distinguishes them,   */
/* exactly as Reference C keeps taACT field-compatible with the legacy ACT.   */
/*---------------------------------------------------------------------------*/
#define DSME_ACT_IN_USE             0x01
#define DSME_ACT_IS_EXT             0x02

typedef struct {
  uint16_t peer;           /* short address at the other end of the link */
  uint8_t sd_index;        /* superframe ID inside the multi-superframe  */
  uint8_t slot_id;         /* slot ID inside the CFP (or extCFP)         */
  uint8_t channel_off;     /* channel offset of the receiver node        */
  uint8_t direction;       /* DSME_DIRECTION_TX / DSME_DIRECTION_RX      */
  uint8_t idle_counter;    /* idle multi-superframes since last use      */
  uint8_t flags;
} dsme_act_entry_t;

/*---------------------------------------------------------------------------*/
/* Slot Allocation Bitmap (SAB) and traffic-adaptive SAB (taSAB).             */
/*                                                                            */
/* SAB, Reference C Fig. 4: one sub-block per superframe in the               */
/* multi-superframe; one bit per CFP slot, set when that GTS is in use by     */
/* this node or by one of its one-hop neighbours.                             */
/*                                                                            */
/* taSAB, Reference C Fig. 7: one sub-block per superframe, each a            */
/* two-dimensional [slot][channel offset] bitmap over the extCFP. The second  */
/* dimension is what lets a node tell an extGTS of a one-hop neighbour (which */
/* turns the CAP into a listen-only period) from one of its own.              */
/*---------------------------------------------------------------------------*/
typedef struct {
  uint16_t sub[DSME_SF_PER_MS];              /* bit i set => CFP slot i used */
} dsme_sab_t;

typedef struct {
  uint16_t sub[DSME_SF_PER_MS][DSME_EXTCFP_SLOTS];  /* bit c => channel c used */
} dsme_tasab_t;

/* Changeable CAP bitmap, Reference C Fig. 8: CAP / LOP / extCFP per superframe. */
typedef struct {
  uint8_t state[DSME_SF_PER_MS];             /* DSME_CCB_* */
} dsme_ccb_t;

/*---------------------------------------------------------------------------*/
/* Outgoing data packet descriptor.                                           */
/*                                                                            */
/* The payload bytes themselves are not stored: this is a MAC-layer study and */
/* only the frame length influences the channel. The length is replayed on    */
/* transmission so that the air time Cooja computes stays correct.            */
/*---------------------------------------------------------------------------*/
typedef struct {
  uint16_t dst;
  uint16_t seqno;
  uint32_t enqueue_us;     /* for end-to-end delay */
  uint32_t deadline_ms;    /* multi-superframe index after which it is stale */
  uint8_t len;             /* total MAC frame length */
  uint8_t tx_count;
} dsme_packet_t;

typedef struct {
  dsme_packet_t items[DSME_QUEUE_LEN];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
} dsme_queue_t;

/*---------------------------------------------------------------------------*/
/* CAP command frame awaiting a slotted CSMA-CA transmission.                 */
/*---------------------------------------------------------------------------*/
#define DSME_MAX_CMD_LEN            96

typedef struct {
  uint16_t dst;
  uint8_t len;
  uint8_t retries;
  uint8_t buf[DSME_MAX_CMD_LEN];
} dsme_cmd_t;

typedef struct {
  dsme_cmd_t items[DSME_CMD_QUEUE_LEN];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
} dsme_cmd_queue_t;

/*---------------------------------------------------------------------------*/
/* State of the in-flight GTS / extGTS three-way handshake.                   */
/*---------------------------------------------------------------------------*/
enum {
  DSME_TXN_IDLE = 0,
  DSME_TXN_REQUEST_QUEUED,   /* REQUEST is in the CAP command queue   */
  DSME_TXN_WAIT_REPLY,       /* REQUEST sent, waiting for the REPLY   */
};

typedef struct {
  uint8_t state;
  uint8_t is_ext;            /* 0 = GTS in CFP, 1 = extGTS in extCFP  */
  uint16_t peer;
  uint8_t num_slots;
  uint8_t pref_sd;
  uint8_t pref_slot;
  uint32_t started_us;
  uint32_t deadline_sf;      /* absolute superframe at which we give up */
} dsme_txn_t;

/*---------------------------------------------------------------------------*/
/* Predictive traffic estimator (P-TaCFPext, this work).                      */
/*                                                                            */
/* Holt's linear-trend method is two coupled EWMAs: `level` tracks the mean   */
/* arrival rate and `trend` its slope, so the pair extrapolates h superframes */
/* ahead instead of merely tracking the past. `fast` and `slow` are a second, */
/* independent EWMA pair whose divergence flags a burst onset within one or   */
/* two samples -- faster than the trend term can respond.                     */
/*                                                                            */
/* All state is Q16.16 fixed point so the estimator runs unchanged on         */
/* FPU-less targets.                                                          */
/*---------------------------------------------------------------------------*/
#define DSME_Q16_ONE                65536
#define DSME_Q16_FROM_INT(i)        ((int32_t)(i) << 16)
#define DSME_Q16_TO_INT(q)          ((int32_t)(q) >> 16)
/* Round-up conversion, used when turning a predicted packet count into a
 * number of slots to reserve. */
#define DSME_Q16_CEIL_INT(q)        (((int32_t)(q) + DSME_Q16_ONE - 1) >> 16)
/* a*b for Q16.16 operands; the operands here are small so int64 is cheap
 * insurance against overflow rather than a necessity. */
#define DSME_Q16_MUL(a, b)          ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> 16))

typedef struct {
  int32_t level;             /* Holt level  L(n),  Q16.16 packets/superframe */
  int32_t trend;             /* Holt trend  T(n),  Q16.16                    */
  int32_t fast;              /* fast EWMA,         Q16.16                    */
  int32_t slow;              /* slow EWMA,         Q16.16                    */
  uint16_t arrivals;         /* arrivals observed in the current superframe  */
  uint8_t warmup;            /* superframes left before the estimator is hot */
  uint8_t persist;           /* consecutive superframes with a deficit       */
  uint8_t cooldown;          /* superframes left before another conversion   */
  /* Last decision, retained for logging and offline analysis. */
  int32_t last_forecast;     /* Q16.16 packets/superframe                    */
  uint8_t last_g_req;
  uint8_t last_g_alloc;
  uint8_t last_free_cfp;
  uint8_t last_onset;
} dsme_predictor_t;

/*---------------------------------------------------------------------------*/
/* Per-node counters, dumped at the end of a run.                             */
/*---------------------------------------------------------------------------*/
typedef struct {
  uint32_t app_generated;
  uint32_t enqueued;
  uint32_t dropped_queue_full;
  uint32_t dropped_no_gts;
  uint32_t dropped_deadline;
  uint32_t cmd_queue_full;
  uint32_t tx_attempts;
  uint32_t tx_ok;
  uint32_t tx_noack;
  uint32_t rx_data;
  uint32_t gts_requests_sent;
  uint32_t gts_alloc_ok;
  uint32_t gts_alloc_fail;
  uint32_t extgts_requests_sent;
  uint32_t extgts_alloc_ok;
  uint32_t extgts_alloc_fail;
  uint32_t trig_reactive;
  uint32_t trig_predictive;
  uint32_t cap_converted;
  uint32_t extgts_slot_used;      /* extGTS slot-instances that carried data */
  uint32_t extgts_slot_idle;      /* extGTS slot-instances left unused       */
  uint32_t cmd_tx;                /* CAP command frames put on air           */
  uint32_t cmd_cca_fail;          /* CSMA-CA gave up: channel never idle     */
  uint64_t delay_sum_us;          /* sum of per-packet queueing+access delay */
  uint32_t delay_count;
  uint32_t delay_max_us;
} dsme_stats_t;

#endif /* DSME_TYPES_H_ */
