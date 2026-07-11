/*******************************************************************************
  I2C1 Master Driver

  File Name:
    plib_i2c.c

  Summary:
    Interrupt-driven driver for the I2C1 peripheral in master mode.
*******************************************************************************/

#include "plib_i2c.h"
#include "32mzda_interrupt_control.h"
#include "device_control.h"
#include <xc.h>

#include <stdio.h>
#include <string.h>

#include "terminal_control.h"
#include "error_handler.h"

// These are macros needed for defining ISRs, included in XC32
#include <sys/attribs.h>

// *****************************************************************************
// Section: Internal State
// *****************************************************************************

typedef enum
{
    I2C_TRANSFER_TYPE_WRITE = 0,
    I2C_TRANSFER_TYPE_READ,
} I2C_TRANSFER_TYPE;

typedef enum
{
    I2C_STATE_ADDR_BYTE_1_SEND,
    I2C_STATE_ADDR_BYTE_2_SEND,
    I2C_STATE_READ_10BIT_MODE,
    I2C_STATE_ADDR_BYTE_1_SEND_10BIT_ONLY,
    I2C_STATE_WRITE,
    I2C_STATE_READ,
    I2C_STATE_READ_BYTE,
    I2C_STATE_WAIT_ACK_COMPLETE,
    I2C_STATE_WAIT_STOP_CONDITION_COMPLETE,
    I2C_STATE_IDLE,
} I2C_STATE;

typedef struct
{
    uint16_t            address;
    const uint8_t       *writeBuffer;
    uint8_t             *readBuffer;
    size_t              writeSize;
    size_t              readSize;
    size_t              writeCount;
    size_t              readCount;
    I2C_TRANSFER_TYPE   transferType;
    I2C_STATE           state;
    I2C_ERROR           error;
    I2C_CALLBACK        callback;
    uintptr_t           context;
} I2C_OBJ;

static volatile I2C_OBJ i2cObj;

// Bail out of a wedged transfer (e.g. a device stretching SCL forever)
// instead of spinning I2C_IsBusy() indefinitely.
#define I2C_TRANSACTION_TIMEOUT_US   50000u
#define I2C_TIMEOUT_TICKS            ((uint32_t)(((uint64_t)SYSCLK_INT / 2u) * I2C_TRANSACTION_TIMEOUT_US / 1000000u))

// I2C1 is clocked from PBCLK2, which PBCLK2Initialize() (device_control.c)
// divides down from SYSCLK by 3 (66.67 MHz on this board).
#define I2C_PBCLK_HZ                 (SYSCLK_INT / 3u)

// Pulse gobbler delay assumed by the I2CxBRG formula below; must match the
// constant used in I2C_TransferSetup() and I2C_Initialize()'s fixed BRG value.
#define I2C_PGD_DELAY_SEC            0.000000150

// *****************************************************************************
// Section: Interface Routines
// *****************************************************************************

bool I2C_Initialize(void)
{
    /* Disable the I2C Master interrupt */
    disableInterrupt(i2c1_host_event);

    /* Disable the I2C Bus collision interrupt */
    disableInterrupt(i2c1_bus_collision_event);

    // BRG = (Pbclk/2) * (1/Fscl - Tpgd) - 1, for Fscl = 100 kHz -> ~100.1 kHz actual
    I2C1BRG = 0x147;

    I2C1CONbits.SIDL = 0;
    // Slew rate control is only needed for 400 kHz/1 MHz; disable it for 100 kHz Standard mode
    I2C1CONbits.DISSLW = 1;
    I2C1CONbits.SMEN = 0;

    setInterruptPriority(i2c1_bus_collision_event, 4);
    setInterruptPriority(i2c1_host_event, 7);

    /* Clear master interrupt flag */
    clearInterruptFlag(i2c1_host_event);

    /* Clear fault interrupt flag */
    clearInterruptFlag(i2c1_bus_collision_event);

    /* Turn on the I2C module */
    I2C1CONbits.ON = 1;

    /* Set the initial state of the I2C state machine */
    i2cObj.state = I2C_STATE_IDLE;

    /* Report success if the I2C master module is enabled */
    return (I2C1CONbits.ON == 1);
}

