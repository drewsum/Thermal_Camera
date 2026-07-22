/*******************************************************************************
  I2C1 Master Driver

  File Name:
    i2c_master.c

  Summary:
    Interrupt-driven, queued driver for the I2C1 peripheral in master mode.
    See i2c_master.h for the queueing model.
*******************************************************************************/

#include "i2c/i2c_master.h"
#include "core/32mzda_interrupt_control.h"
#include "core/device_control.h"
#include <xc.h>

#include <stdio.h>
#include <string.h>

#include "usb_uart/terminal_control.h"
#include "application/error_handler.h"

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
    I2C_STATE_WAIT_BUS_FREE,
    I2C_STATE_IDLE,
} I2C_STATE;

// Working state of the transaction currently on the bus. Loaded from the
// queue head by I2C_EngineStart() and advanced by the I2C1 master ISR.
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
} I2C_OBJ;

static volatile I2C_OBJ i2cObj;

// One queued transaction. Small write payloads (<= I2C_QUEUE_STAGED_MAX
// bytes) are copied into `staged` at enqueue time so the caller's buffer
// doesn't have to outlive the call; larger write buffers and all read
// buffers stay caller-owned until the callback fires.
typedef struct
{
    uint16_t              address;
    const uint8_t         *writeBuffer;
    uint8_t               *readBuffer;
    size_t                writeSize;
    size_t                readSize;
    uint8_t               staged[I2C_QUEUE_STAGED_MAX];
    I2C_TRANSFER_CALLBACK callback;
    uintptr_t             context;
} I2C_TRANSACTION;

// Must be a power of two: head/tail are free-running uint8_t indices and
// are reduced modulo the depth, so wraparound arithmetic stays correct.
#define I2C_QUEUE_DEPTH   32u

// Single-producer (thread context, with the I2C interrupts masked) /
// single-consumer (the I2C1 ISRs) FIFO. Slots [head, tail) are occupied;
// the head slot is the transaction currently on the bus.
static volatile I2C_TRANSACTION i2cQueue[I2C_QUEUE_DEPTH];
static volatile uint8_t i2cQueueHead;   // next transaction to execute/retire
static volatile uint8_t i2cQueueTail;   // next free slot

// CP0 count captured when the head transaction started, for I2C_Tasks()'s
// wedged-transfer watchdog.
static volatile uint32_t i2cTransferStartTick;

// CP0 count captured when the last stop condition completed, for the
// per-device bus-free timing below.
static volatile uint32_t i2cLastStopTick;

// Per-device minimum bus-free time (t_BUF), registered by address via
// I2C_SetDeviceBusFreeTime(). The I2C spec's own t_BUF (4.7us in Standard
// mode) is comfortably satisfied just by the instruction overhead between
// transactions, so this driver never needed to think about it -- but some
// devices demand far more than spec and silently misframe if they don't get
// it. The BQ27441-G1 fuel gauge is one: TI specifies t(BUF) >= 66us between
// all packets addressed to it, and below that it merges consecutive packets
// into one, taking the next packet's register-address byte as write data
// (measured on this board: reads of its block-data window came back one
// byte late until ~50us of gap was inserted).
//
// Devices with no registered requirement are unaffected -- they still chain
// straight through inside the ISR as before.
#define I2C_BUS_FREE_DEVICE_MAX   4u
#define I2C_BUS_FREE_TICKS(us)    ((uint32_t)(((uint64_t)(SYSCLK_INT / 2u) * (us)) / 1000000u))

static uint16_t i2cBusFreeAddress[I2C_BUS_FREE_DEVICE_MAX];
static uint32_t i2cBusFreeTicks[I2C_BUS_FREE_DEVICE_MAX];
static uint8_t  i2cBusFreeCount;

