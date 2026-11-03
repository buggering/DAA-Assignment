/* Host stand-in for Contiki-NG's rtimer, matching the Cooja platform: a
 * 32-bit counter running at 1 MHz, so one tick is one microsecond. */
#ifndef RTIMER_H_
#define RTIMER_H_
#include <stdint.h>
typedef uint32_t rtimer_clock_t;
#define RTIMER_SECOND            UINT32_C(1000000)
#define US_TO_RTIMERTICKS(US)    (US)
#define RTIMERTICKS_TO_US(T)     (T)
#endif /* RTIMER_H_ */