/* I2C state machine */
static void I2C_TransferStateMachine(void)
{
    clearInterruptFlag(i2c1_host_event);

    switch (i2cObj.state)
    {
        case I2C_STATE_ADDR_BYTE_1_SEND:
            /* Is transmit buffer full? */
            if (!I2C1STATbits.TBF)
            {
                if (i2cObj.address > 0x007F)
                {
                    /* Transmit the MSB 2 bits of the 10-bit slave address, with R/W = 0 */
                    I2C1TRN = (0xF0 | (((uint8_t*)&i2cObj.address)[1] << 1));

                    i2cObj.state = I2C_STATE_ADDR_BYTE_2_SEND;
                }
                else
                {
                    /* 8-bit addressing mode */
                    I2C1TRN = ((i2cObj.address << 1) | i2cObj.transferType);

                    i2cObj.state = (i2cObj.transferType == I2C_TRANSFER_TYPE_WRITE)
                                       ? I2C_STATE_WRITE
                                       : I2C_STATE_READ;
                }
            }
            break;

        case I2C_STATE_ADDR_BYTE_2_SEND:
            /* Transmit the 2nd byte of the 10-bit slave address */
            if (!I2C1STATbits.ACKSTAT)
            {
                if (!I2C1STATbits.TBF)
                {
                    /* Transmit the remaining 8-bits of the 10-bit address */
                    I2C1TRN = i2cObj.address;

                    i2cObj.state = (i2cObj.transferType == I2C_TRANSFER_TYPE_WRITE)
                                       ? I2C_STATE_WRITE
                                       : I2C_STATE_READ_10BIT_MODE;
                }
            }
            else
            {
                /* NAK received. Generate Stop Condition. */
                i2cObj.error = I2C_ERROR_NACK;
                I2C1CONbits.PEN = 1;
                i2cObj.state = I2C_STATE_WAIT_STOP_CONDITION_COMPLETE;
            }
            break;

        case I2C_STATE_READ_10BIT_MODE:
            if (!I2C1STATbits.ACKSTAT)
            {
                /* Generate repeated start condition */
                I2C1CONbits.RSEN = 1;
                i2cObj.state = I2C_STATE_ADDR_BYTE_1_SEND_10BIT_ONLY;
            }
            else
            {
                /* NAK received. Generate Stop Condition. */
                i2cObj.error = I2C_ERROR_NACK;
                I2C1CONbits.PEN = 1;
                i2cObj.state = I2C_STATE_WAIT_STOP_CONDITION_COMPLETE;
            }
            break;

        case I2C_STATE_ADDR_BYTE_1_SEND_10BIT_ONLY:
            /* Is transmit buffer full? */
            if (!I2C1STATbits.TBF)
            {
                /* Transmit the first byte of the 10-bit slave address, with R/W = 1 */
                I2C1TRN = (0xF1 | ((((uint8_t*)&i2cObj.address)[1] << 1)));
                i2cObj.state = I2C_STATE_READ;
            }
            else
            {
                /* NAK received. Generate Stop Condition. */
                i2cObj.error = I2C_ERROR_NACK;
                I2C1CONbits.PEN = 1;
                i2cObj.state = I2C_STATE_WAIT_STOP_CONDITION_COMPLETE;
            }
            break;

        case I2C_STATE_WRITE:
            if (!I2C1STATbits.ACKSTAT)
            {
                /* ACK received */
                if (i2cObj.writeCount < i2cObj.writeSize)
                {
                    if (!I2C1STATbits.TBF)
                    {
                        /* Transmit the data from writeBuffer[] */
                        I2C1TRN = i2cObj.writeBuffer[i2cObj.writeCount++];
                    }
                }
                else if (i2cObj.readCount < i2cObj.readSize)
                {
                    /* Generate repeated start condition */
                    I2C1CONbits.RSEN = 1;

                    i2cObj.transferType = I2C_TRANSFER_TYPE_READ;

                    /* Send the I2C slave address with R/W = 1 */
                    i2cObj.state = (i2cObj.address > 0x007F)
                                       ? I2C_STATE_ADDR_BYTE_1_SEND_10BIT_ONLY
                                       : I2C_STATE_ADDR_BYTE_1_SEND;
                }
                else
                {
                    /* Transfer Complete. Generate Stop Condition */
                    I2C1CONbits.PEN = 1;
                    i2cObj.state = I2C_STATE_WAIT_STOP_CONDITION_COMPLETE;
                }
            }
            else
            {
                /* NAK received. Generate Stop Condition. */
                i2cObj.error = I2C_ERROR_NACK;
                I2C1CONbits.PEN = 1;
                i2cObj.state = I2C_STATE_WAIT_STOP_CONDITION_COMPLETE;
            }
            break;

        case I2C_STATE_READ:
            if (!I2C1STATbits.ACKSTAT)
            {
                /* Slave ACK'd the device address. Enable receiver. */
                I2C1CONbits.RCEN = 1;
                i2cObj.state = I2C_STATE_READ_BYTE;
            }
            else
            {
                /* NAK received. Generate Stop Condition. */
                i2cObj.error = I2C_ERROR_NACK;
                I2C1CONbits.PEN = 1;
                i2cObj.state = I2C_STATE_WAIT_STOP_CONDITION_COMPLETE;
            }
            break;

        case I2C_STATE_READ_BYTE:
            /* Data received from the slave */
            if (I2C1STATbits.RBF)
            {
                i2cObj.readBuffer[i2cObj.readCount++] = I2C1RCV;

                /* ACK unless this was the last byte, then NAK */
                I2C1CONbits.ACKDT = (i2cObj.readCount == i2cObj.readSize) ? 1 : 0;
                I2C1CONbits.ACKEN = 1;
                i2cObj.state = I2C_STATE_WAIT_ACK_COMPLETE;
            }
            break;

        case I2C_STATE_WAIT_ACK_COMPLETE:
            /* ACK or NAK sent to the I2C slave */
            if (i2cObj.readCount < i2cObj.readSize)
            {
                /* Enable receiver */
                I2C1CONbits.RCEN = 1;
                i2cObj.state = I2C_STATE_READ_BYTE;
            }
            else
            {
                /* Generate Stop Condition */
                I2C1CONbits.PEN = 1;
                i2cObj.state = I2C_STATE_WAIT_STOP_CONDITION_COMPLETE;
            }
            break;

        case I2C_STATE_WAIT_STOP_CONDITION_COMPLETE:
            i2cObj.state = I2C_STATE_IDLE;
            disableInterrupt(i2c1_host_event);
            disableInterrupt(i2c1_bus_collision_event);
            if (i2cObj.callback != NULL)
            {
                i2cObj.callback(i2cObj.context);
            }
            break;

        default:
            break;
    }
}

