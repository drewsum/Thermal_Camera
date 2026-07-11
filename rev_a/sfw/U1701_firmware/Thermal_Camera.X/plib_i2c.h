/*******************************************************************************
  I2C1 Master Driver

  File Name:
    plib_i2c.h

  Summary:
    Interrupt-driven driver for the I2C1 peripheral in master mode.

  Description:
    I2C_ReadAsync/WriteAsync/WriteReadAsync are non-blocking: they start bus
    activity in the I2C1 interrupt and report completion through a
    registered callback. Built on top of those, I2C_Read/Write/WriteRead
    block (with a timeout) until the transfer finishes, and
    I2C_ReadRegister/WriteRegister add the usual register-address framing
    for simple I2C devices. Device drivers should generally use the
    Register-level functions; drop to the Async primitives only if a driver
    needs to do other work while a transfer is in flight.
*******************************************************************************/

#ifndef PLIB_I2C_H
#define PLIB_I2C_H

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
} I2C_ERROR;

typedef void (*I2C_CALLBACK)(uintptr_t contextHandle);

typedef struct
{
    uint32_t clkSpeed;
} I2C_TRANSFER_SETUP;

// Enables I2C1 and its interrupts. Must be called before any other I2C_* function.
bool I2C_Initialize(void);

// Reconfigures the bus clock speed (Hz). Do not call while I2C_IsBusy(). Pass
// srcClkFreq = 0 to use the default peripheral clock assumption.
bool I2C_TransferSetup(I2C_TRANSFER_SETUP *setup, uint32_t srcClkFreq);

// True while a transfer started with one of the *Async functions (or a
// blocking wrapper) is still in progress.
bool I2C_IsBusy(void);

// Returns the error latched by the most recently completed transfer, then clears it.
I2C_ERROR I2C_ErrorGet(void);

// Registers a callback fired (from interrupt context) when an *Async transfer completes.
void I2C_CallbackRegister(I2C_CALLBACK callback, uintptr_t contextHandle);

// --- Low-level async transfers ------------------------------------------
// Non-blocking: each call starts bus activity and returns immediately,
// returning false only if a transfer was already in progress. Completion
// (success or error) is reported through the registered callback and
// I2C_ErrorGet().

bool I2C_ReadAsync(uint16_t address, uint8_t *rdata, size_t rlength);
bool I2C_WriteAsync(uint16_t address, const uint8_t *wdata, size_t wlength);
bool I2C_WriteReadAsync(uint16_t address, const uint8_t *wdata, size_t wlength, uint8_t *rdata, size_t rlength);

// --- Blocking transfers (recommended default for device drivers) --------
// Each function starts the transfer and polls until it completes or times
// out. Returns true on success; on false, call I2C_ErrorGet() for the reason.

bool I2C_Write(uint16_t address, const uint8_t *data, size_t length);
bool I2C_Read(uint16_t address, uint8_t *data, size_t length);
bool I2C_WriteRead(uint16_t address, const uint8_t *wdata, size_t wlength, uint8_t *rdata, size_t rlength);

// --- Register-oriented helpers -------------------------------------------
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

// Prints I2C1 driver state, calculated bus speed, and controller/status
// register state to the terminal.
void I2C_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* PLIB_I2C_H */
