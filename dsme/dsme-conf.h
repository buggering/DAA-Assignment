/*
 * dsme-conf.h -- Compile-time configuration for the Contiki-NG DSME MAC
 *                and the TaCFPext / P-TaCFPext CFP-extension protocols.
 *
 * Reference A: IEEE Std 802.15.4-2015, DSME MAC behaviour mode.
 * Reference B: D. De Guglielmo, S. Brienza, G. Anastasi, "IEEE 802.15.4e:
 *              A survey", Computer Communications 88 (2016) 1-24, Section 5.
 * Reference C: S.-W. Lee, J.-H. Kwon, X. Zhang, E.-J. Kim, "Traffic-Adaptive
 *              CFP Extension for IEEE 802.15.4 DSME MAC in Industrial Wireless
 *              Sensor Networks", IEEE Access, vol. 9, pp. 94454-94469, 2021.
 *
 * All values below may be overridden from the application project-conf.h.
 */

#ifndef DSME_CONF_H_
#define DSME_CONF_H_

/*---------------------------------------------------------------------------*/
/* Operating mode                                                            */
/*---------------------------------------------------------------------------*/
/* Legacy DSME, CAP reduction disabled (Reference C baseline "Legacy DSME"). */
#define DSME_MODE_LEGACY            0
/* Legacy DSME, CAP reduction enabled (Reference C baseline "DSME w/ CAP red"). */
#define DSME_MODE_CAP_REDUCTION     1
/* TaCFPext as published: reactive trigger after N consecutive GTS failures.  */
#define DSME_MODE_TACFPEXT          2
/* P-TaCFPext (this work): predictive EWMA trigger + reactive backstop.       */
#define DSME_MODE_PREDICTIVE        3

#ifdef DSME_CONF_MODE
#define DSME_MODE DSME_CONF_MODE
#else
#define DSME_MODE DSME_MODE_TACFPEXT
#endif

/*---------------------------------------------------------------------------*/
/* Multi-superframe structure orders.                                         */
/* Reference C, Table 1: SO = 3, MO = 5, BO = 6.                              */
/* Constraint: 0 <= SO <= MO <= BO <= 14.                                     */
/*---------------------------------------------------------------------------*/
#ifdef DSME_CONF_SO
#define DSME_SO DSME_CONF_SO
#else
#define DSME_SO 3
#endif

#ifdef DSME_CONF_MO
#define DSME_MO DSME_CONF_MO
#else
#define DSME_MO 5
#endif

#ifdef DSME_CONF_BO
#define DSME_BO DSME_CONF_BO
#else
#define DSME_BO 6
#endif

/*---------------------------------------------------------------------------*/
/* PHY / channel set                                                          */
/*---------------------------------------------------------------------------*/
/* Number of physical channels in the hopping sequence (Reference C: 16).     */
#define DSME_NUM_CHANNELS           16
#define DSME_CHANNEL_MIN            11
/* The control channel carries beacons and all CAP command traffic. It is
 * excluded from the extCFP hopping sequence (Reference C, Eq. 3). */
#define DSME_CONTROL_CHANNEL        26

/*---------------------------------------------------------------------------*/
/* CSMA-CA parameters in the CAP (IEEE 802.15.4-2015 slotted CSMA-CA)         */
/*---------------------------------------------------------------------------*/
#define DSME_MAC_MIN_BE             3
#define DSME_MAC_MAX_BE             5
#define DSME_MAC_MAX_CSMA_BACKOFFS  4
#define DSME_MAC_MAX_FRAME_RETRIES  3

/*---------------------------------------------------------------------------*/
/* GTS management                                                             */
/*---------------------------------------------------------------------------*/
/* macDsmeGtsExpirationTime: idle multi-superframes before a GTS expires.     */
#define DSME_GTS_EXPIRATION_TIME    7
/* Reference C, Section III-C: extGTS expires at half that, so that CFP space
 * is reclaimed before the GTSs when the load drops.                          */
#define DSME_EXTGTS_EXPIRATION_TIME (DSME_GTS_EXPIRATION_TIME / 2)

/* Maximum number of ACT / taACT entries held per node. */
#ifdef DSME_CONF_MAX_ACT_ENTRIES
#define DSME_MAX_ACT_ENTRIES DSME_CONF_MAX_ACT_ENTRIES
#else
#define DSME_MAX_ACT_ENTRIES        64
#endif

/* Maximum number of GTSs requested in a single handshake. */
#define DSME_MAX_SLOTS_PER_REQUEST  7

