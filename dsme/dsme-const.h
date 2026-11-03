/*
 * dsme-const.h -- Derived structural and timing constants for the DSME MAC.
 *
 * Everything in this file follows from DSME_SO / DSME_MO / DSME_BO and the
 * O-QPSK 250 kbit/s PHY, so the whole multi-superframe geometry moves
 * consistently if the orders are changed in dsme-conf.h.
 */

#ifndef DSME_CONST_H_
#define DSME_CONST_H_

#include "contiki.h"
#include "sys/rtimer.h"
#include "dsme-conf.h"

/*---------------------------------------------------------------------------*/
/* PHY timing                                                                 */
/*---------------------------------------------------------------------------*/
/* O-QPSK, 250 kbit/s, 4 bits/symbol => 62500 symbol/s => 16 us per symbol.   */
#define DSME_US_PER_SYMBOL          16
#define DSME_US_PER_OCTET           32   /* 8 bits / 250 kbit/s              */

/* IEEE 802.15.4-2015 constants. */
#define DSME_A_BASE_SLOT_DURATION       60    /* symbols */
#define DSME_A_BASE_SUPERFRAME_DURATION 960   /* symbols = 16 * 60          */
#define DSME_A_UNIT_BACKOFF_PERIOD      20    /* symbols                     */
#define DSME_A_TURNAROUND_TIME          12    /* symbols = 192 us            */

/*---------------------------------------------------------------------------*/
/* Multi-superframe geometry                                                  */
/*---------------------------------------------------------------------------*/
#define DSME_SLOTS_PER_SF           16
/* Superframes per multi-superframe: 2^(MO-SO). */
#define DSME_SF_PER_MS              (1 << (DSME_MO - DSME_SO))
/* Multi-superframes per beacon interval: 2^(BO-MO). */
#define DSME_MS_PER_BI              (1 << (DSME_BO - DSME_MO))
/* Superframes per beacon interval: 2^(BO-SO). */
#define DSME_SF_PER_BI              (1 << (DSME_BO - DSME_SO))

/* Durations, in microseconds. */
#define DSME_SLOT_DURATION_US \
  ((uint32_t)DSME_A_BASE_SLOT_DURATION * (1UL << DSME_SO) * DSME_US_PER_SYMBOL)
#define DSME_SF_DURATION_US         (DSME_SLOT_DURATION_US * DSME_SLOTS_PER_SF)
#define DSME_MS_DURATION_US         (DSME_SF_DURATION_US * DSME_SF_PER_MS)
#define DSME_BI_DURATION_US         (DSME_SF_DURATION_US * DSME_SF_PER_BI)

/* Durations, in rtimer ticks. */
#define DSME_SLOT_DURATION          ((rtimer_clock_t)US_TO_RTIMERTICKS(DSME_SLOT_DURATION_US))

/*---------------------------------------------------------------------------*/
/* Slot layout inside one superframe                                          */
/*                                                                            */
/*   slot 0        : beacon (EB) slot                                         */
/*   slots 1..8    : CAP   (slotted CSMA-CA)  -- or extCFP when converted     */
/*   slots 9..15   : CFP   (7 DSME-GTS)                                       */
/*                                                                            */
/* With CAP reduction enabled, every superframe except SD index 0 carries     */
/* 15 GTSs in slots 1..15 and has no CAP.                                     */
/*---------------------------------------------------------------------------*/
#define DSME_BEACON_SLOT            0
#define DSME_CAP_FIRST_SLOT         1
#define DSME_CAP_SLOTS              8
#define DSME_CAP_LAST_SLOT          (DSME_CAP_FIRST_SLOT + DSME_CAP_SLOTS - 1)
#define DSME_CFP_FIRST_SLOT         9
#define DSME_CFP_SLOTS              7
/* Number of extGTS in a superframe whose CAP has been converted to extCFP.   */
#define DSME_EXTCFP_SLOTS           DSME_CAP_SLOTS
/* Number of GTSs per superframe when CAP reduction is active (slots 1..15).  */
#define DSME_CFP_SLOTS_REDUCED      15

/* Total GTS positions addressable by the SAB in one multi-superframe. */
#define DSME_SAB_SLOTS_PER_SF       DSME_CFP_SLOTS
#define DSME_SAB_TOTAL_SLOTS        (DSME_SF_PER_MS * DSME_SAB_SLOTS_PER_SF)

/*---------------------------------------------------------------------------*/
/* Intra-slot timing, in microseconds. Budget is DSME_SLOT_DURATION_US.       */
/* Layout mirrors the 802.15.4e timeslot template:                            */
/*                                                                            */
/*   TX: |--- TxOffset ---|=== frame ===|-- AckDelay --|== ack ==|            */
/*   RX: |- RxOffset -|<--- RxWait --->|=== frame ===|- AckDelay -|== ack ==| */
/*---------------------------------------------------------------------------*/
#define DSME_TS_GUARD_US            1000
#define DSME_TS_TX_OFFSET_US        1500
#define DSME_TS_RX_OFFSET_US        (DSME_TS_TX_OFFSET_US - DSME_TS_GUARD_US)
#define DSME_TS_RX_WAIT_US          (2 * DSME_TS_GUARD_US)
#define DSME_TS_TX_ACK_DELAY_US     800
#define DSME_TS_RX_ACK_DELAY_US     (DSME_TS_TX_ACK_DELAY_US - 400)
#define DSME_TS_ACK_WAIT_US         1200
/* Longest data frame on air: 125 octets (Cooja radio buffer limit). */
#define DSME_TS_MAX_FRAME_US        (125 * DSME_US_PER_OCTET)