// Error latched by the most recently completed transaction (diagnostics /
// I2C_ErrorGet(); per-transaction code should use its callback's error).
static volatile I2C_ERROR i2cLastError;

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

    /* Both ISRs manipulate the transaction queue, so they must share a
       priority level (equal-priority interrupts cannot preempt each other) */
    setInterruptPriority(i2c1_bus_collision_event, 7);
    setInterruptPriority(i2c1_host_event, 7);

    /* Clear master interrupt flag */
    clearInterruptFlag(i2c1_host_event);

    /* Clear fault interrupt flag */
    clearInterruptFlag(i2c1_bus_collision_event);

    /* Turn on the I2C module */
    I2C1CONbits.ON = 1;

    /* Reset the transaction queue and the I2C state machine */
    i2cQueueHead = 0;
    i2cQueueTail = 0;
    i2cLastError = I2C_ERROR_NONE;
    i2cObj.state = I2C_STATE_IDLE;

    /* Report success if the I2C master module is enabled */
    return (I2C1CONbits.ON == 1);
}

// Loads the transaction at the queue head into the working transfer state
// and generates a start condition. The caller must guarantee the engine is
// idle, the queue is non-empty, and the I2C interrupts are masked (or it is
// running inside the I2C ISR itself); this re-enables both I2C interrupts.
static void I2C_EngineStart(void)
{
    volatile I2C_TRANSACTION *t = &i2cQueue[i2cQueueHead % I2C_QUEUE_DEPTH];

    i2cObj.address      = t->address;
    i2cObj.writeBuffer  = t->writeBuffer;
    i2cObj.readBuffer   = t->readBuffer;
    i2cObj.writeSize    = t->writeSize;
    i2cObj.readSize     = t->readSize;
    i2cObj.writeCount   = 0;
    i2cObj.readCount    = 0;
    i2cObj.transferType = (t->writeSize != 0) ? I2C_TRANSFER_TYPE_WRITE : I2C_TRANSFER_TYPE_READ;
    i2cObj.error        = I2C_ERROR_NONE;
    i2cObj.state        = I2C_STATE_ADDR_BYTE_1_SEND;

    i2cTransferStartTick = _CP0_GET_COUNT();

    I2C1CONbits.SEN = 1;
    enableInterrupt(i2c1_host_event);
    enableInterrupt(i2c1_bus_collision_event);
}

// Returns the bus-free time `address` requires, in CP0 ticks, or 0 if it
// has no registered requirement.
static uint32_t I2C_BusFreeTicksFor(uint16_t address)
{
    uint8_t i;

    for (i = 0; i < i2cBusFreeCount; i++)
    {
        if (i2cBusFreeAddress[i] == address)
        {
            return i2cBusFreeTicks[i];
        }
    }

    return 0;
}

// Starts the queue head, or parks the engine in I2C_STATE_WAIT_BUS_FREE if
// the head's device still owes bus-free time. Parking (rather than busy-
// waiting here) matters because two of the three callers run in the I2C ISR
// at IPL7: I2C_Tasks() picks a parked transaction back up from the main
// loop once the gap has elapsed. The caller must guarantee the engine is not
// mid-transfer, the queue is non-empty, and the I2C interrupts are masked.
static void I2C_StartOrDeferHead(void)
{
    uint32_t required = I2C_BusFreeTicksFor(i2cQueue[i2cQueueHead % I2C_QUEUE_DEPTH].address);

    if ((required != 0) && ((uint32_t)(_CP0_GET_COUNT() - i2cLastStopTick) < required))
    {
        i2cObj.state = I2C_STATE_WAIT_BUS_FREE;
        disableInterrupt(i2c1_host_event);
        disableInterrupt(i2c1_bus_collision_event);
        return;
    }

    I2C_EngineStart();   /* re-enables the I2C interrupts */
}

