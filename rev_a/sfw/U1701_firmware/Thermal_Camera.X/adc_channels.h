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

#ifndef _ADC_CHANNELS_H    /* Guard against multiple inclusion */
#define _ADC_CHANNELS_H

#include <xc.h>

// These are macros needed for defining ISRs, included in XC32
#include <sys/attribs.h>

#include "adc.h"

// VBAT (AN41), internal VREF (AN42), and internal die temp sensor (AN43)
// are wired directly into the ADC7 shared mux with no external attenuation
// network (PIC32MZ DA family datasheet DS60001565C, Figure 29-1).
#define VBAT_ADC_GAIN               1.0

// Internal die temperature sensor (IVTEMP, AN43) transfer function, per
// DS60001565C Table 44-47 "Temperature Sensor Specifications": output is
// linear from 0.5V at -40C to 1.5V at +160C (slope 5mV/C).
#define DIE_TEMP_SENSOR_SLOPE_V_PER_C   0.005
#define DIE_TEMP_SENSOR_V_AT_TMIN       0.5
#define DIE_TEMP_SENSOR_TMIN_C          (-40.0)

// Board-level trim applied on top of the sensor equation above, determined
// by comparing telemetry.mcu_die_temp against a known-good reference
// temperature. Leave at 0.0 until characterized.
#define HOST_TEMP_OFFSET             0.0

// this function sets up ADC channels
void adcChannelsInitialize(void);

// this function prints out ADC channel status
void printADCChannelStatus(void);

#endif /* _ADC_CHANNELS_H */

/* *****************************************************************************
 End of File
 */