/*
 * macMaxFrameTotalWaitTime for a REPLY, in superframes. It must span a whole
 * multi-superframe: once TaCFPext has converted CAPs, the CAP of superframe 0
 * can be the only contention period a pair still shares, and it comes round
 * once per multi-superframe.
 */
#define DSME_REPLY_WAIT_SUPERFRAMES (DSME_SF_PER_MS + 1)

/*---------------------------------------------------------------------------*/
/* Source of the node's GTS demand                                            */
/*                                                                            */
/* How many GTSs a node asks for is a separate question from when it decides  */
/* to extend the CFP, and the two must be varied independently or the         */
/* evaluation cannot attribute a gain to either.                              */
/*                                                                            */
/*  ORACLE   the application declares its own packet rate the instant it      */
/*           changes. This is the assumption made in Reference C ("the sender */
/*           transmits a certain number of data packets per multi-superframe, */
/*           for which the dedicated GTSs are allocated"), and it is the       */
/*           strongest possible baseline: the node never has to learn its own  */
/*           load, only that the CFP is full.                                  */
/*  LASTVAL  demand equals the arrivals counted in the previous               */
/*           multi-superframe. This is what a real implementation without a   */
/*           traffic model would do, and it lags a burst by one               */
/*           multi-superframe.                                                 */
/*  FORECAST demand comes from the Holt forecast in dsme-predictor.c.          */
/*---------------------------------------------------------------------------*/
#define DSME_DEMAND_ORACLE          0
#define DSME_DEMAND_LASTVAL         1
#define DSME_DEMAND_FORECAST        2

#ifdef DSME_CONF_DEMAND_SRC
#define DSME_DEMAND_SRC DSME_CONF_DEMAND_SRC
#else
#if DSME_MODE == DSME_MODE_PREDICTIVE
#define DSME_DEMAND_SRC             DSME_DEMAND_FORECAST
#else
#define DSME_DEMAND_SRC             DSME_DEMAND_ORACLE
#endif
#endif

/*---------------------------------------------------------------------------*/
/* TaCFPext (Reference C)                                                     */
/*---------------------------------------------------------------------------*/
/* Reactive trigger threshold. Reference C initiates TaCFPext after
 * 2^(MO-SO)/k consecutive GTS allocation failures, where k is the number of
 * SAB sub-blocks carried in one DSME GTS Request; Fig. 6 of Reference C shows
 * three consecutive failures. Configurable so that the threshold itself can
 * be swept. */
#ifdef DSME_CONF_FAIL_THRESHOLD
#define DSME_FAIL_THRESHOLD DSME_CONF_FAIL_THRESHOLD
#else
#define DSME_FAIL_THRESHOLD         3
#endif

/*---------------------------------------------------------------------------*/
/* P-TaCFPext predictive trigger (this work)                                  */
/*---------------------------------------------------------------------------*/
/* Holt double-exponential smoothing coefficients, Q16.16 fixed point.
 * alpha weights the level, beta the trend. */
#ifdef DSME_CONF_PRED_ALPHA_Q16
#define DSME_PRED_ALPHA_Q16 DSME_CONF_PRED_ALPHA_Q16
#else
#define DSME_PRED_ALPHA_Q16         26214   /* 0.40 */
#endif

#ifdef DSME_CONF_PRED_BETA_Q16
#define DSME_PRED_BETA_Q16 DSME_CONF_PRED_BETA_Q16
#else
#define DSME_PRED_BETA_Q16          19661   /* 0.30 */
#endif

/* Fast / slow EWMA pair used as the burst-onset detector. */
#ifdef DSME_CONF_PRED_ALPHA_FAST_Q16
#define DSME_PRED_ALPHA_FAST_Q16 DSME_CONF_PRED_ALPHA_FAST_Q16
#else
#define DSME_PRED_ALPHA_FAST_Q16    39322   /* 0.60 */
#endif

#ifdef DSME_CONF_PRED_ALPHA_SLOW_Q16
#define DSME_PRED_ALPHA_SLOW_Q16 DSME_CONF_PRED_ALPHA_SLOW_Q16
#else
#define DSME_PRED_ALPHA_SLOW_Q16    6554    /* 0.10 */
#endif

