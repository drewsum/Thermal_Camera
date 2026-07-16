/*******************************************************************************
  I2C1 Master Driver

  File Name:
    i2c_master.h

  Summary:
    Interrupt-driven, queued driver for the I2C1 peripheral in master mode.

  Description:
    Transfers are described as transactions and placed in a FIFO queue. The
    I2C1 interrupt executes the queue back-to-back with no CPU involvement
    between bytes or between transactions; each transaction's completion
    (success or error) is reported through its own callback. I2C_Tasks()
    must be called periodically from the main loop -- it times out wedged
    transfers and restarts the queue after a bus error.

    The I2C_Queue* functions are fully non-blocking: they enqueue and return
    immediately. Built on top of those, I2C_Read/Write/WriteRead service the
    queue until their own transaction finishes, and I2C_ReadRegister/
    WriteRegister add the usual register-address framing for simple I2C
    devices. Use the Queue functions on periodic/hot paths (e.g. telemetry);
    the blocking helpers are fine for one-shot init/config/status code.
*******************************************************************************/

#ifndef I2C_MASTER_H
#define I2C_MASTER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    I2C_ERROR_NONE = 0,
    I2C_ERROR_NACK,
    I2C_ERROR_BUS_COLLISION,
    I2C_ERROR_TIMEOUT,
    I2C_ERROR_INVALID_PARAMETER,
    I2C_ERROR_QUEUE_FULL,
} I2C_ERROR;

// Per-transaction completion callback. Runs in I2C interrupt context: keep
// it short, integer-only (the IPL7 shadow register set does not save FPU
// state), and never call a blocking I2C_* function from it.
typedef void (*I2C_TRANSFER_CALLBACK)(uintptr_t context, I2C_ERROR error);

typedef struct
{
    uint32_t clkSpeed;
} I2C_TRANSFER_SETUP;

// Enables I2C1 and resets the transaction queue. Must be called before any
// other I2C_* function.
bool I2C_Initialize(void);

// Reconfigures the bus clock speed (Hz). Do not call while I2C_IsBusy(). Pass
// srcClkFreq = 0 to use the default peripheral clock assumption.
bool I2C_TransferSetup(I2C_TRANSFER_SETUP *setup, uint32_t srcClkFreq);

// True while any transaction is executing or queued.
bool I2C_IsBusy(void);

// Returns the error latched by the most recently completed transaction, then
// clears it. With multiple transactions in flight this is diagnostic only --
// code that needs the error for a specific transaction should take it from
// that transaction's callback argument.
I2C_ERROR I2C_ErrorGet(void);

// Services the transaction queue from thread context: aborts the in-flight
// transaction if it has been running longer than the driver timeout (wedged
// bus / clock-stretching device) and restarts the queue if it stalled after
// a bus collision or timeout. Call once per main-loop iteration.
void I2C_Tasks(void);

// Number of transactions currently queued (including the executing one).
size_t I2C_QueuePendingCount(void);

// --- Queued (non-blocking) transfers -------------------------------------
// Each call appends a transaction to the queue and returns immediately;
// false means the queue was full or a parameter was invalid (nothing was
// queued). `callback` (optional, may be NULL) fires from interrupt context
// when this transaction completes. Buffers must remain valid until the
// callback fires -- except small write payloads (<= I2C_QUEUE_STAGED_MAX
// bytes, e.g. QueueReadRegister's register byte), which are copied into the
// queue slot.

#define I2C_QUEUE_STAGED_MAX   4u

bool I2C_QueueWrite(uint16_t address, const uint8_t *wdata, size_t wlength,
                    I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2C_QueueRead(uint16_t address, uint8_t *rdata, size_t rlength,
                   I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2C_QueueWriteRead(uint16_t address, const uint8_t *wdata, size_t wlength,
                        uint8_t *rdata, size_t rlength,
                        I2C_TRANSFER_CALLBACK callback, uintptr_t context);

// Register-addressed read: writes `reg`, repeated-starts into a read of
// `rlength` bytes. `reg` is staged into the queue slot, so it need not
// outlive the call (rdata must, until the callback fires).
bool I2C_QueueReadRegister(uint16_t address, uint8_t reg, uint8_t *rdata, size_t rlength,
                           I2C_TRANSFER_CALLBACK callback, uintptr_t context);

// --- Blocking transfers (init/config/status paths) -----------------------
// Each function queues the transfer and services the queue until it
// completes or times out, so transactions queued ahead of it finish first.
// Returns true on success. Do not call from interrupt context.

bool I2C_Write(uint16_t address, const uint8_t *data, size_t length);
bool I2C_Read(uint16_t address, uint8_t *data, size_t length);
bool I2C_WriteRead(uint16_t address, const uint8_t *wdata, size_t wlength, uint8_t *rdata, size_t rlength);

// --- Register-oriented helpers (blocking) ---------------------------------
// For the common case of an 8-bit-register-addressed device: writes/reads
// `length` bytes starting at register `reg`, using a repeated start for the
// read. WriteRegister stages `reg` and `data` into a single write
// transaction and so accepts at most I2C_REG_WRITE_MAX_PAYLOAD data bytes.

#define I2C_REG_WRITE_MAX_PAYLOAD   32u

bool I2C_WriteRegister(uint16_t address, uint8_t reg, const uint8_t *data, size_t length);
bool I2C_ReadRegister(uint16_t address, uint8_t reg, uint8_t *data, size_t length);

// Returns the I2C1 bus clock speed (Hz) actually produced by the current
// I2C1BRG setting, computed from the peripheral clock feeding I2C1.
uint32_t I2C_GetBusSpeed(void);

// Prints I2C1 driver state, queue depth, calculated bus speed, and
// controller/status register state to the terminal.
void I2C_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_MASTER_H */
