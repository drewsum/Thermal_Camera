

#include "application/telemetry.h"

#include <xc.h>
#include <stdio.h>

#include "usb_uart/terminal_control.h"
#include "gpio/pin_macros.h"
#include "application/pgood_monitor.h"
#include "i2c/i2c_master.h"
#include "i2c/i2c_devices.h"

// This prints all telemetry data in an easily digested format
void printCurrentTelemetry(void) {
 
     // Print stuff off for POS12
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+12V Power Input:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos12.voltage,
            telemetry.pos12.current,
            telemetry.pos12.power,
            telemetry.pos12.temperature);

    // Print stuff off for POS3P0
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+3.0V Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos3p0.voltage,
            telemetry.pos3p0.current,
            telemetry.pos3p0.power,
            telemetry.pos3p0.temperature);

    // Print stuff off for POS1P8
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+1.8V Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos1p8.voltage,
            telemetry.pos1p8.current,
            telemetry.pos1p8.power,
            telemetry.pos1p8.temperature);

    // Print stuff off for POS2P8
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+2.8V Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos2p8.voltage,
            telemetry.pos2p8.current,
            telemetry.pos2p8.power,
            telemetry.pos2p8.temperature);

    // Print stuff off for POS1P2
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+1.2V Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos1p2.voltage,
            telemetry.pos1p2.current,
            telemetry.pos1p2.power,
            telemetry.pos1p2.temperature);

    // Print stuff off for Backlight
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\tBacklight Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.backlight.voltage,
            telemetry.backlight.current,
            telemetry.backlight.power,
            telemetry.backlight.temperature);

    // print off other random data points
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\tMiscellaneous Telemetry:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tMCU Die Temperature: %.3fC\033[K\r\n", telemetry.mcu_die_temp);
    printf("\t\tMCU ADC Reference Voltage: %.3fV\033[K\r\n", telemetry.adc_vref_voltage);
    printf("\t\tMCU Battery Voltage: %.3fV\033[K\r\n", telemetry.mcu_battery_voltage);
    printf("\t\tAmbient Temperature: %.3fC\033[K\r\n", telemetry.ambient_temperature);

    // print off battery fuel gauge telemetry
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\tBattery (BQ27441 Fuel Gauge):\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    if (telemetry.battery.present) {
        printf("\t\tVoltage: %.3fV"
               "\tCurrent: %.3fA"
               "\tTemp: %.3fC\033[K\r\n"
               "\t\tSOC: %.1f%%"
               "\tSOH: %.1f%%"
               "\tRemaining: %.1f / %.1f mAh\033[K\r\n"
               "\t\tStatus: %s%s%s%s%s\033[K\r\n\033[K\r\n",
               telemetry.battery.voltage,
               telemetry.battery.current,
               telemetry.battery.temperature,
               telemetry.battery.state_of_charge,
               telemetry.battery.state_of_health,
               telemetry.battery.remaining_capacity,
               telemetry.battery.full_charge_capacity,
               telemetry.battery.charging       ? "CHARGING "      : "",
               telemetry.battery.discharging    ? "DISCHARGING "   : "",
               telemetry.battery.fully_charged  ? "FULLY-CHARGED " : "",
               telemetry.battery.over_temperature  ? "OVER-TEMP "  : "",
               telemetry.battery.under_temperature ? "UNDER-TEMP " : "");
        if (telemetry.battery.low_battery) {
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("\t\tLOW BATTERY\033[K\r\n");
            terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
        }
    } else {
        printf("\t\tNo battery detected at boot\033[K\r\n");
    }
    printf("\033[K\r\n");

    // print out state of PGOOD pins
    printPGOODStatus();

    printf("\r\n");

    terminalTextAttributesReset();

}

