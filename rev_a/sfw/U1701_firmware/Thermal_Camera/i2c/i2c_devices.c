/*******************************************************************************
  I2C Device Registry

  File Name:
    i2c_devices.c

  Summary:
    The single interface for every physical I2C device on the board:
    presence, address, name, register-level status printing, and
    kind-specific typed reads, all dispatched by device kind.
*******************************************************************************/

#include "i2c/i2c_devices.h"
#include "i2c/device_driver/mcp9804.h"
#include "i2c/device_driver/ina231a.h"
#include "i2c/device_driver/ds1683.h"
#include "i2c/device_driver/gt911.h"
#include "i2c/device_driver/bq27441.h"
#include "usb_uart/terminal_control.h"
#include "application/error_handler.h"

#include <stdio.h>

#define I2C_DEVICE_ADDRESS_ENTRY(name, kind, address, label, refdes)  (address),
static const uint16_t i2cDeviceAddresses[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_ADDRESS_ENTRY)
};

#define I2C_DEVICE_KIND_ENTRY(name, kind, address, label, refdes)  (kind),
static const I2C_DEVICE_KIND i2cDeviceKinds[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_KIND_ENTRY)
};

#define I2C_DEVICE_NAME_ENTRY(name, kind, address, label, refdes)  label,
static const char* const i2cDeviceNames[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_NAME_ENTRY)
};

#define I2C_DEVICE_REFDES_ENTRY(name, kind, address, label, refdes)  refdes,
static const char* const i2cDeviceRefdes[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_REFDES_ENTRY)
};

static bool i2cDevicePresent[I2C_DEVICE_COUNT];

// Only meaningful for I2C_DEVICE_KIND_INA231A entries -- the Current_LSB
// (amps/bit) I2CDevices_ConfigureOne() got back from INA231A_Configure().
static float i2cDeviceCurrentLSB[I2C_DEVICE_COUNT];

// All 6 INA231A power monitors on this board share this shunt resistor and
// expected max current -- update here (or move into I2C_DEVICE_LIST as a
// per-device column) if that ever differs per rail.
#define INA231A_SHUNT_RESISTANCE_OHMS       0.02f
#define INA231A_MAX_EXPECTED_CURRENT_AMPS   3.1f

// The 18650 cell fitted to this board, programmed into the BQ27441's data
// memory by BQ27441_Configure(). Update these together if the cell changes.
//
// Taper rate is derived from the MAX8903G's charge termination current, not
// picked: R_ISET (1.2k to GND) sets the fast-charge current per the
// datasheet's R_ISET = 1200V / I_CHGMAX, giving 1.0A, and the charger
// terminates at 10% of fast-charge, so 100mA. Taper rate is then
// DesignCapacity / (0.1 * 100mA) = 2600 / 10 = 260.
//
// Terminate voltage is the battery voltage at which this board stops
// working, i.e. where SOC should read 0%. Both switchers fed from the
// battery (+3.0V and +1.8V) are TPS62130As, and that part's specified input
// range starts at 3.0V -- so 3.0V is a hard floor no matter what the load
// is. The +3.0V rail is the binding one on top of that: it runs out of
// headroom first, since at 100% duty cycle its output is the input minus
// I_load * (high-side RDS(on) + L401's DCR). With L401 at 2.2uH/IHLP2020
// that series term is on the order of 0.1 ohm, so the extra headroom needed
// is only a few mV at the ~35mA this rail draws at idle, rising to ~100mV
// if it ever pulls an amp. The MAX8903's BAT-to-SYS FET drops a bit more,
// carrying the whole system current.
//
// That puts the real cutoff between roughly 3.0V and 3.15V depending on
// load, so the gauge's 3200mV default already sits just above it with a
// little margin, and is kept. Dropping it to 3000mV would buy very little
// -- a Li-ion discharge curve is nearly vertical below 3.3V, so only a few
// percent of capacity lives between 3.2V and 3.0V -- while risking a
// brownout mid-operation. POS3P0_PGOOD is the definitive empirical check if
// this is ever worth revisiting: log telemetry.battery.voltage at the moment
// that pin drops under a worst-case load.
#define BATTERY_DESIGN_CAPACITY_MAH         2600u
#define BATTERY_DESIGN_ENERGY_MWH           9620u    // 2600mAh * 3.7V nominal
#define BATTERY_TERMINATE_VOLTAGE_MV        3200u
#define BATTERY_TAPER_RATE                  260u

