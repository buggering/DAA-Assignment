/*
 * dsme-queue.h -- Fixed-capacity FIFOs for data frames and CAP commands.
 */

#ifndef DSME_QUEUE_H_
#define DSME_QUEUE_H_

#include "dsme-types.h"

void dsme_queue_init(dsme_queue_t *q);
/** \return 1 on success, 0 if full. */
int dsme_queue_push(dsme_queue_t *q, const dsme_packet_t *p);
/** \return the head without removing it, or NULL when empty. */
dsme_packet_t *dsme_queue_peek(dsme_queue_t *q);
void dsme_queue_pop(dsme_queue_t *q);
static inline uint8_t dsme_queue_count(const dsme_queue_t *q) { return q->count; }

void dsme_cmd_queue_init(dsme_cmd_queue_t *q);
int dsme_cmd_queue_push(dsme_cmd_queue_t *q, const dsme_cmd_t *c);
/* Jump the queue. A requester blocks on the REPLY under
 * macMaxFrameTotalWaitTime, whereas NOTIFY and Duplicated Allocation
 * Notification frames only refresh neighbours' bitmaps and can wait. */
int dsme_cmd_queue_push_front(dsme_cmd_queue_t *q, const dsme_cmd_t *c);
dsme_cmd_t *dsme_cmd_queue_peek(dsme_cmd_queue_t *q);
void dsme_cmd_queue_pop(dsme_cmd_queue_t *q);
static inline uint8_t dsme_cmd_queue_count(const dsme_cmd_queue_t *q) { return q->count; }

#endif /* DSME_QUEUE_H_ */