// Retires the head transaction with `error` and fires its callback. Must run
// with the I2C interrupts masked or from inside an I2C ISR. The engine state
// is NOT touched: the caller decides whether to chain, park, or idle.
static void I2C_RetireHeadTransaction(I2C_ERROR error)
{
    volatile I2C_TRANSACTION *t = &i2cQueue[i2cQueueHead % I2C_QUEUE_DEPTH];
    I2C_TRANSFER_CALLBACK callback = t->callback;
    uintptr_t context = t->context;

    i2cLastError = error;

    /* Retire before the callback so the callback may safely enqueue */
    i2cQueueHead++;

    if (callback != NULL)
    {
        callback(context, error);
    }
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
            /* Timestamp the stop before retiring, so the bus-free window is
               measured from the stop condition itself rather than from
               however long the completion callback happens to run */
            i2cLastStopTick = _CP0_GET_COUNT();

            I2C_RetireHeadTransaction(i2cObj.error);

            if (i2cQueueTail != i2cQueueHead)
            {
                /* Stop condition finished so the bus is idle: chain straight
                   into the next queued transaction without leaving the ISR,
                   unless that transaction's device owes bus-free time */
                I2C_StartOrDeferHead();
            }
            else
            {
                i2cObj.state = I2C_STATE_IDLE;
                disableInterrupt(i2c1_host_event);
                disableInterrupt(i2c1_bus_collision_event);
            }
            break;

        default:
            break;
    }
}

// Appends a transaction to the queue and starts the engine if it is idle.
// Callable from thread context or from a completion callback (I2C ISR).
static bool I2C_Enqueue(uint16_t address, const uint8_t *wdata, size_t wlength,
                        uint8_t *rdata, size_t rlength,
                        I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    bool queued = false;

    if (((wlength != 0) && (wdata == NULL)) ||
        ((rlength != 0) && (rdata == NULL)) ||
        ((wlength == 0) && (rlength == 0)))
    {
        i2cLastError = I2C_ERROR_INVALID_PARAMETER;
        return false;
    }

    /* Mask both I2C ISRs (the queue's only other user) while the indices and
       engine state are examined/updated. If an ISR is already executing we
       cannot be here (single core), so the state seen below is consistent. */
    disableInterrupt(i2c1_host_event);
    disableInterrupt(i2c1_bus_collision_event);

    if ((uint8_t)(i2cQueueTail - i2cQueueHead) < I2C_QUEUE_DEPTH)
    {
        volatile I2C_TRANSACTION *slot = &i2cQueue[i2cQueueTail % I2C_QUEUE_DEPTH];

        slot->address    = address;
        slot->readBuffer = rdata;
        slot->readSize   = rlength;
        slot->callback   = callback;
        slot->context    = context;

        if ((wlength != 0) && (wlength <= I2C_QUEUE_STAGED_MAX))
        {
            /* Small write payload: copy into the slot so the caller's buffer
               doesn't have to outlive this call */
            size_t i;

            for (i = 0; i < wlength; i++)
            {
                slot->staged[i] = wdata[i];
            }

            slot->writeBuffer = (const uint8_t *)slot->staged;
        }
        else
        {
            slot->writeBuffer = wdata;
        }
        slot->writeSize = wlength;

        i2cQueueTail++;
        queued = true;
    }
    else
    {
        i2cLastError = I2C_ERROR_QUEUE_FULL;
    }

    if (i2cObj.state == I2C_STATE_IDLE)
    {
        if (i2cQueueTail != i2cQueueHead)
        {
            I2C_StartOrDeferHead();   /* re-enables the I2C interrupts, or parks */
        }
        /* else: nothing queued; interrupts stay masked until the next start */
    }
    else if (i2cObj.state == I2C_STATE_WAIT_BUS_FREE)
    {
        /* Parked waiting out a device's bus-free time -- leave it parked with
           the interrupts masked; I2C_Tasks() starts it when the gap elapses */
    }
    else
    {
        /* A transfer is in flight -- restore its interrupts */
        enableInterrupt(i2c1_host_event);
        enableInterrupt(i2c1_bus_collision_event);
    }

    return queued;
}