static const BQ27441_BATTERY_PROFILE batteryProfile =
{
    BATTERY_DESIGN_CAPACITY_MAH,
    BATTERY_DESIGN_ENERGY_MWH,
    BATTERY_TERMINATE_VOLTAGE_MV,
    BATTERY_TAPER_RATE
};

static bool I2CDevices_IdIsValid(I2C_DEVICE_ID id)
{
    return ((unsigned)id < (unsigned)I2C_DEVICE_COUNT);
}

void I2CDevices_ReportI2CError(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return;
    }

    ERROR_HANDLER_I2C_DEVICE_FLAG(id) = 1;
}

void I2CDevices_ReportConfigError(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return;
    }

    ERROR_HANDLER_I2C_CONFIG_FLAG(id) = 1;
}

// Registers any bus-timing requirement `id`'s kind has with the I2C driver.
// Must run before the first transfer to that device -- including the verify
// below -- since the driver can only honour a requirement it knows about.
static void I2CDevices_ApplyBusTiming(I2C_DEVICE_ID id)
{
    switch (i2cDeviceKinds[id])
    {
        case I2C_DEVICE_KIND_BQ27441:
            I2C_SetDeviceBusFreeTime(i2cDeviceAddresses[id], BQ27441_BUS_FREE_TIME_US);
            break;

        default:
            // Every other kind on this board is satisfied by the I2C spec's
            // own t_BUF, which the gap between transactions already covers.
            break;
    }
}

// Dispatches the presence/identification check for `id` to its kind's driver.
static bool I2CDevices_Verify(I2C_DEVICE_ID id)
{
    switch (i2cDeviceKinds[id])
    {
        case I2C_DEVICE_KIND_MCP9804:
            return MCP9804_Verify(i2cDeviceAddresses[id]);

        case I2C_DEVICE_KIND_INA231A:
            return INA231A_Verify(i2cDeviceAddresses[id]);

        case I2C_DEVICE_KIND_DS1683:
            return DS1683_Verify(i2cDeviceAddresses[id]);

        case I2C_DEVICE_KIND_GT911:
            return GT911_Verify(i2cDeviceAddresses[id]);

        case I2C_DEVICE_KIND_BQ27441:
            return BQ27441_Verify(i2cDeviceAddresses[id]);

        default:
            return false;
    }
}

// Dispatches kind-specific one-time setup for `id`, called once presence is
// confirmed.
static bool I2CDevices_ConfigureOne(I2C_DEVICE_ID id)
{
    switch (i2cDeviceKinds[id])
    {
        // The MCP9804 needs no configuration, but shutdown is explicitly
        // CLEARED rather than assumed clear: I2CDevices_EnterLowPower()
        // parks every temp sensor in SHDN, and a warm reset (the sleep
        // path's wake IS one) doesn't power-cycle them. SHDN doesn't block
        // register reads, so without this the sensors verify fine and then
        // silently serve frozen temperatures all session.
        case I2C_DEVICE_KIND_MCP9804:
            return MCP9804_SetShutdown(i2cDeviceAddresses[id], false);

        case I2C_DEVICE_KIND_INA231A:
            return INA231A_Configure(i2cDeviceAddresses[id],
                                      INA231A_SHUNT_RESISTANCE_OHMS,
                                      INA231A_MAX_EXPECTED_CURRENT_AMPS,
                                      &i2cDeviceCurrentLSB[id]);

        case I2C_DEVICE_KIND_DS1683:
            return true;

        case I2C_DEVICE_KIND_GT911:
            return true;

        case I2C_DEVICE_KIND_BQ27441:
            return BQ27441_Configure(i2cDeviceAddresses[id], &batteryProfile);

        default:
            return false;
    }
}

uint8_t I2CDevices_EnterLowPower(void)
{
    uint8_t id;
    uint8_t count = 0;

    for (id = 0; id < I2C_DEVICE_COUNT; id++)
    {
        bool ok;

        if (!i2cDevicePresent[id])
        {
            continue;
        }

        switch (i2cDeviceKinds[id])
        {
            case I2C_DEVICE_KIND_MCP9804:
                ok = MCP9804_SetShutdown(i2cDeviceAddresses[id], true);
                break;

            case I2C_DEVICE_KIND_INA231A:
                ok = INA231A_SetPowerDown(i2cDeviceAddresses[id], true);
                break;

            // The DS1683 is deliberately left running: it is the board's
            // elapsed-time recorder, so stopping it would corrupt the very
            // thing it exists to measure, and it draws well under a
            // microamp anyway.
            case I2C_DEVICE_KIND_DS1683:
                ok = false;
                break;

            // Likewise the fuel gauge -- it has to keep coulomb counting and
            // taking OCV readings across sleep, which is the whole point of
            // the low-power mode existing.
            case I2C_DEVICE_KIND_BQ27441:
                ok = false;
                break;

            // The touch controller has no register-level low-power entry
            // here; enterLowPowerSleep() holds it in hardware reset instead.
            case I2C_DEVICE_KIND_GT911:
            default:
                ok = false;
                break;
        }

        if (ok)
        {
            count++;
        }
    }

    return count;
}

