/*
 * dsme-predictor.h -- Predictive traffic estimator for P-TaCFPext.
 *
 * ---------------------------------------------------------------------------
 * What this replaces
 * ---------------------------------------------------------------------------
 * TaCFPext (Reference C) starts converting a CAP into an extCFP only after a
 * node has failed to allocate a GTS 2^(MO-SO)/k times in a row. The mechanism
 * is therefore purely reactive: by the time it engages, several superframes
 * of data have already been delayed or dropped, and the extGTS negotiation
 * that follows costs several more. In bursty traffic that recovery window
 * recurs at every burst onset.
 *
 * ---------------------------------------------------------------------------
 * The replacement
 * ---------------------------------------------------------------------------
 * Two observations drive the design.
 *
 * 1. A node can *forecast* its own demand. Arrivals are sampled once per
 *    superframe and smoothed with Holt's linear-trend method -- two coupled
 *    EWMAs, one for the level L and one for the slope T -- so the estimate
 *    extrapolates h superframes ahead as L + h*T rather than merely tracking
 *    the past. A plain single EWMA cannot do this: it lags a step change by
 *    roughly 1/alpha samples, which is the very delay we are trying to
 *    remove. A second, independent fast/slow EWMA pair acts as a burst-onset
 *    detector: when the fast average pulls away from the slow one, the ramp
 *    has started, and that fires within one or two samples -- before the
 *    trend term has had time to build.
 *
 * 2. A node can *predict the failure itself*, with no extra signalling. The
 *    SAB a node already maintains by overhearing its one-hop neighbours
 *    records exactly which GTS positions in the multi-superframe are taken.
 *    If the forecast demand exceeds what is still free there, the next GTS
 *    Request is already known to be answered "disapproved -- lack of
 *    availability". TaCFPext holds this information and discards it, choosing
 *    instead to learn the same fact three handshakes later. P-TaCFPext reads
 *    it directly and goes straight to changeable-CAP selection.
 *
 * The published reactive trigger is retained underneath as a backstop, so
 * P-TaCFPext degrades into TaCFPext rather than below it when a burst is
 * genuinely unpredictable.
 *
 * ---------------------------------------------------------------------------
 * Stability
 * ---------------------------------------------------------------------------
 * Acting early costs nothing if the prediction is right and costs a reserved
 * extGTS if it is wrong, so the trigger is damped three ways: a deficit must
 * persist for DSME_PRED_PERSIST superframes (bypassed by the onset detector,
 * which is the deliberately fast path), a cooldown follows every conversion,
 * and extGTSs that go unused expire after macDsmeGtsExpirationTime/2 idle
 * multi-superframes as in Reference C. The idle extGTS slot-instances are
 * counted so that the cost of a mispredicted burst is reported rather than
 * hidden.
 *
 * All arithmetic is Q16.16 fixed point: no FPU is assumed.
 */

#ifndef DSME_PREDICTOR_H_
#define DSME_PREDICTOR_H_

#include "dsme-types.h"

typedef enum {
  DSME_PRED_NONE = 0,
  /* Demand is rising but the CFP still has room: request ordinary GTSs now
   * rather than when the queue has already built up. */
  DSME_PRED_EARLY_GTS,
  /* Forecast demand exceeds the free CFP capacity the SAB reports: skip
   * straight to changeable-CAP selection and extGTS allocation. */
  DSME_PRED_EXTEND_CFP,
} dsme_pred_action_t;

void dsme_predictor_init(dsme_predictor_t *p);

/** Record \p n application arrivals in the superframe now in progress. */
void dsme_predictor_on_arrival(dsme_predictor_t *p, uint8_t n);

/**
 * Advance the estimator by one superframe and decide whether to act.
 *
 * \param g_alloc  TX slots (GTS + extGTS) already allocated to this node
 * \param free_cfp GTS positions in the multi-superframe still free in the SAB
 * \param backlog  packets currently queued
 */
dsme_pred_action_t dsme_predictor_update(dsme_predictor_t *p,
                                         uint8_t g_alloc,
                                         uint8_t free_cfp,
                                         uint16_t backlog);

/** Called after a CAP-to-extCFP conversion, to start the cooldown. */
void dsme_predictor_note_conversion(dsme_predictor_t *p);

/** Slots the estimator believes are still missing; 0 when demand is met. */
uint8_t dsme_predictor_deficit(const dsme_predictor_t *p);

#endif /* DSME_PREDICTOR_H_ */