bool I2C_QueueWrite(uint16_t address, const uint8_t *wdata, size_t wlength,
                    I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_Enqueue(address, wdata, wlength, NULL, 0, callback, context);
}

bool I2C_QueueRead(uint16_t address, uint8_t *rdata, size_t rlength,
                   I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_Enqueue(address, NULL, 0, rdata, rlength, callback, context);
}

bool I2C_QueueWriteRead(uint16_t address, const uint8_t *wdata, size_t wlength,
                        uint8_t *rdata, size_t rlength,
                        I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_Enqueue(address, wdata, wlength, rdata, rlength, callback, context);
}

bool I2C_QueueReadRegister(uint16_t address, uint8_t reg, uint8_t *rdata, size_t rlength,
                           I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    /* `reg` is staged into the queue slot (1 <= I2C_QUEUE_STAGED_MAX), so
       this stack variable's lifetime is fine */
    return I2C_Enqueue(address, &reg, 1, rdata, rlength, callback, context);
}

size_t I2C_QueuePendingCount(void)
{
    return (uint8_t)(i2cQueueTail - i2cQueueHead);
}

bool I2C_IsBusy(void)
{
    return (i2cObj.state != I2C_STATE_IDLE) || (i2cQueueTail != i2cQueueHead) ||
           ((I2C1CON & 0x0000001F) != 0) || I2C1STATbits.TRSTAT || I2C1STATbits.S;
}

I2C_ERROR I2C_ErrorGet(void)
{
    I2C_ERROR error = i2cLastError;
    i2cLastError = I2C_ERROR_NONE;

    return error;
}

void I2C_Tasks(void)
{
    disableInterrupt(i2c1_host_event);
    disableInterrupt(i2c1_bus_collision_event);

    if (i2cObj.state == I2C_STATE_WAIT_BUS_FREE)
    {
        /* Parked waiting out a device's bus-free time. Nothing is on the bus,
           so the wedged-transfer watchdog below must not see this state --
           start the transaction once the gap has elapsed. */
        if (i2cQueueTail != i2cQueueHead)
        {
            I2C_StartOrDeferHead();
        }
        else
        {
            i2cObj.state = I2C_STATE_IDLE;
        }
    }
    else if (i2cObj.state != I2C_STATE_IDLE)
    {
        if ((uint32_t)(_CP0_GET_COUNT() - i2cTransferStartTick) >= I2C_TIMEOUT_TICKS)
        {
            /* In-flight transaction wedged (e.g. a device stretching SCL
               forever): fail it and park the engine with the interrupts
               masked. The next I2C_Tasks() call or enqueue restarts the
               queue -- if the bus is still held, the peripheral flags a
               collision and that transaction fails too, so a dead bus
               drains the queue with errors instead of hanging the system. */
            I2C_RetireHeadTransaction(I2C_ERROR_TIMEOUT);
            i2cObj.state = I2C_STATE_IDLE;
        }
        else
        {
            /* Still within its time budget -- let it run */
            enableInterrupt(i2c1_host_event);
            enableInterrupt(i2c1_bus_collision_event);
        }
    }
    else if (i2cQueueTail != i2cQueueHead)
    {
        /* Engine parked after a collision/timeout with work still queued */
        I2C_StartOrDeferHead();
    }
    /* else: idle with an empty queue; interrupts stay masked */
}