// Dispatches the full register status dump for `id` to its kind's driver.
static void I2CDevices_PrintOne(I2C_DEVICE_ID id)
{
    switch (i2cDeviceKinds[id])
    {
        case I2C_DEVICE_KIND_MCP9804:
            MCP9804_PrintStatus(i2cDeviceAddresses[id]);
            break;

        case I2C_DEVICE_KIND_INA231A:
            INA231A_PrintStatus(i2cDeviceAddresses[id]);
            break;

        case I2C_DEVICE_KIND_DS1683:
            DS1683_PrintStatus(i2cDeviceAddresses[id]);
            break;

        case I2C_DEVICE_KIND_GT911:
            GT911_PrintStatus(i2cDeviceAddresses[id]);
            break;

        case I2C_DEVICE_KIND_BQ27441:
            BQ27441_PrintStatus(i2cDeviceAddresses[id]);
            break;

        default:
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    Unknown device kind\n\r");
            break;
    }
}

bool I2CDevices_Initialize(void)
{
    bool allPresent = true;
    uint8_t id;

    for (id = 0; id < I2C_DEVICE_COUNT; id++)
    {
        bool ok;

        I2CDevices_ApplyBusTiming((I2C_DEVICE_ID)id);

        ok = I2CDevices_Verify((I2C_DEVICE_ID)id);

        i2cDevicePresent[id] = ok;

        // Record failure against this specific device's own flags (generated
        // from I2C_DEVICE_LIST -- see error_handler.h) rather than a single
        // flag shared by every I2C device on the board. Like every other
        // error_handler flag these only latch -- a device that inits fine
        // here does NOT clear a flag some earlier runtime read may have set,
        // since those flags are meant to survive warm resets.
        //
        // A verify failure and a configure failure go to DIFFERENT flags: a
        // device that identified correctly but whose setup sequence failed is
        // still fully reachable on the bus (and its telemetry reads will work
        // fine), so reporting that as an I2C error sends you hunting for a
        // wiring/bus problem that isn't there.
        if (!ok)
        {
            I2CDevices_ReportI2CError((I2C_DEVICE_ID)id);
        }
        // Only configure devices that are actually there -- e.g. writing an
        // INA231A calibration register to an address nothing ACKed is pointless.
        else if (!I2CDevices_ConfigureOne((I2C_DEVICE_ID)id))
        {
            I2CDevices_ReportConfigError((I2C_DEVICE_ID)id);
            ok = false;
        }

        allPresent = allPresent && ok;
    }

    return allPresent;
}

bool I2CDevices_IsPresent(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return false;
    }

    return i2cDevicePresent[id];
}

const char* I2CDevices_GetName(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return "Invalid Device ID";
    }

    return i2cDeviceNames[id];
}

uint16_t I2CDevices_GetAddress(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return 0;
    }

    return i2cDeviceAddresses[id];
}

const char* I2CDevices_GetRefdes(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return "Invalid Device ID";
    }

    return i2cDeviceRefdes[id];
}

void I2CDevices_PrintStatus(void)
{
    uint8_t id;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- I2C Devices ---\n\r");

    for (id = 0; id < I2C_DEVICE_COUNT; id++)
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
        printf("    %s (Refdes: %s)\n\r", i2cDeviceNames[id], i2cDeviceRefdes[id]);
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        I2CDevices_PrintOne((I2C_DEVICE_ID)id);
    }

    terminalTextAttributesReset();
}