/* Onset is declared when fast > (1 + gamma) * slow + delta. */
#ifdef DSME_CONF_PRED_GAMMA_Q16
#define DSME_PRED_GAMMA_Q16 DSME_CONF_PRED_GAMMA_Q16
#else
#define DSME_PRED_GAMMA_Q16         32768   /* 0.50 */
#endif
/* delta, in packets per superframe (Q16.16); guards against noise at low rate */
#define DSME_PRED_DELTA_Q16         32768   /* 0.50 packet/superframe */

/* Forecast horizon h, in superframes. Set to the measured cost of an extGTS
 * negotiation so that the reservation completes as the burst ramps up. */
#ifdef DSME_CONF_PRED_HORIZON_SF
#define DSME_PRED_HORIZON_SF DSME_CONF_PRED_HORIZON_SF
#else
#define DSME_PRED_HORIZON_SF        4
#endif

/* Backlog drain horizon, in multi-superframes. */
#define DSME_PRED_DRAIN_MS          2

/* Hysteresis: consecutive superframes the deficit must persist before the
 * slow path fires. The onset detector bypasses this. */
#ifdef DSME_CONF_PRED_PERSIST
#define DSME_PRED_PERSIST DSME_CONF_PRED_PERSIST
#else
#define DSME_PRED_PERSIST           2
#endif

/* Cooldown after a conversion, in superframes, to stop CAP/extCFP thrash. */
#ifdef DSME_CONF_PRED_COOLDOWN
#define DSME_PRED_COOLDOWN DSME_CONF_PRED_COOLDOWN
#else
#define DSME_PRED_COOLDOWN          8
#endif

/* Superframes of samples discarded while the estimator initialises. */
#define DSME_PRED_WARMUP_SF         2

/* Ablation switches. All default on in DSME_MODE_PREDICTIVE. */
#ifndef DSME_PRED_USE_TREND
#define DSME_PRED_USE_TREND         1   /* Holt trend term (level + h*trend)  */
#endif
#ifndef DSME_PRED_USE_ONSET
#define DSME_PRED_USE_ONSET         1   /* fast/slow divergence detector      */
#endif
#ifndef DSME_PRED_USE_SAB_CAPACITY
#define DSME_PRED_USE_SAB_CAPACITY  1   /* local free-CFP estimate from SAB   */
#endif
#ifndef DSME_PRED_USE_EARLY_GTS
#define DSME_PRED_USE_EARLY_GTS     1   /* proactive plain-GTS request too    */
#endif

/*---------------------------------------------------------------------------*/
/* Data frames                                                                */
/*---------------------------------------------------------------------------*/
/* Total MAC frame length of an application data frame, in octets. Reference C
 * Table 1 lists a 127-octet payload plus a 10-octet MAC header, which exceeds
 * the 127-octet PSDU the 802.15.4 PHY allows; a full-size standard-compliant
 * frame is used instead, also staying inside the 125-octet Cooja radio
 * buffer. Air time is 32 us per octet at 250 kbit/s. */
#ifdef DSME_CONF_DATA_FRAME_LEN
#define DSME_DATA_FRAME_LEN DSME_CONF_DATA_FRAME_LEN
#else
#define DSME_DATA_FRAME_LEN         120
#endif

/*---------------------------------------------------------------------------*/
/* Queueing                                                                   */
/*                                                                            */
/* Reference C, Section IV-A: "If all required GTSs cannot be allocated due to */
/* high contention in the CAP or insufficient space in CFPs, the node drops    */
/* the data packets that could not be transmitted in the current              */
/* multi-superframe." The queue therefore carries a deadline rather than       */
/* growing without bound -- which is also the right model for an industrial    */
/* WSN, where a late sample is worth no more than a lost one.                  */
/*---------------------------------------------------------------------------*/
#ifdef DSME_CONF_PACKET_DEADLINE_MS
#define DSME_PACKET_DEADLINE_MS DSME_CONF_PACKET_DEADLINE_MS
#else
#define DSME_PACKET_DEADLINE_MS     1   /* multi-superframes */
#endif

#ifdef DSME_CONF_QUEUE_LEN
#define DSME_QUEUE_LEN DSME_CONF_QUEUE_LEN
#else
#define DSME_QUEUE_LEN              64
#endif

/* CAP command queue depth. */
#define DSME_CMD_QUEUE_LEN          8

/*---------------------------------------------------------------------------*/
/* Logging                                                                    */
/*---------------------------------------------------------------------------*/
#ifdef DSME_CONF_LOG_LEVEL
#define DSME_LOG_LEVEL DSME_CONF_LOG_LEVEL
#else
#define DSME_LOG_LEVEL              1
#endif

#endif /* DSME_CONF_H_ */
