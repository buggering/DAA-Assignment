/*
 * dsme-log.h -- Structured event log.
 *
 * Every line is machine-parsable and carries the multi-superframe position
 * of the event, so the offline analysis can align node-local events with the
 * global superframe grid without relying on the simulator's wall clock.
 *
 *   #D|<event>|asn=<n>|<key>=<value>|...
 */

#ifndef DSME_LOG_H_
#define DSME_LOG_H_

#include "dsme-conf.h"
#include <stdio.h>

#if DSME_LOG_LEVEL > 0
#define DSME_LOG(...)   do { printf("#D|"); printf(__VA_ARGS__); printf("\n"); } while(0)
#else
#define DSME_LOG(...)
#endif

#if DSME_LOG_LEVEL > 1
#define DSME_LOG_DBG(...) DSME_LOG(__VA_ARGS__)
#else
#define DSME_LOG_DBG(...)
#endif

#endif /* DSME_LOG_H_ */
