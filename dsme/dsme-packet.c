#include "dsme-packet.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
static inline void
put_u16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}
static inline uint16_t
get_u16(const uint8_t *p)
{
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline void
put_u32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static inline uint32_t
get_u32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/*---------------------------------------------------------------------------*/
static uint8_t
write_hdr(uint8_t *buf, uint8_t type, uint8_t seqno, uint16_t src, uint16_t dst)
{
  buf[0] = type;
  buf[1] = seqno;
  put_u16(&buf[2], src);
  put_u16(&buf[4], dst);
  return DSME_HDR_LEN;
}
/*---------------------------------------------------------------------------*/
int
dsme_pkt_parse_hdr(const uint8_t *buf, uint8_t len, dsme_hdr_t *h)
{
  if(len < DSME_HDR_LEN) {
    return 0;
  }
  h->type = buf[0];
  h->seqno = buf[1];
  h->src = get_u16(&buf[2]);
  h->dst = get_u16(&buf[4]);
  return 1;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_pkt_build_beacon(uint8_t *buf, uint8_t seqno, uint16_t src,
                      uint32_t asn, uint8_t pan_bsn)
{
  uint8_t i = write_hdr(buf, DSME_FRAME_BEACON, seqno, src, DSME_ADDR_BROADCAST);

  /* DSME PAN Descriptor IE: the structure parameters plus the network-wide
   * time reference a joining node needs. */
  buf[i++] = pan_bsn;
  put_u32(&buf[i], asn);
  i = (uint8_t)(i + 4);
  buf[i++] = DSME_SO;
  buf[i++] = DSME_MO;
  buf[i++] = DSME_BO;
#if DSME_MODE == DSME_MODE_CAP_REDUCTION
  buf[i++] = 1;
#else
  buf[i++] = 0;
#endif
  return i;
}
/*---------------------------------------------------------------------------*/
int
dsme_pkt_parse_beacon(const uint8_t *buf, uint8_t len, dsme_beacon_info_t *info)
{
  if(len < DSME_BEACON_LEN || buf[0] != DSME_FRAME_BEACON) {
    return 0;
  }
  info->pan_bsn = buf[DSME_HDR_LEN];
  info->asn = get_u32(&buf[DSME_HDR_LEN + 1]);
  info->so = buf[DSME_HDR_LEN + 5];
  info->mo = buf[DSME_HDR_LEN + 6];
  info->bo = buf[DSME_HDR_LEN + 7];
  info->cap_reduction = buf[DSME_HDR_LEN + 8];
  return 1;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_pkt_build_data(uint8_t *buf, uint8_t seqno, uint16_t src, uint16_t dst,
                    uint8_t total_len)
{
  uint8_t i = write_hdr(buf, DSME_FRAME_DATA, seqno, src, dst);

  if(total_len < DSME_HDR_LEN) {
    total_len = DSME_HDR_LEN;
  }
  /* The payload content is irrelevant to a MAC study; only its length, which
   * sets the air time, is. */
  if(total_len > i) {
    memset(&buf[i], 0xA5, (size_t)(total_len - i));
  }
  return total_len;
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_pkt_build_ack(uint8_t *buf, uint8_t seqno, uint16_t src, uint16_t dst)
{
  return write_hdr(buf, DSME_FRAME_ACK, seqno, src, dst);
}
/*---------------------------------------------------------------------------*/
uint8_t
dsme_pkt_build_cmd(uint8_t *buf, uint8_t seqno, uint16_t src, uint16_t dst,
                   const dsme_cmd_info_t *info, const dsme_sab_t *sab,
                   const dsme_tasab_t *tasab)
{
  uint8_t i = write_hdr(buf, DSME_FRAME_CMD, seqno, src, dst);
  uint8_t s, k;

  buf[i++] = info->cmd;
  buf[i++] = info->mgmt;
  buf[i++] = info->direction;
  buf[i++] = info->status;
  put_u16(&buf[i], info->target);
  i = (uint8_t)(i + 2);
  buf[i++] = info->channel_off;
  buf[i++] = info->num_slots;
  buf[i++] = info->pref_sd;
  buf[i++] = info->pref_slot;

  /* Newly allocated (or deallocated) slot list. */
  buf[i++] = info->n_alloc;
  for(k = 0; k < info->n_alloc; k++) {
    buf[i++] = info->alloc_sd[k];
    buf[i++] = info->alloc_slot[k];
  }

  /* Allocation bitmap sub-blocks. */
  buf[i++] = info->n_sub;
  buf[i++] = info->sub_start;
  for(k = 0; k < info->n_sub; k++) {
    s = (uint8_t)((info->sub_start + k) % DSME_SF_PER_MS);
    if(dsme_cmd_is_ext(info->cmd)) {
      uint8_t j;
      for(j = 0; j < DSME_EXTCFP_SLOTS; j++) {
        put_u16(&buf[i], tasab != NULL ? tasab->sub[s][j] : 0);
        i = (uint8_t)(i + 2);
      }
    } else {
      put_u16(&buf[i], sab != NULL ? sab->sub[s] : 0);
      i = (uint8_t)(i + 2);
    }
  }
  return i;
}
/*---------------------------------------------------------------------------*/
int
dsme_pkt_parse_cmd(const uint8_t *buf, uint8_t len, dsme_cmd_info_t *info)
{
  uint8_t i = DSME_HDR_LEN;
  uint8_t k, s;

  if(len < DSME_HDR_LEN + 12 || buf[0] != DSME_FRAME_CMD) {
    return 0;
  }
  memset(info, 0, sizeof(*info));
  info->cmd = buf[i++];
  info->mgmt = buf[i++];
  info->direction = buf[i++];
  info->status = buf[i++];
  info->target = get_u16(&buf[i]);
  i = (uint8_t)(i + 2);
  info->channel_off = buf[i++];
  info->num_slots = buf[i++];
  info->pref_sd = buf[i++];
  info->pref_slot = buf[i++];

  info->n_alloc = buf[i++];
  if(info->n_alloc > DSME_MAX_SLOTS_PER_REQUEST) {
    return 0;
  }
  if((uint16_t)i + 2 * (uint16_t)info->n_alloc + 2 > len) {
    return 0;
  }
  for(k = 0; k < info->n_alloc; k++) {
    info->alloc_sd[k] = buf[i++];
    info->alloc_slot[k] = buf[i++];
  }

  info->n_sub = buf[i++];
  info->sub_start = buf[i++];
  if(info->n_sub > DSME_SF_PER_MS) {
    return 0;
  }
  for(k = 0; k < info->n_sub; k++) {
    s = (uint8_t)((info->sub_start + k) % DSME_SF_PER_MS);
    if(dsme_cmd_is_ext(info->cmd)) {
      uint8_t j;
      if((uint16_t)i + 2 * DSME_EXTCFP_SLOTS > len) {
        return 0;
      }
      for(j = 0; j < DSME_EXTCFP_SLOTS; j++) {
        info->tasab[s][j] = get_u16(&buf[i]);
        i = (uint8_t)(i + 2);
      }
    } else {
      if((uint16_t)i + 2 > len) {
        return 0;
      }
      info->sab[s] = get_u16(&buf[i]);
      i = (uint8_t)(i + 2);
    }
  }
  return 1;
}
/*---------------------------------------------------------------------------*/
