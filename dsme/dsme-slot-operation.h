/*
 * dsme-slot-operation.h -- The rtimer-driven multi-superframe state machine.
 */

#ifndef DSME_SLOT_OPERATION_H_
#define DSME_SLOT_OPERATION_H_

#include "dsme-types.h"

/** Begin scanning for a beacon, or -- on the PAN coordinator -- start the PAN. */
void dsme_slot_operation_start(void);
void dsme_slot_operation_stop(void);

#endif /* DSME_SLOT_OPERATION_H_ */
