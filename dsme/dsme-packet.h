/*
 * dsme-packet.h -- DSME frame construction and parsing.
 *
 * A compact fixed-layout header is used instead of the full IEEE 802.15.4
 * framer. The fields that matter to this study -- frame type, addresses,
 * sequence number, and the GTS / extGTS command payloads -- are all present;
 * the auxiliary security header, PAN IDs and IE list are not, because no part
 * of DSME-GTS management depends on them. Frame lengths are kept
 * representative so that the air time Cooja derives from the frame size stays
 * faithful to the 250 kbit/s O-QPSK PHY.
 *
 *   octet 0      frame type
 *   octet 1      sequence number
 *   octets 2-3   source short address      (little endian)
 *   octets 4-5   destination short address (little endian)
 */

#ifndef DSME_PACKET_H_
#define DSME_PACKET_H_

#include "dsme-types.h"

#define DSME_HDR_LEN                6

/* Enhanced beacon: header + DSME PAN Descriptor IE. */
#define DSME_BEACON_LEN             (DSME_HDR_LEN + 9)
/* Acknowledgement frame. */
#define DSME_ACK_LEN                DSME_HDR_LEN

/*
 * Number of SAB (or taSAB) sub-blocks carried by one GTS Request. Reference C
 * notes that when this is smaller than the number of superframes in a
 * multi-superframe, a pair of nodes must exchange Request/Reply repeatedly to
 * compare their entire CFPs -- which is precisely what makes the published
 * trigger reactive.
 */
#ifndef DSME_SAB_SUBBLOCKS_PER_REQUEST
#define DSME_SAB_SUBBLOCKS_PER_REQUEST 1
#endif

/** Parsed fixed header. */
typedef struct {
  uint8_t type;
  uint8_t seqno;
  uint16_t src;
  uint16_t dst;
} dsme_hdr_t;

/** Parsed beacon payload. */
typedef struct {
  uint8_t pan_bsn;
  uint32_t asn;
  uint8_t so, mo, bo;
  uint8_t cap_reduction;
} dsme_beacon_info_t;

/** Parsed GTS / extGTS command payload. */
typedef struct {
  uint8_t cmd;
  uint8_t mgmt;              /* DSME_MGMT_ALLOCATION / _DEALLOCATION */
  uint8_t direction;         /* as seen by the requester */
  uint8_t status;            /* REPLY only */
  /* Destination Address field of the command payload. REPLY and NOTIFY go out
   * as broadcasts so that one-hop neighbours can keep their SABs up to date,
   * so the node the command actually concerns has to be named inside it
   * (Reference B, Fig. 12). */
  uint16_t target;
  uint8_t channel_off;       /* channel offset of the receiving node */
  uint8_t num_slots;         /* slots requested */
  uint8_t pref_sd;
  uint8_t pref_slot;
  uint8_t n_alloc;           /* length of the newly-allocated slot list */
  uint8_t alloc_sd[DSME_MAX_SLOTS_PER_REQUEST];
  uint8_t alloc_slot[DSME_MAX_SLOTS_PER_REQUEST];
  uint8_t n_sub;             /* number of sub-blocks carried */
  uint8_t sub_start;         /* superframe ID of the first sub-block */
  uint16_t sab[DSME_SF_PER_MS];                            /* GTS commands   */
  uint16_t tasab[DSME_SF_PER_MS][DSME_EXTCFP_SLOTS];       /* extGTS commands*/
} dsme_cmd_info_t;

/*---------------------------------------------------------------------------*/
int dsme_pkt_parse_hdr(const uint8_t *buf, uint8_t len, dsme_hdr_t *h);

uint8_t dsme_pkt_build_beacon(uint8_t *buf, uint8_t seqno, uint16_t src,
                              uint32_t asn, uint8_t pan_bsn);
int dsme_pkt_parse_beacon(const uint8_t *buf, uint8_t len,
                          dsme_beacon_info_t *info);

uint8_t dsme_pkt_build_data(uint8_t *buf, uint8_t seqno, uint16_t src,
                            uint16_t dst, uint8_t total_len);
uint8_t dsme_pkt_build_ack(uint8_t *buf, uint8_t seqno, uint16_t src,
                           uint16_t dst);

/**
 * Build a GTS or extGTS command frame.
 * \p sab is used for the GTS variants, \p tasab for the extGTS variants;
 * either may be NULL when the command carries no bitmap.
 * \return the frame length.
 */
uint8_t dsme_pkt_build_cmd(uint8_t *buf, uint8_t seqno, uint16_t src,
                           uint16_t dst, const dsme_cmd_info_t *info,
                           const dsme_sab_t *sab, const dsme_tasab_t *tasab);

int dsme_pkt_parse_cmd(const uint8_t *buf, uint8_t len, dsme_cmd_info_t *info);

/** 1 if \p cmd operates on extGTSs rather than GTSs. */
static inline int dsme_cmd_is_ext(uint8_t cmd)
{
  return cmd == DSME_CMD_EXTGTS_REQUEST || cmd == DSME_CMD_EXTGTS_REPLY ||
         cmd == DSME_CMD_EXTGTS_NOTIFY || cmd == DSME_CMD_EXT_DUPLICATED_ALLOC;
}

#endif /* DSME_PACKET_H_ */