// *****************************************************************************
// Section: Non-blocking I2C telemetry staging
// *****************************************************************************
// updateTemperatureTelemetry()/updatePowerMonitorTelemetry() only QUEUE the
// I2C register reads (via the i2c_master transaction queue) and return
// immediately; the I2C1 ISR runs the transfers back-to-back with no CPU
// involvement. Each read's callback just records the raw bytes and outcome
// in a staging slot -- integer-only, since it runs at IPL7 where FPU state
// isn't saved. telemetryTasks() (main loop) then decodes finished slots
// into the telemetry struct, and reports failed ones to the owning device's
// error_handler.flags.*_i2c_error flag via I2CDevices_ReportI2CError() --
// see i2c_devices.h for why that call has to happen here rather than inside
// i2c_devices.c itself.

typedef enum {
    TELEM_SLOT_IDLE = 0,    // free; safe to queue a new read
    TELEM_SLOT_PENDING,     // read queued/in flight; ISR owns .raw
    TELEM_SLOT_READY,       // read finished OK; .raw holds fresh data
    TELEM_SLOT_FAILED,      // read failed (NACK/timeout/...); keep old value, report the error
} telem_slot_state_t;

typedef struct {
    uint8_t raw[2];                 // raw register image, MSB first
    volatile uint8_t state;         // telem_slot_state_t; written from I2C ISR
} telem_i2c_slot_t;

// Runs in I2C interrupt context: record the outcome, nothing more.
static void telemetrySlotCallback(uintptr_t context, I2C_ERROR error) {

    telem_i2c_slot_t *slot = (telem_i2c_slot_t *)context;

    slot->state = (error == I2C_ERROR_NONE) ? TELEM_SLOT_READY : TELEM_SLOT_FAILED;

}

// Marks `slot` pending and queues one read via `queueRead`; on queue-full
// (or kind mismatch) the slot is released to retry on the next request.
static void telemetryQueueSlot(telem_i2c_slot_t *slot, I2C_DEVICE_ID id,
                               bool (*queueRead)(I2C_DEVICE_ID, uint8_t*, I2C_TRANSFER_CALLBACK, uintptr_t)) {

    // previous read of this quantity still in flight -- don't double-queue
    if (slot->state == TELEM_SLOT_PENDING) return;

    slot->state = TELEM_SLOT_PENDING;

    if (!queueRead(id, slot->raw, telemetrySlotCallback, (uintptr_t)slot)) {
        slot->state = TELEM_SLOT_IDLE;
    }

}

// I2C_DEV_TEMP_1..7 (i2c_devices.h) are wired to physical MCP9804s in this
// exact order (POS12, POS3P0, POS1P8, POS2P8, POS1P2, Backlight, Ambient) --
// keep these tables in sync with I2C_DEVICE_LIST if that list is ever reordered.
#define TELEM_TEMP_COUNT    7u

static const I2C_DEVICE_ID telemTempDevice[TELEM_TEMP_COUNT] = {
    I2C_DEV_TEMP_1, I2C_DEV_TEMP_2, I2C_DEV_TEMP_3, I2C_DEV_TEMP_4,
    I2C_DEV_TEMP_5, I2C_DEV_TEMP_6, I2C_DEV_TEMP_7
};

static volatile double * const telemTempDest[TELEM_TEMP_COUNT] = {
    &telemetry.pos12.temperature,  &telemetry.pos3p0.temperature,
    &telemetry.pos1p8.temperature, &telemetry.pos2p8.temperature,
    &telemetry.pos1p2.temperature, &telemetry.backlight.temperature,
    &telemetry.ambient_temperature
};

static telem_i2c_slot_t telemTempSlot[TELEM_TEMP_COUNT];

// I2C_DEV_PWR_1..3,5,6 (i2c_devices.h; PWR_4/POS2P8 isn't populated on this
// board) are wired to physical INA231As in the same rail order as their
// I2C_DEV_TEMP_1..6 temperature sensor counterparts.
#define TELEM_PWR_COUNT     5u
#define TELEM_PWR_QTY       3u   // voltage, current, power (order below)

static const I2C_DEVICE_ID telemPwrDevice[TELEM_PWR_COUNT] = {
    I2C_DEV_PWR_1, I2C_DEV_PWR_2, I2C_DEV_PWR_3,
    I2C_DEV_PWR_5, I2C_DEV_PWR_6
};

