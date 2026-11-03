/*
 * test_predictor.c -- Host-compiled unit test for the P-TaCFPext estimator.
 *
 * Exercises dsme-predictor.c directly on synthetic arrival traces, so the
 * forecast and the trigger logic can be checked without a radio, a slot
 * machine, or a simulator. Build and run with:  make -C tests
 */

#include "dsme-predictor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The estimator is the only thing under test; these satisfy the linker. */
dsme_time_t dsme_time;
uint16_t dsme_short_addr = 1;

static int failures;

#define CHECK(cond, ...)                                                   \
  do {                                                                     \
    if(!(cond)) {                                                          \
      printf("  FAIL  " __VA_ARGS__);                                      \
      printf("        (%s, line %d)\n", #cond, __LINE__);                  \
      failures++;                                                          \
    }                                                                      \
  } while(0)

static double q16(int32_t v) { return (double)v / 65536.0; }

/*---------------------------------------------------------------------------*/
/* A step from `low` to `high` arrivals per superframe. Returns the number of
 * superframes after the step at which the estimator first asked to extend the
 * CFP, or -1 if it never did. */
static int
run_step(uint8_t low, uint8_t high, uint8_t g_alloc, uint8_t free_cfp,
         int warm_sf, int observe_sf, dsme_predictor_t *out)
{
  dsme_predictor_t p;
  int i, fired = -1;

  dsme_predictor_init(&p);
  for(i = 0; i < warm_sf; i++) {
    dsme_predictor_on_arrival(&p, low);
    dsme_predictor_update(&p, g_alloc, free_cfp, 0);
  }
  for(i = 0; i < observe_sf; i++) {
    dsme_pred_action_t a;
    dsme_predictor_on_arrival(&p, high);
    a = dsme_predictor_update(&p, g_alloc, free_cfp, 0);
    if(a == DSME_PRED_EXTEND_CFP && fired < 0) {
      fired = i;
    }
  }
  if(out) {
    memcpy(out, &p, sizeof(p));
  }
  return fired;
}
/*---------------------------------------------------------------------------*/
static void
test_steady_state_tracks_the_rate(void)
{
  dsme_predictor_t p;
  int i;

  printf("steady state: the level converges on the arrival rate\n");
  dsme_predictor_init(&p);
  for(i = 0; i < 40; i++) {
    dsme_predictor_on_arrival(&p, 5);
    dsme_predictor_update(&p, 28, 0, 0);
  }
  printf("        level=%.3f trend=%.3f (arrivals 5/superframe)\n",
         q16(p.level), q16(p.trend));
  CHECK(q16(p.level) > 4.9 && q16(p.level) < 5.1,
        "level should converge on 5, got %.3f\n", q16(p.level));
  CHECK(q16(p.trend) > -0.05 && q16(p.trend) < 0.05,
        "trend should decay to 0 on a flat input, got %.3f\n", q16(p.trend));
}
/*---------------------------------------------------------------------------*/
static void
test_idle_never_triggers(void)
{
  int fired;

  printf("idle traffic: no spurious conversion\n");
  /* One packet per multi-superframe is 0.25 per superframe; a single
   * allocated GTS covers it. */
  fired = run_step(0, 0, 1, 0, 8, 40, NULL);
  CHECK(fired < 0, "a silent link must never ask to extend the CFP\n");
}
/*---------------------------------------------------------------------------*/
static void
test_burst_fires_within_two_superframes(void)
{
  dsme_predictor_t p;
  int fired;

  printf("burst onset: the trigger fires on the ramp, not after it\n");
  /* Rate steps from 0 to 5 per superframe with one slot allocated and no
   * free CFP left, which is the situation the SAB test is meant to catch. */
  fired = run_step(0, 5, 1, 0, 8, 20, &p);
  printf("        fired at superframe %d after the step, forecast=%.2f, "
         "g_req=%u\n", fired, q16(p.last_forecast), p.last_g_req);
  CHECK(fired >= 0, "the trigger must fire on a burst that exceeds the CFP\n");
  CHECK(fired >= 0 && fired <= 1,
        "must fire within two superframes of the step, fired at %d\n", fired);
}
/*---------------------------------------------------------------------------*/
static void
test_forecast_leads_a_ramp(void)
{
  dsme_predictor_t p;
  int i;

  printf("linear ramp: the forecast leads the measurement\n");
  dsme_predictor_init(&p);
  for(i = 0; i < 4; i++) {
    dsme_predictor_on_arrival(&p, 0);
    dsme_predictor_update(&p, 28, 28, 0);
  }
  for(i = 1; i <= 10; i++) {
    dsme_predictor_on_arrival(&p, (uint8_t)i);
    dsme_predictor_update(&p, 28, 28, 0);
  }
  printf("        last arrivals=10, level=%.2f, trend=%.2f, forecast=%.2f\n",
         q16(p.level), q16(p.trend), q16(p.last_forecast));
  CHECK(q16(p.last_forecast) > 10.0,
        "on a rising ramp the h-step forecast must exceed the latest sample "
        "(%.2f vs 10)\n", q16(p.last_forecast));
  CHECK(q16(p.trend) > 0.5, "the trend term must be positive on a ramp, "
        "got %.2f\n", q16(p.trend));
}
/*---------------------------------------------------------------------------*/
static void
test_free_cfp_suppresses_extension(void)
{
  int fired_full, fired_empty;

  printf("SAB capacity test: an empty CFP is used before a CAP is taken\n");
  fired_empty = run_step(0, 5, 1, 28, 8, 20, NULL);   /* whole CFP free */
  fired_full = run_step(0, 5, 1, 0, 8, 20, NULL);     /* CFP exhausted  */
  printf("        CFP free: fired=%d   CFP full: fired=%d\n",
         fired_empty, fired_full);
  CHECK(fired_full >= 0, "an exhausted CFP must produce an extension\n");
  CHECK(fired_empty < 0,
        "with the whole CFP free the node must ask for ordinary GTSs "
        "instead of converting a CAP\n");
}
/*---------------------------------------------------------------------------*/
static void
test_cooldown_limits_conversions(void)
{
  dsme_predictor_t p;
  int i, fires = 0;

  printf("cooldown: conversions cannot repeat every superframe\n");
  dsme_predictor_init(&p);
  for(i = 0; i < 8; i++) {
    dsme_predictor_on_arrival(&p, 0);
    dsme_predictor_update(&p, 1, 0, 0);
  }
  for(i = 0; i < 24; i++) {
    dsme_predictor_on_arrival(&p, 5);
    if(dsme_predictor_update(&p, 1, 0, 0) == DSME_PRED_EXTEND_CFP) {
      fires++;
      dsme_predictor_note_conversion(&p);
    }
  }
  printf("        %d conversions in 24 superframes "
         "(cooldown is %d superframes)\n", fires, DSME_PRED_COOLDOWN);
  CHECK(fires <= 24 / DSME_PRED_COOLDOWN + 1,
        "the cooldown must bound the conversion rate, saw %d\n", fires);
  CHECK(fires >= 1, "sustained unmet demand must still convert\n");
}
/*---------------------------------------------------------------------------*/
static void
test_fixed_point_has_no_overflow(void)
{
  dsme_predictor_t p;
  int i;

  printf("fixed point: saturating input does not overflow Q16.16\n");
  dsme_predictor_init(&p);
  for(i = 0; i < 200; i++) {
    dsme_predictor_on_arrival(&p, 255);
    dsme_predictor_update(&p, 0, 0, 255);
  }
  printf("        level=%.1f trend=%.1f g_req=%u\n",
         q16(p.level), q16(p.trend), p.last_g_req);
  CHECK(p.level > 0, "level must stay positive, got %.1f\n", q16(p.level));
  CHECK(p.last_g_req <= DSME_SAB_TOTAL_SLOTS,
        "g_req must stay inside the multi-superframe, got %u\n", p.last_g_req);
}
/*---------------------------------------------------------------------------*/
int
main(void)
{
  printf("P-TaCFPext estimator tests "
         "(alpha=%.2f beta=%.2f fast=%.2f slow=%.2f h=%d persist=%d)\n\n",
         q16(DSME_PRED_ALPHA_Q16), q16(DSME_PRED_BETA_Q16),
         q16(DSME_PRED_ALPHA_FAST_Q16), q16(DSME_PRED_ALPHA_SLOW_Q16),
         DSME_PRED_HORIZON_SF, DSME_PRED_PERSIST);

  test_steady_state_tracks_the_rate();
  test_idle_never_triggers();
  test_burst_fires_within_two_superframes();
  test_forecast_leads_a_ramp();
  test_free_cfp_suppresses_extension();
  test_cooldown_limits_conversions();
  test_fixed_point_has_no_overflow();

  printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
  return failures ? 1 : 0;
}