bool I2CDevices_ReadTemperature(I2C_DEVICE_ID id, float *celsius)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_MCP9804))
    {
        return false;
    }

    if (!MCP9804_ReadTemperature(i2cDeviceAddresses[id], celsius))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadTemperatureWithStatus(I2C_DEVICE_ID id, I2C_DEVICE_TEMP_READING *reading)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_MCP9804))
    {
        reading->present = false;
        return false;
    }

    reading->present = MCP9804_ReadTemperatureAndStatus(i2cDeviceAddresses[id],
                                                          &reading->celsius, &reading->alerts);

    if (!reading->present)
    {
        I2CDevices_ReportI2CError(id);
    }

    return reading->present;
}

uint8_t I2CDevices_ReadAllTemperatures(I2C_DEVICE_TEMP_READING readings[I2C_DEVICE_COUNT])
{
    uint8_t successCount = 0;
    uint8_t id;

    for (id = 0; id < I2C_DEVICE_COUNT; id++)
    {
        if (I2CDevices_ReadTemperatureWithStatus((I2C_DEVICE_ID)id, &readings[id]))
        {
            successCount++;
        }
    }

    return successCount;
}

bool I2CDevices_ReadVoltage(I2C_DEVICE_ID id, float *volts)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    if (!INA231A_ReadBusVoltage(i2cDeviceAddresses[id], volts))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadCurrent(I2C_DEVICE_ID id, float *amps)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    if (!INA231A_ReadCurrent(i2cDeviceAddresses[id], i2cDeviceCurrentLSB[id], amps))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadPower(I2C_DEVICE_ID id, float *watts)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    if (!INA231A_ReadPower(i2cDeviceAddresses[id], i2cDeviceCurrentLSB[id], watts))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadElapsedSeconds(I2C_DEVICE_ID id, uint32_t *seconds)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_DS1683))
    {
        return false;
    }

    if (!DS1683_ReadElapsedSeconds(i2cDeviceAddresses[id], seconds))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadEventCount(I2C_DEVICE_ID id, uint16_t *count)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_DS1683))
    {
        return false;
    }

    if (!DS1683_ReadEventCount(i2cDeviceAddresses[id], count))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

static bool I2CDevices_IdIsKind(I2C_DEVICE_ID id, I2C_DEVICE_KIND kind)
{
    return I2CDevices_IdIsValid(id) && (i2cDeviceKinds[id] == kind);
}

bool I2CDevices_QueueTemperatureRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                     I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_MCP9804))
    {
        return false;
    }

    return MCP9804_QueueReadTemperature(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueVoltageRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                 I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    return INA231A_QueueReadBusVoltage(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueCurrentRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                 I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    return INA231A_QueueReadCurrent(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueuePowerRead(I2C_DEVICE_ID id, uint8_t raw[2],
                               I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    return INA231A_QueueReadPower(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_DecodeTemperature(I2C_DEVICE_ID id, const uint8_t raw[2],
                                  I2C_DEVICE_TEMP_READING *reading)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_MCP9804))
    {
        return false;
    }

    reading->present = true;
    reading->celsius = MCP9804_DecodeTemperatureRaw(raw);
    MCP9804_DecodeAlertFlagsRaw(raw, &reading->alerts);
    return true;
}

bool I2CDevices_DecodeVoltage(I2C_DEVICE_ID id, const uint8_t raw[2], float *volts)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    *volts = INA231A_DecodeBusVoltageRaw(raw);
    return true;
}

bool I2CDevices_DecodeCurrent(I2C_DEVICE_ID id, const uint8_t raw[2], float *amps)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    *amps = INA231A_DecodeCurrentRaw(raw, i2cDeviceCurrentLSB[id]);
    return true;
}

bool I2CDevices_DecodePower(I2C_DEVICE_ID id, const uint8_t raw[2], float *watts)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    *watts = INA231A_DecodePowerRaw(raw, i2cDeviceCurrentLSB[id]);
    return true;
}

// --- BQ27441 fuel gauge accessors -----------------------------------------