static volatile telemetry_parameters_ps_t * const telemPwrDest[TELEM_PWR_COUNT] = {
    &telemetry.pos12,  &telemetry.pos3p0, &telemetry.pos1p8,
    &telemetry.pos1p2, &telemetry.backlight
};

static bool (* const telemPwrQueueRead[TELEM_PWR_QTY])(I2C_DEVICE_ID, uint8_t*, I2C_TRANSFER_CALLBACK, uintptr_t) = {
    I2CDevices_QueueVoltageRead, I2CDevices_QueueCurrentRead, I2CDevices_QueuePowerRead
};

static telem_i2c_slot_t telemPwrSlot[TELEM_PWR_COUNT][TELEM_PWR_QTY];

// Only one BQ27441 fuel gauge on this board (I2C_DEV_BATT_1), but it
// needs 8 quantities queued per cycle -- more than any other single
// device kind -- so this uses a flat per-quantity slot array rather than
// the 2D [device][qty] shape telemPwrSlot above uses for its 5 devices.
#define TELEM_BATT_QTY   8u   // voltage, current, temperature, soc, remcap, fullcap, soh, flags

static telem_i2c_slot_t telemBattSlot[TELEM_BATT_QTY];

static bool (* const telemBattQueueRead[TELEM_BATT_QTY])(I2C_DEVICE_ID, uint8_t*, I2C_TRANSFER_CALLBACK, uintptr_t) = {
    I2CDevices_QueueBatteryVoltageRead,
    I2CDevices_QueueBatteryCurrentRead,
    I2CDevices_QueueBatteryTemperatureRead,
    I2CDevices_QueueBatteryStateOfChargeRead,
    I2CDevices_QueueBatteryRemainingCapacityRead,
    I2CDevices_QueueBatteryFullChargeCapacityRead,
    I2CDevices_QueueBatteryStateOfHealthRead,
    I2CDevices_QueueBatteryFlagsRead
};

void updateTemperatureTelemetry(void) {

    uint8_t i;

    for (i = 0; i < TELEM_TEMP_COUNT; i++) {
        telemetryQueueSlot(&telemTempSlot[i], telemTempDevice[i], I2CDevices_QueueTemperatureRead);
    }

}

void updatePowerMonitorTelemetry(void) {

    uint8_t dev;
    uint8_t qty;

    for (dev = 0; dev < TELEM_PWR_COUNT; dev++) {
        for (qty = 0; qty < TELEM_PWR_QTY; qty++) {
            telemetryQueueSlot(&telemPwrSlot[dev][qty], telemPwrDevice[dev], telemPwrQueueRead[qty]);
        }
    }

}

void updateBatteryTelemetry(void) {

    uint8_t qty;

    for (qty = 0; qty < TELEM_BATT_QTY; qty++) {
        telemetryQueueSlot(&telemBattSlot[qty], I2C_DEV_BATT_1, telemBattQueueRead[qty]);
    }

}