/*
 * Acknowledgement timing inside a GTS. Both ends derive the instant from the
 * slot boundary rather than from the end of the data frame, so the receiver
 * does not have to know the frame length before it has read it. Every data
 * frame in this study is DSME_DATA_FRAME_LEN octets, so this is exact.
 */
#define DSME_TS_DATA_FRAME_US       (DSME_DATA_FRAME_LEN * DSME_US_PER_OCTET)
#define DSME_TS_ACK_TX_OFFSET_US \
  (DSME_TS_TX_OFFSET_US + DSME_TS_DATA_FRAME_US + DSME_TS_TX_ACK_DELAY_US)
/* Instant at which the receiver reads the data frame out of the radio. */
#define DSME_TS_RX_READ_OFFSET_US   (DSME_TS_ACK_TX_OFFSET_US - 300)
/* Instant at which the sender gives up on the acknowledgement. */
#define DSME_TS_ACK_DEADLINE_US \
  (DSME_TS_ACK_TX_OFFSET_US + DSME_ACK_LEN_OCTETS * DSME_US_PER_OCTET + 300)
#define DSME_ACK_LEN_OCTETS         6

/* One backoff period of the slotted CSMA-CA used in the CAP. */
#define DSME_BACKOFF_PERIOD_US \
  (DSME_A_UNIT_BACKOFF_PERIOD * DSME_US_PER_SYMBOL)       /* 320 us */
#define DSME_BACKOFF_PERIODS_PER_SLOT \
  (DSME_SLOT_DURATION_US / DSME_BACKOFF_PERIOD_US)        /* 24 at SO=3 */
/*
 * Backoff periods that actually fit in a CAP slot. The countdown starts at
 * TsRxOffset, not at the slot boundary, and the last period must end early
 * enough to leave room for the longest command frame, so fewer than
 * DSME_BACKOFF_PERIODS_PER_SLOT are usable. Overrunning here would push the
 * next slot late, which is exactly the kind of drift a TDMA MAC must not have.
 */
#define DSME_CAP_TAIL_GUARD_US      400
#define DSME_CAP_BACKOFF_PERIODS \
  ((DSME_SLOT_DURATION_US - DSME_TS_RX_OFFSET_US - DSME_CAP_TAIL_GUARD_US) \
   / DSME_BACKOFF_PERIOD_US)                              /* 21 at SO=3 */
#define DSME_CCA_DURATION_US        (8 * DSME_US_PER_SYMBOL)  /* 128 us */

/*---------------------------------------------------------------------------*/
/* Frame formats                                                              */
/*---------------------------------------------------------------------------*/
#define DSME_FRAME_BEACON           0x00
#define DSME_FRAME_DATA             0x01
#define DSME_FRAME_CMD              0x02
#define DSME_FRAME_ACK              0x03

/* DSME MAC command identifiers. The ext* variants carry taSAB sub-blocks and
 * address the extCFP; see Reference C, Section III-C. */
#define DSME_CMD_GTS_REQUEST        0x10
#define DSME_CMD_GTS_REPLY          0x11
#define DSME_CMD_GTS_NOTIFY         0x12
#define DSME_CMD_EXTGTS_REQUEST     0x20
#define DSME_CMD_EXTGTS_REPLY       0x21
#define DSME_CMD_EXTGTS_NOTIFY      0x22
#define DSME_CMD_DUPLICATED_ALLOC   0x30
#define DSME_CMD_EXT_DUPLICATED_ALLOC 0x31

/* Management type field, Reference C Algorithm 2. */
#define DSME_MGMT_DEALLOCATION      0x00  /* 000 */
#define DSME_MGMT_ALLOCATION        0x01  /* 001 */

/* Status codes carried in a REPLY. */
#define DSME_STATUS_SUCCESS         0x00
#define DSME_STATUS_DENIED_NO_SLOT  0x01  /* "Disapproved - lack of availability" */
#define DSME_STATUS_DENIED_NO_ACT   0x02  /* responder's ACT is full              */

/* Allocation direction, from the point of view of the requesting node. */
#define DSME_DIRECTION_TX           0
#define DSME_DIRECTION_RX           1

/* Changeable CAP bitmap (CCB) states, Reference C Fig. 8. */
#define DSME_CCB_CAP                0x00
#define DSME_CCB_LOP                0x01  /* listen-only period               */
#define DSME_CCB_EXTCFP             0x02

/* Broadcast short address. */
#define DSME_ADDR_BROADCAST         0xFFFF

/*---------------------------------------------------------------------------*/
/* Compile-time sanity checks                                                 */
/*---------------------------------------------------------------------------*/
#if DSME_SO > DSME_MO || DSME_MO > DSME_BO
#error "DSME requires 0 <= SO <= MO <= BO <= 14"
#endif
#if DSME_BEACON_SLOT + 1 + DSME_CAP_SLOTS + DSME_CFP_SLOTS != DSME_SLOTS_PER_SF
#error "Superframe slot layout does not add up to 16 slots"
#endif

#endif /* DSME_CONST_H_ */