void I2C_CallbackRegister(I2C_CALLBACK callback, uintptr_t contextHandle)
{
    if (callback == NULL)
    {
        return;
    }

    i2cObj.callback = callback;
    i2cObj.context = contextHandle;
}

bool I2C_IsBusy(void)
{
    return (i2cObj.state != I2C_STATE_IDLE) || ((I2C1CON & 0x0000001F) != 0) ||
           I2C1STATbits.TRSTAT || I2C1STATbits.S;
}

bool I2C_ReadAsync(uint16_t address, uint8_t *rdata, size_t rlength)
{
    /* State machine must be idle and I2C module should not have detected a start bit on the bus */
    if ((i2cObj.state != I2C_STATE_IDLE) || I2C1STATbits.S)
    {
        return false;
    }

    i2cObj.address      = address;
    i2cObj.readBuffer   = rdata;
    i2cObj.readSize     = rlength;
    i2cObj.writeBuffer  = NULL;
    i2cObj.writeSize    = 0;
    i2cObj.writeCount   = 0;
    i2cObj.readCount    = 0;
    i2cObj.transferType = I2C_TRANSFER_TYPE_READ;
    i2cObj.error        = I2C_ERROR_NONE;
    i2cObj.state        = I2C_STATE_ADDR_BYTE_1_SEND;

    I2C1CONbits.SEN = 1;
    enableInterrupt(i2c1_host_event);
    enableInterrupt(i2c1_bus_collision_event);

    return true;
}

bool I2C_WriteAsync(uint16_t address, const uint8_t *wdata, size_t wlength)
{
    /* State machine must be idle and I2C module should not have detected a start bit on the bus */
    if ((i2cObj.state != I2C_STATE_IDLE) || I2C1STATbits.S)
    {
        return false;
    }

    i2cObj.address      = address;
    i2cObj.readBuffer   = NULL;
    i2cObj.readSize     = 0;
    i2cObj.writeBuffer  = wdata;
    i2cObj.writeSize    = wlength;
    i2cObj.writeCount   = 0;
    i2cObj.readCount    = 0;
    i2cObj.transferType = I2C_TRANSFER_TYPE_WRITE;
    i2cObj.error        = I2C_ERROR_NONE;
    i2cObj.state        = I2C_STATE_ADDR_BYTE_1_SEND;

    I2C1CONbits.SEN = 1;
    enableInterrupt(i2c1_host_event);
    enableInterrupt(i2c1_bus_collision_event);

    return true;
}