void telemetryTasks(void) {

    uint8_t i;
    uint8_t qty;

    for (i = 0; i < TELEM_TEMP_COUNT; i++) {

        telem_i2c_slot_t *slot = &telemTempSlot[i];

        if (slot->state == TELEM_SLOT_READY) {

            I2C_DEVICE_TEMP_READING reading;

            if (I2CDevices_DecodeTemperature(telemTempDevice[i], slot->raw, &reading)) {
                *telemTempDest[i] = reading.celsius;
            }
            slot->state = TELEM_SLOT_IDLE;

        } else if (slot->state == TELEM_SLOT_FAILED) {
            // sensor didn't respond; keep the last good value, but latch it
            // against this device's error_handler.flags.*_i2c_error
            I2CDevices_ReportI2CError(telemTempDevice[i]);
            slot->state = TELEM_SLOT_IDLE;
        }

    }

    for (i = 0; i < TELEM_PWR_COUNT; i++) {
        for (qty = 0; qty < TELEM_PWR_QTY; qty++) {

            telem_i2c_slot_t *slot = &telemPwrSlot[i][qty];

            if (slot->state == TELEM_SLOT_READY) {

                float value;
                bool decoded = false;

                switch (qty) {
                    case 0: decoded = I2CDevices_DecodeVoltage(telemPwrDevice[i], slot->raw, &value); break;
                    case 1: decoded = I2CDevices_DecodeCurrent(telemPwrDevice[i], slot->raw, &value); break;
                    case 2: decoded = I2CDevices_DecodePower(telemPwrDevice[i], slot->raw, &value);   break;
                    default: break;
                }

                if (decoded) {
                    switch (qty) {
                        case 0: telemPwrDest[i]->voltage = value; break;
                        case 1: telemPwrDest[i]->current = value; break;
                        case 2: telemPwrDest[i]->power   = value; break;
                        default: break;
                    }
                }
                slot->state = TELEM_SLOT_IDLE;

            } else if (slot->state == TELEM_SLOT_FAILED) {
                // monitor didn't respond; keep the last good value, but latch
                // it against this device's error_handler.flags.*_i2c_error
                I2CDevices_ReportI2CError(telemPwrDevice[i]);
                slot->state = TELEM_SLOT_IDLE;
            }

        }
    }

    for (qty = 0; qty < TELEM_BATT_QTY; qty++) {

        telem_i2c_slot_t *slot = &telemBattSlot[qty];

        if (slot->state == TELEM_SLOT_READY) {

            switch (qty) {
                case 0: {
                    float value;
                    if (I2CDevices_DecodeBatteryVoltage(I2C_DEV_BATT_1, slot->raw, &value)) {
                        telemetry.battery.voltage = value;
                    }
                    break;
                }
                case 1: {
                    float value;
                    if (I2CDevices_DecodeBatteryCurrent(I2C_DEV_BATT_1, slot->raw, &value)) {
                        telemetry.battery.current = value;
                    }
                    break;
                }
                case 2: {
                    float value;
                    if (I2CDevices_DecodeBatteryTemperature(I2C_DEV_BATT_1, slot->raw, &value)) {
                        telemetry.battery.temperature = value;
                    }
                    break;
                }
                case 3: {
                    uint8_t value;
                    if (I2CDevices_DecodeBatteryStateOfCharge(I2C_DEV_BATT_1, slot->raw, &value)) {
                        telemetry.battery.state_of_charge = value;
                    }
                    break;
                }
                case 4: {
                    float value;
                    if (I2CDevices_DecodeBatteryRemainingCapacity(I2C_DEV_BATT_1, slot->raw, &value)) {
                        telemetry.battery.remaining_capacity = value;
                    }
                    break;
                }
                case 5: {
                    float value;
                    if (I2CDevices_DecodeBatteryFullChargeCapacity(I2C_DEV_BATT_1, slot->raw, &value)) {
                        telemetry.battery.full_charge_capacity = value;
                    }
                    break;
                }
                case 6: {
                    uint8_t value;
                    if (I2CDevices_DecodeBatteryStateOfHealth(I2C_DEV_BATT_1, slot->raw, &value)) {
                        telemetry.battery.state_of_health = value;
                    }
                    break;
                }
                case 7: {
                    I2C_DEVICE_BATTERY_FLAGS flags;
                    if (I2CDevices_DecodeBatteryFlags(I2C_DEV_BATT_1, slot->raw, &flags)) {
                        telemetry.battery.charging          = flags.fastChargingAllowed;
                        telemetry.battery.discharging        = flags.dischargeDetected;
                        telemetry.battery.fully_charged      = flags.fullyCharged;
                        telemetry.battery.over_temperature   = flags.overTemperature;
                        telemetry.battery.under_temperature  = flags.underTemperature;
                        telemetry.battery.low_battery         = flags.lowStateOfCharge;
                    }
                    break;
                }
                default: break;
            }
            slot->state = TELEM_SLOT_IDLE;

        } else if (slot->state == TELEM_SLOT_FAILED) {
            // fuel gauge didn't respond; keep the last good value, but latch
            // it against this device's error_handler.flags.*_i2c_error
            I2CDevices_ReportI2CError(I2C_DEV_BATT_1);
            slot->state = TELEM_SLOT_IDLE;
        }

    }

}