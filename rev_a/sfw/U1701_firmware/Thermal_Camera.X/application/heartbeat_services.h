/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.h

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _HEARTBEAT_SERVICES_H    /* Guard against multiple inclusion */
#define _HEARTBEAT_SERVICES_H

#include "xc.h"

#include "core/heartbeat_timer.h"

// API Variables
volatile uint32_t device_on_time_counter;

// Set by heartbeatServices() every ~200ms while live telemetry is enabled.
// main()'s loop checks this, calls updateTemperatureTelemetry() (blocking
// I2C reads -- must not happen here in ISR context), and clears it.
volatile __attribute__((coherent)) uint8_t temp_sense_data_request;
volatile __attribute__((coherent)) uint8_t power_monitor_data_request;

// This function executes actions every call of the heartbeat timer, and is used as an easy interface to do some action every second
void heartbeatServices(void);

#endif /* _HEARTBEAT_SERVICES_H */

/* *****************************************************************************
 End of File
 */