bool I2C_WriteReadAsync(uint16_t address, const uint8_t *wdata, size_t wlength, uint8_t *rdata, size_t rlength)
{
    /* State machine must be idle and I2C module should not have detected a start bit on the bus */
    if ((i2cObj.state != I2C_STATE_IDLE) || I2C1STATbits.S)
    {
        return false;
    }

    i2cObj.address      = address;
    i2cObj.readBuffer   = rdata;
    i2cObj.readSize     = rlength;
    i2cObj.writeBuffer  = wdata;
    i2cObj.writeSize    = wlength;
    i2cObj.writeCount   = 0;
    i2cObj.readCount    = 0;
    i2cObj.transferType = I2C_TRANSFER_TYPE_WRITE;
    i2cObj.error        = I2C_ERROR_NONE;
    i2cObj.state        = I2C_STATE_ADDR_BYTE_1_SEND;

    I2C1CONbits.SEN = 1;
    enableInterrupt(i2c1_host_event);
    enableInterrupt(i2c1_bus_collision_event);

    return true;
}

I2C_ERROR I2C_ErrorGet(void)
{
    I2C_ERROR error = i2cObj.error;
    i2cObj.error = I2C_ERROR_NONE;

    return error;
}

bool I2C_TransferSetup(I2C_TRANSFER_SETUP *setup, uint32_t srcClkFreq)
{
    uint32_t baudValue;
    uint32_t i2cClkSpeed;

    if (setup == NULL)
    {
        return false;
    }

    i2cClkSpeed = setup->clkSpeed;

    /* Maximum I2C clock speed cannot be greater than 1 MHz */
    if (i2cClkSpeed > 1000000)
    {
        return false;
    }

    if (srcClkFreq == 0)
    {
        srcClkFreq = 6666666UL;
    }

    baudValue = ((float)((float)srcClkFreq / 2.0) * (1 / (float)i2cClkSpeed - 0.000000150)) - 1;

    /* I2CxBRG value cannot be from 0 to 5 or more than the size of the baud rate register */
    if ((baudValue < 4) || (baudValue > 65555))
    {
        return false;
    }

    I2C1BRG = baudValue;

    /* Enable slew rate for 400 kHz clock speed; disable for all other speeds */
    I2C1CONbits.DISSLW = (i2cClkSpeed == 400000) ? 0 : 1;

    return true;
}

// Blocks until the in-flight transfer reaches idle, or aborts it after
// I2C_TRANSACTION_TIMEOUT_US if a device wedges the bus (e.g. holds SCL low).
static bool I2C_WaitForIdle(void)
{
    uint32_t start = _CP0_GET_COUNT();

    while (I2C_IsBusy())
    {
        if ((uint32_t)(_CP0_GET_COUNT() - start) >= I2C_TIMEOUT_TICKS)
        {
            disableInterrupt(i2c1_host_event);
            disableInterrupt(i2c1_bus_collision_event);
            i2cObj.state = I2C_STATE_IDLE;
            i2cObj.error = I2C_ERROR_TIMEOUT;
            return false;
        }
    }

    return true;
}

bool I2C_Write(uint16_t address, const uint8_t *data, size_t length)
{
    if (!I2C_WriteAsync(address, data, length) || !I2C_WaitForIdle())
    {
        return false;
    }

    return (I2C_ErrorGet() == I2C_ERROR_NONE);
}

bool I2C_Read(uint16_t address, uint8_t *data, size_t length)
{
    if (!I2C_ReadAsync(address, data, length) || !I2C_WaitForIdle())
    {
        return false;
    }

    return (I2C_ErrorGet() == I2C_ERROR_NONE);
}

bool I2C_WriteRead(uint16_t address, const uint8_t *wdata, size_t wlength, uint8_t *rdata, size_t rlength)
{
    if (!I2C_WriteReadAsync(address, wdata, wlength, rdata, rlength) || !I2C_WaitForIdle())
    {
        return false;
    }

    return (I2C_ErrorGet() == I2C_ERROR_NONE);
}

