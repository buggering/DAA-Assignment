#include "dsme-queue.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
void
dsme_queue_init(dsme_queue_t *q)
{
  q->head = q->tail = q->count = 0;
}
/*---------------------------------------------------------------------------*/
int
dsme_queue_push(dsme_queue_t *q, const dsme_packet_t *p)
{
  if(q->count >= DSME_QUEUE_LEN) {
    return 0;
  }
  memcpy(&q->items[q->tail], p, sizeof(dsme_packet_t));
  q->tail = (uint8_t)((q->tail + 1) % DSME_QUEUE_LEN);
  q->count++;
  return 1;
}
/*---------------------------------------------------------------------------*/
dsme_packet_t *
dsme_queue_peek(dsme_queue_t *q)
{
  if(q->count == 0) {
    return NULL;
  }
  return &q->items[q->head];
}
/*---------------------------------------------------------------------------*/
void
dsme_queue_pop(dsme_queue_t *q)
{
  if(q->count == 0) {
    return;
  }
  q->head = (uint8_t)((q->head + 1) % DSME_QUEUE_LEN);
  q->count--;
}
/*---------------------------------------------------------------------------*/
void
dsme_cmd_queue_init(dsme_cmd_queue_t *q)
{
  q->head = q->tail = q->count = 0;
}
/*---------------------------------------------------------------------------*/
int
dsme_cmd_queue_push(dsme_cmd_queue_t *q, const dsme_cmd_t *c)
{
  if(q->count >= DSME_CMD_QUEUE_LEN) {
    return 0;
  }
  memcpy(&q->items[q->tail], c, sizeof(dsme_cmd_t));
  q->tail = (uint8_t)((q->tail + 1) % DSME_CMD_QUEUE_LEN);
  q->count++;
  return 1;
}
/*---------------------------------------------------------------------------*/
int
dsme_cmd_queue_push_front(dsme_cmd_queue_t *q, const dsme_cmd_t *c)
{
  if(q->count >= DSME_CMD_QUEUE_LEN) {
    return 0;
  }
  q->head = (uint8_t)((q->head + DSME_CMD_QUEUE_LEN - 1) % DSME_CMD_QUEUE_LEN);
  memcpy(&q->items[q->head], c, sizeof(dsme_cmd_t));
  q->count++;
  return 1;
}
/*---------------------------------------------------------------------------*/
dsme_cmd_t *
dsme_cmd_queue_peek(dsme_cmd_queue_t *q)
{
  if(q->count == 0) {
    return NULL;
  }
  return &q->items[q->head];
}
/*---------------------------------------------------------------------------*/
void
dsme_cmd_queue_pop(dsme_cmd_queue_t *q)
{
  if(q->count == 0) {
    return;
  }
  q->head = (uint8_t)((q->head + 1) % DSME_CMD_QUEUE_LEN);
  q->count--;
}
/*---------------------------------------------------------------------------*/
