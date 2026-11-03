/*
 * project-conf.h -- Netstack and platform configuration for the DSME
 *                   TaCFPext / P-TaCFPext experiments.
 */

#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* The DSME MAC owns the radio: no framer, no CSMA layer, no IPv6 stack. */
#define NETSTACK_CONF_MAC                      dsme_driver

/*
 * Cooja platform.
 *
 * The turnaround emulation in the Cooja radio inserts two extra mote yields
 * around every transmission, and a yield advances simulated time by a whole
 * millisecond. That would smear the slot template, so it is disabled -- the
 * same reason Contiki-NG disables it for TSCH.
 *
 * Transmit-on-CCA is likewise disabled: all carrier sensing in the CAP is
 * performed explicitly by the slotted CSMA-CA backoff machine, which needs to
 * observe the result rather than have the radio silently drop the frame.
 */
#define COOJA_CONF_SIMULATE_TURNAROUND         0
#define COOJA_CONF_TRANSMIT_ON_CCA             0

/* The 802.15.4 channel a node sits on before it has synchronised. */
#define IEEE802154_CONF_DEFAULT_CHANNEL        26

/* Keep the Contiki log subsystem quiet; the DSME event log is the output. */
#define LOG_CONF_LEVEL_MAC                     0
#define LOG_CONF_LEVEL_FRAMER                  0
#define LOG_CONF_LEVEL_MAIN                    0

#endif /* PROJECT_CONF_H_ */