bool I2C_WriteRegister(uint16_t address, uint8_t reg, const uint8_t *data, size_t length)
{
    uint8_t buffer[I2C_REG_WRITE_MAX_PAYLOAD + 1];

    if ((length > I2C_REG_WRITE_MAX_PAYLOAD) || ((length != 0) && (data == NULL)))
    {
        i2cObj.error = I2C_ERROR_INVALID_PARAMETER;
        return false;
    }

    buffer[0] = reg;
    if (length != 0)
    {
        memcpy(&buffer[1], data, length);
    }

    return I2C_Write(address, buffer, length + 1);
}

bool I2C_ReadRegister(uint16_t address, uint8_t reg, uint8_t *data, size_t length)
{
    return I2C_WriteRead(address, &reg, 1, data, length);
}

void __ISR(_I2C1_BUS_VECTOR, IPL4SRS) I2C1_BusCollisionISR(void)
{
    /* Clear the bus collision error status bit */
    I2C1STATbits.BCL = 0;

    /* ACK the bus interrupt */
    clearInterruptFlag(i2c1_bus_collision_event);

    i2cObj.state = I2C_STATE_IDLE;
    i2cObj.error = I2C_ERROR_BUS_COLLISION;

    if (i2cObj.callback != NULL)
    {
        i2cObj.callback(i2cObj.context);
    }
}

void __ISR(_I2C1_MASTER_VECTOR, IPL7SRS) I2C1_MasterISR(void)
{
    I2C_TransferStateMachine();
}

uint32_t I2C_GetBusSpeed(void)
{
    // Inverts the I2CxBRG formula used by I2C_TransferSetup():
    //   BRG = (Pbclk/2) * (1/Fscl - Tpgd) - 1
    //   => Fscl = 1 / ( Tpgd + 2*(BRG+1)/Pbclk )
    double period = I2C_PGD_DELAY_SEC + (2.0 * ((double)I2C1BRG + 1.0)) / (double)I2C_PBCLK_HZ;

    return (uint32_t)(1.0 / period);
}

// Returns a short label for the nearest standard I2C bus speed, or "non-standard".
static const char* I2C_BusSpeedModeName(uint32_t speedHz)
{
    if (speedHz > 500000) return "Fast Mode Plus (~1 MHz)";
    if (speedHz > 150000) return "Fast Mode (~400 kHz)";
    if (speedHz > 50000)  return "Standard Mode (~100 kHz)";
    return "non-standard / very slow";
}

static const char* I2C_StateName(I2C_STATE state)
{
    switch (state)
    {
        case I2C_STATE_ADDR_BYTE_1_SEND:              return "ADDR_BYTE_1_SEND";
        case I2C_STATE_ADDR_BYTE_2_SEND:               return "ADDR_BYTE_2_SEND";
        case I2C_STATE_READ_10BIT_MODE:                return "READ_10BIT_MODE";
        case I2C_STATE_ADDR_BYTE_1_SEND_10BIT_ONLY:    return "ADDR_BYTE_1_SEND_10BIT_ONLY";
        case I2C_STATE_WRITE:                          return "WRITE";
        case I2C_STATE_READ:                           return "READ";
        case I2C_STATE_READ_BYTE:                      return "READ_BYTE";
        case I2C_STATE_WAIT_ACK_COMPLETE:              return "WAIT_ACK_COMPLETE";
        case I2C_STATE_WAIT_STOP_CONDITION_COMPLETE:   return "WAIT_STOP_CONDITION_COMPLETE";
        case I2C_STATE_IDLE:                           return "IDLE";
        default:                                        return "UNKNOWN";
    }
}

static const char* I2C_ErrorName(I2C_ERROR error)
{
    switch (error)
    {
        case I2C_ERROR_NONE:                return "None";
        case I2C_ERROR_NACK:                return "NACK";
        case I2C_ERROR_BUS_COLLISION:       return "Bus Collision";
        case I2C_ERROR_TIMEOUT:             return "Timeout";
        case I2C_ERROR_INVALID_PARAMETER:   return "Invalid Parameter";
        default:                             return "Unknown";
    }
}