bool I2C_SetDeviceBusFreeTime(uint16_t address, uint32_t microseconds)
{
    uint8_t i;

    for (i = 0; i < i2cBusFreeCount; i++)
    {
        if (i2cBusFreeAddress[i] == address)
        {
            i2cBusFreeTicks[i] = I2C_BUS_FREE_TICKS(microseconds);
            return true;
        }
    }

    if (i2cBusFreeCount >= I2C_BUS_FREE_DEVICE_MAX)
    {
        return false;
    }

    i2cBusFreeAddress[i2cBusFreeCount] = address;
    i2cBusFreeTicks[i2cBusFreeCount]   = I2C_BUS_FREE_TICKS(microseconds);
    i2cBusFreeCount++;

    return true;
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

// Completion state shared between a blocking wrapper (on the stack) and its
// transaction's callback (I2C ISR context).
typedef struct
{
    volatile bool      done;
    volatile I2C_ERROR error;
} I2C_SYNC_RESULT;

static void I2C_SyncCallback(uintptr_t context, I2C_ERROR error)
{
    I2C_SYNC_RESULT *result = (I2C_SYNC_RESULT *)context;

    result->error = error;
    result->done  = true;
}

// Queues a transfer and services the queue until that transfer completes.
// Transactions queued ahead of it finish (or are timed out by I2C_Tasks())
// first, so the wait is bounded even on a wedged bus: every head transaction
// either completes in the ISR or is retired with I2C_ERROR_TIMEOUT.
static bool I2C_TransferBlocking(uint16_t address, const uint8_t *wdata, size_t wlength,
                                 uint8_t *rdata, size_t rlength)
{
    I2C_SYNC_RESULT result = { false, I2C_ERROR_NONE };

    if (!I2C_Enqueue(address, wdata, wlength, rdata, rlength,
                     I2C_SyncCallback, (uintptr_t)&result))
    {
        return false;
    }

    while (!result.done)
    {
        I2C_Tasks();
    }

    return (result.error == I2C_ERROR_NONE);
}

bool I2C_Write(uint16_t address, const uint8_t *data, size_t length)
{
    return I2C_TransferBlocking(address, data, length, NULL, 0);
}

bool I2C_Read(uint16_t address, uint8_t *data, size_t length)
{
    return I2C_TransferBlocking(address, NULL, 0, data, length);
}

bool I2C_WriteRead(uint16_t address, const uint8_t *wdata, size_t wlength, uint8_t *rdata, size_t rlength)
{
    return I2C_TransferBlocking(address, wdata, wlength, rdata, rlength);
}

bool I2C_WriteRegister(uint16_t address, uint8_t reg, const uint8_t *data, size_t length)
{
    uint8_t buffer[I2C_REG_WRITE_MAX_PAYLOAD + 1];

    if ((length > I2C_REG_WRITE_MAX_PAYLOAD) || ((length != 0) && (data == NULL)))
    {
        i2cLastError = I2C_ERROR_INVALID_PARAMETER;
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

void __ISR(_I2C1_BUS_VECTOR, IPL7SRS) I2C1_BusCollisionISR(void)
{
    /* Clear the bus collision error status bit */
    I2C1STATbits.BCL = 0;

    /* ACK the bus interrupt */
    clearInterruptFlag(i2c1_bus_collision_event);

    /* Fail the in-flight transaction and park the engine with the interrupts
       masked; the next enqueue or I2C_Tasks() call restarts the queue once
       the bus has (hopefully) settled */
    if (i2cObj.state != I2C_STATE_IDLE)
    {
        I2C_RetireHeadTransaction(I2C_ERROR_BUS_COLLISION);
        i2cObj.state = I2C_STATE_IDLE;
    }
    else
    {
        i2cLastError = I2C_ERROR_BUS_COLLISION;
    }

    disableInterrupt(i2c1_host_event);
    disableInterrupt(i2c1_bus_collision_event);
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
        case I2C_ERROR_QUEUE_FULL:          return "Queue Full";
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
    printf("    Queued transactions: %u of %u\n\r",
           (unsigned)I2C_QueuePendingCount(), (unsigned)I2C_QUEUE_DEPTH);

    if (I2C_IsBusy()) terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Bus is currently: %s\n\r", I2C_IsBusy() ? "busy" : "idle");

    if (i2cLastError != I2C_ERROR_NONE) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Last latched error: %s\n\r", I2C_ErrorName(i2cLastError));

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