bool I2CDevices_ReadBatteryVoltage(I2C_DEVICE_ID id, float *volts)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    if (!BQ27441_ReadVoltage(i2cDeviceAddresses[id], volts))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadBatteryCurrent(I2C_DEVICE_ID id, float *amps)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    if (!BQ27441_ReadAverageCurrent(i2cDeviceAddresses[id], amps))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadBatteryTemperature(I2C_DEVICE_ID id, float *celsius)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    if (!BQ27441_ReadTemperature(i2cDeviceAddresses[id], celsius))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadBatteryStateOfCharge(I2C_DEVICE_ID id, uint8_t *percent)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    if (!BQ27441_ReadStateOfCharge(i2cDeviceAddresses[id], percent))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadBatteryStateOfHealth(I2C_DEVICE_ID id, uint8_t *percent)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    if (!BQ27441_ReadStateOfHealth(i2cDeviceAddresses[id], percent))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadBatteryRemainingCapacity(I2C_DEVICE_ID id, float *milliamphours)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    if (!BQ27441_ReadRemainingCapacity(i2cDeviceAddresses[id], milliamphours))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadBatteryFullChargeCapacity(I2C_DEVICE_ID id, float *milliamphours)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    if (!BQ27441_ReadFullChargeCapacity(i2cDeviceAddresses[id], milliamphours))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadBatteryFlags(I2C_DEVICE_ID id, I2C_DEVICE_BATTERY_FLAGS *flags)
{
    BQ27441_FLAG_STATUS status;

    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    if (!BQ27441_ReadFlags(i2cDeviceAddresses[id], &status))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    flags->overTemperature     = status.overTemperature;
    flags->underTemperature    = status.underTemperature;
    flags->fullyCharged        = status.fullyCharged;
    flags->fastChargingAllowed = status.fastChargingAllowed;
    flags->dischargeDetected   = status.dischargeDetected;
    flags->lowStateOfCharge    = status.lowStateOfCharge;
    return true;
}

bool I2CDevices_QueueBatteryVoltageRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                        I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    return BQ27441_QueueReadVoltage(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueBatteryCurrentRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                        I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    return BQ27441_QueueReadAverageCurrent(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueBatteryTemperatureRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                            I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    return BQ27441_QueueReadTemperature(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueBatteryStateOfChargeRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                              I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    return BQ27441_QueueReadStateOfCharge(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueBatteryStateOfHealthRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                              I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    return BQ27441_QueueReadStateOfHealth(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueBatteryRemainingCapacityRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                                  I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    return BQ27441_QueueReadRemainingCapacity(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueBatteryFullChargeCapacityRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                                   I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    return BQ27441_QueueReadFullChargeCapacity(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueBatteryFlagsRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                     I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    return BQ27441_QueueReadFlags(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_DecodeBatteryVoltage(I2C_DEVICE_ID id, const uint8_t raw[2], float *volts)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    *volts = BQ27441_DecodeVoltageRaw(raw);
    return true;
}

bool I2CDevices_DecodeBatteryCurrent(I2C_DEVICE_ID id, const uint8_t raw[2], float *amps)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    *amps = BQ27441_DecodeAverageCurrentRaw(raw);
    return true;
}

bool I2CDevices_DecodeBatteryTemperature(I2C_DEVICE_ID id, const uint8_t raw[2], float *celsius)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    *celsius = BQ27441_DecodeTemperatureRaw(raw);
    return true;
}

bool I2CDevices_DecodeBatteryStateOfCharge(I2C_DEVICE_ID id, const uint8_t raw[2], uint8_t *percent)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    *percent = BQ27441_DecodeStateOfChargeRaw(raw);
    return true;
}

bool I2CDevices_DecodeBatteryStateOfHealth(I2C_DEVICE_ID id, const uint8_t raw[2], uint8_t *percent)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    *percent = BQ27441_DecodeStateOfHealthRaw(raw);
    return true;
}

bool I2CDevices_DecodeBatteryRemainingCapacity(I2C_DEVICE_ID id, const uint8_t raw[2], float *milliamphours)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    *milliamphours = BQ27441_DecodeRemainingCapacityRaw(raw);
    return true;
}

bool I2CDevices_DecodeBatteryFullChargeCapacity(I2C_DEVICE_ID id, const uint8_t raw[2], float *milliamphours)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    *milliamphours = BQ27441_DecodeFullChargeCapacityRaw(raw);
    return true;
}

bool I2CDevices_DecodeBatteryFlags(I2C_DEVICE_ID id, const uint8_t raw[2], I2C_DEVICE_BATTERY_FLAGS *flags)
{
    BQ27441_FLAG_STATUS status;

    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_BQ27441))
    {
        return false;
    }

    BQ27441_DecodeFlagsRaw(raw, &status);

    flags->overTemperature     = status.overTemperature;
    flags->underTemperature    = status.underTemperature;
    flags->fullyCharged        = status.fullyCharged;
    flags->fastChargingAllowed = status.fastChargingAllowed;
    flags->dischargeDetected   = status.dischargeDetected;
    flags->lowStateOfCharge    = status.lowStateOfCharge;
    return true;
}