// this function prints out status about the I2C module used in master mode
void I2C_PrintStatus(void)
{
    uint32_t busSpeed = I2C_GetBusSpeed();

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- Driver State ---\n\r");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    State machine is currently: %s\n\r", I2C_StateName(i2cObj.state));

    if (I2C_IsBusy()) terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Bus is currently: %s\n\r", I2C_IsBusy() ? "busy" : "idle");

    if (i2cObj.error != I2C_ERROR_NONE) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Last latched error: %s\n\r", I2C_ErrorName(i2cObj.error));

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- Bus Speed ---\n\r");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    PBCLK2 (I2C1 peripheral clock): %lu Hz\n\r", (unsigned long)I2C_PBCLK_HZ);
    printf("    I2C1BRG: 0x%04X (%u)\n\r", I2C1BRG, I2C1BRG);
    printf("    Calculated bus speed: %lu Hz (%s)\n\r", (unsigned long)busSpeed, I2C_BusSpeedModeName(busSpeed));

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- Control/Status Registers ---\n\r");

    // print I2CXCON bitfield
    if (I2C1CONbits.ON) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    I2C Master Module is %s\n\r", I2C1CONbits.ON ? "enabled" : "disabled");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    I2C SDA hold time set to %s\n\r", I2C1CONbits.SDAHT ? "500ns" : "100ns");

    if (I2C1CONbits.SIDL) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    I2C Master Module %s in Idle Mode\n\r", I2C1CONbits.SIDL ? "Disabled" : "Enabled");

    if (I2C1CONbits.STRICT) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Strict address enforcement is %s\n\r", I2C1CONbits.STRICT ? "enabled" : "disabled");

    if (I2C1CONbits.A10M) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    10 bit addressing is %s\n\r", I2C1CONbits.A10M ? "enabled" : "disabled");

    if (I2C1CONbits.DISSLW) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Drive strength slew rate control is %s\n\r", I2C1CONbits.DISSLW ? "disabled" : "enabled");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    I/O logic thresholds set to %s levels\n\r", I2C1CONbits.SMEN ? "SMBus" : "I2C");
    printf("    Next acknowledge sequence is a data %s\n\r", I2C1CONbits.ACKDT ? "NACK" : "ACK");

    if (I2C1CONbits.ACKEN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Acknowledge sequence is currently %s\n\r", I2C1CONbits.ACKEN ? "Enabled" : "Disabled");

    if (I2C1CONbits.RCEN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Master is currently %s\n\r", I2C1CONbits.RCEN ? "reading" : "writing");

    if (I2C1CONbits.PEN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Stop condition is currently %s\n\r", I2C1CONbits.PEN ? "enabled" : "disabled");

    if (I2C1CONbits.RSEN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Repeated start condition is %s\n\r", I2C1CONbits.RSEN ? "in progress" : "not in progress");

    if (I2C1CONbits.SEN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Start condition is currently %s\n\r", I2C1CONbits.SEN ? "in progress" : "not in progress");

    // Print out bitfield for I2CXSTAT register
    if (I2C1STATbits.ACKSTAT) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    %s received from slave\n\r", I2C1STATbits.ACKSTAT ? "NACK" : "ACK");

    if (I2C1STATbits.TRSTAT) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Master transmit is currently %s\n\r", I2C1STATbits.TRSTAT ? "in progress" : "not in progress");

    if (I2C1STATbits.BCL) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Bus collision %s\n\r", I2C1STATbits.BCL ? "detected" : "not detected");

    if (I2C1STATbits.ADD10) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    10 bit address %s\n\r", I2C1STATbits.ADD10 ? "matched" : "not matched");

    if (I2C1STATbits.IWCOL) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Write collision has %s\n\r", I2C1STATbits.IWCOL ? "occurred" : "not occurred");

    if (I2C1STATbits.I2COV) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Receive overflow has %s\n\r", I2C1STATbits.I2COV ? "occurred" : "not occurred");

    if (I2C1STATbits.P) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Stop bit was %s\n\r", I2C1STATbits.P ? "detected" : "not detected");

    if (I2C1STATbits.S) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Start or repeated start %s\n\r", I2C1STATbits.S ? "detected" : "not detected");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Master is currently %s\n\r", I2C1STATbits.R_W ? "reading" : "writing");

    if (I2C1STATbits.RBF) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Receive buffer is currently %s\n\r", I2C1STATbits.RBF ? "full" : "empty");

    if (I2C1STATbits.TBF) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Transmit buffer is currently %s\n\r", I2C1STATbits.TBF ? "full" : "empty");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    I2C Baud Rate Generator is set to 0x%04X\r\n", I2C1BRG);
    printf("    Current transmit buffer contents: 0x%02X\r\n", I2C1TRN);
    printf("    Current receive buffer contents: 0x%02X\r\n", I2C1RCV);

    terminalTextAttributesReset();
}
