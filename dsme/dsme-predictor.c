#include "dsme-predictor.h"
#include "dsme-log.h"

#define Q16_ONE     DSME_Q16_ONE
#define ALPHA       ((int32_t)DSME_PRED_ALPHA_Q16)
#define BETA        ((int32_t)DSME_PRED_BETA_Q16)
#define ALPHA_FAST  ((int32_t)DSME_PRED_ALPHA_FAST_Q16)
#define ALPHA_SLOW  ((int32_t)DSME_PRED_ALPHA_SLOW_Q16)
#define GAMMA       ((int32_t)DSME_PRED_GAMMA_Q16)
#define DELTA       ((int32_t)DSME_PRED_DELTA_Q16)

/*---------------------------------------------------------------------------*/
void
dsme_predictor_init(dsme_predictor_t *p)
{
  p->level = 0;
  p->trend = 0;
  p->fast = 0;
  p->slow = 0;
  p->arrivals = 0;
  p->warmup = DSME_PRED_WARMUP_SF;
  p->persist = 0;
  p->cooldown = 0;
  p->last_forecast = 0;
  p->last_g_req = 0;
  p->last_g_alloc = 0;
  p->last_free_cfp = 0;
  p->last_onset = 0;
}
/*---------------------------------------------------------------------------*/
void
dsme_predictor_on_arrival(dsme_predictor_t *p, uint8_t n)
{
  if((uint32_t)p->arrivals + n > 0xFFFF) {
    p->arrivals = 0xFFFF;
  } else {
    p->arrivals = (uint16_t)(p->arrivals + n);
  }
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_predictor_deficit(const dsme_predictor_t *p)
{
  return p->last_g_req > p->last_g_alloc
           ? (uint8_t)(p->last_g_req - p->last_g_alloc)
           : 0;
}
/*---------------------------------------------------------------------------*/
void
dsme_predictor_note_conversion(dsme_predictor_t *p)
{
  p->cooldown = DSME_PRED_COOLDOWN;
  p->persist = 0;
}
/*---------------------------------------------------------------------------*/
dsme_pred_action_t
dsme_predictor_update(dsme_predictor_t *p, uint8_t g_alloc, uint8_t free_cfp,
                      uint16_t backlog)
{
  int32_t a_q, prev_level, forecast, demand;
  int32_t g_req;
  int deficit;
  uint8_t onset = 0;
  uint8_t fire;

  /* Arrivals observed over the superframe that just ended. */
  a_q = DSME_Q16_FROM_INT(p->arrivals);
  p->arrivals = 0;

  if(p->cooldown > 0) {
    p->cooldown--;
  }

  if(p->warmup > 0) {
    /* Seed every state from the first samples rather than from zero, so the
     * estimator does not spend its first superframes climbing out of a hole
     * it invented. */
    p->level = a_q;
    p->trend = 0;
    p->fast = a_q;
    p->slow = a_q;
    p->warmup--;
    p->last_forecast = a_q;
    p->last_g_alloc = g_alloc;
    p->last_free_cfp = free_cfp;
    return DSME_PRED_NONE;
  }

  /* Holt's linear trend:
   *   L(n) = alpha*a(n) + (1-alpha)*(L(n-1) + T(n-1))
   *   T(n) = beta*(L(n) - L(n-1)) + (1-beta)*T(n-1)            */
  prev_level = p->level;
  p->level = DSME_Q16_MUL(ALPHA, a_q) +
             DSME_Q16_MUL(Q16_ONE - ALPHA, prev_level + p->trend);
  p->trend = DSME_Q16_MUL(BETA, p->level - prev_level) +
             DSME_Q16_MUL(Q16_ONE - BETA, p->trend);

  /* Independent fast/slow pair, used only as an onset detector. */
  p->fast = DSME_Q16_MUL(ALPHA_FAST, a_q) +
            DSME_Q16_MUL(Q16_ONE - ALPHA_FAST, p->fast);
  p->slow = DSME_Q16_MUL(ALPHA_SLOW, a_q) +
            DSME_Q16_MUL(Q16_ONE - ALPHA_SLOW, p->slow);

  /* Forecast h superframes ahead. */
#if DSME_PRED_USE_TREND
  forecast = p->level + (int32_t)DSME_PRED_HORIZON_SF * p->trend;
#else
  forecast = p->level;
#endif
  if(forecast < 0) {
    forecast = 0;
  }

#if DSME_PRED_USE_ONSET
  /* fast > (1 + gamma)*slow + delta. The additive delta stops the ratio test
   * from firing on the noise of a near-idle link, where slow is tiny. */
  if(p->fast > DSME_Q16_MUL(Q16_ONE + GAMMA, p->slow) + DELTA) {
    onset = 1;
    /* During a ramp the fast average is ahead of the smoothed level; take
     * whichever is larger so the reservation is sized for the burst. */
    if(p->fast > forecast) {
      forecast = p->fast;
    }
  }
#endif

  p->last_forecast = forecast;
  p->last_onset = onset;

  /* Translate the forecast into a slot demand. One GTS carries one packet per
   * multi-superframe, so the per-superframe rate scales by DSME_SF_PER_MS,
   * plus whatever is needed to drain the current backlog over the drain
   * horizon. */
  demand = (int32_t)DSME_SF_PER_MS * forecast;
  demand += DSME_Q16_FROM_INT(backlog) / DSME_PRED_DRAIN_MS;
  g_req = DSME_Q16_CEIL_INT(demand);
  if(g_req > DSME_SAB_TOTAL_SLOTS) {
    g_req = DSME_SAB_TOTAL_SLOTS;
  }
  if(g_req < 0) {
    g_req = 0;
  }

  p->last_g_req = (uint8_t)g_req;
  p->last_g_alloc = g_alloc;
  p->last_free_cfp = free_cfp;

  deficit = (int)g_req - (int)g_alloc;
  if(deficit <= 0) {
    p->persist = 0;
    return DSME_PRED_NONE;
  }

  if(p->persist < 255) {
    p->persist++;
  }

  /* The onset detector is the fast path and deliberately bypasses the
   * persistence requirement; everything else must hold for PERSIST
   * consecutive superframes. */
  fire = (uint8_t)((p->persist >= DSME_PRED_PERSIST) || onset);
  if(!fire || p->cooldown > 0) {
    return DSME_PRED_NONE;
  }
  p->persist = 0;

#if DSME_PRED_USE_SAB_CAPACITY
  /* The decisive test: the SAB already says there is not enough free CFP for
   * what we are about to need, so a plain GTS Request would be refused. */
  if(deficit > (int)free_cfp) {
    return DSME_PRED_EXTEND_CFP;
  }
#endif

#if DSME_PRED_USE_EARLY_GTS
  return DSME_PRED_EARLY_GTS;
#else
  return DSME_PRED_NONE;
#endif
}
/*---------------------------------------------------------------------------*/
