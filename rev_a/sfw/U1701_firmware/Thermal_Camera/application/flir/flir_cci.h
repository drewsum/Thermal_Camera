/*******************************************************************************
  FLIR Lepton CCI (Command and Control Interface) Driver

  File Name:
    flir_cci.h

  Summary:
    I2C control-plane driver for the FLIR Lepton 3.5 thermal camera module
    (I2C1, 7-bit address 0x2A). Separate from the VoSPI video port
    (flir_vospi.c) -- this file only carries commands and status, never pixel
    data.

  Description:
    The Lepton CCI is a two-wire (I2C) register interface documented in FLIR
    document #110-0144-04 (Lepton Software Interface Description Document),
    referenced by a note on the schematic's FLIR_Lepton_Sensor sheet. Unlike
    the simple 8-bit-register devices in i2c/i2c_devices.c, the CCI uses
    16-bit register addresses and a command/status handshake:

      0x0002  STATUS        busy bit, boot bits, and an 8-bit command result
      0x0004  COMMAND       module|command|type word that triggers an action
      0x0006  DATA LENGTH   number of 16-bit words in the DATA registers
      0x0008  DATA 0..15    up to sixteen 16-bit parameter/result words
      0xF800  DATA block    larger transfers (unused here)

    Everything on the wire is big-endian (MSB first), including the register
    address. This driver builds those framed accesses on top of the generic
    blocking I2C_WriteRead()/I2C_Write() primitives in i2c/i2c_master.h -- no
    changes to the I2C master are needed.

    All functions here are blocking and must be called from thread context
    (they service the I2C queue and, for commands, poll the STATUS busy bit).
    The Lepton must be powered and out of reset first (application/flir/flir.c
    drives that sequence); calls made while it is unpowered simply NACK and
    return false.
*******************************************************************************/

#ifndef FLIR_CCI_H
#define FLIR_CCI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 7-bit I2C address of the Lepton CCI (schematic FLIR_Lepton_Sensor sheet).
#define FLIR_CCI_I2C_ADDRESS        0x2Au

// CCI register addresses (16-bit). See the file header for the layout.
#define FLIR_CCI_REG_STATUS         0x0002u
#define FLIR_CCI_REG_COMMAND        0x0004u
#define FLIR_CCI_REG_DATA_LENGTH    0x0006u
#define FLIR_CCI_REG_DATA_0         0x0008u

// STATUS register bit fields (register 0x0002).
#define FLIR_CCI_STATUS_BUSY_MASK        0x0001u   // 1 = a command is executing
#define FLIR_CCI_STATUS_BOOT_MODE_MASK   0x0002u   // 1 = booted from internal ROM
#define FLIR_CCI_STATUS_BOOT_STATUS_MASK 0x0004u   // 1 = boot sequence complete
// Bits 15:8 are a signed 8-bit command result code (LEP_RESULT, 0 == OK).
#define FLIR_CCI_STATUS_ERROR_SHIFT      8

// Command word encoding (register 0x0004): a module base (a multiple of 4)
// OR'd with a transaction type in the low two bits. Protected modules (OEM,
// RAD) additionally require the 0x4000 bit; none of those are used here.
#define FLIR_CCI_TYPE_GET   0x0u
#define FLIR_CCI_TYPE_SET   0x1u
#define FLIR_CCI_TYPE_RUN   0x2u

// Command bases (multiples of 4) used by this driver. Values per FLIR doc
// #110-0144-04; confirm against that document if the Lepton rejects a command
// (STATUS error code nonzero). GET = base, SET = base|1, RUN = base|2.
#define FLIR_CCI_CMD_SYS_PING             0x0200u  // RUN: liveness check
#define FLIR_CCI_CMD_SYS_AUX_TEMP_K       0x0210u  // GET: housing temp, K*100
#define FLIR_CCI_CMD_SYS_FPA_TEMP_K       0x0214u  // GET: sensor temp, K*100
#define FLIR_CCI_CMD_SYS_TELEMETRY_ENABLE 0x0218u  // GET/SET: 0 off, 1 on
#define FLIR_CCI_CMD_AGC_ENABLE           0x0100u  // GET/SET: 0 off, 1 on

// --- Raw 16-bit register access -------------------------------------------

// Reads/writes a single 16-bit CCI register. `reg` is one of the 0x0002.. or
// FLIR_CCI_REG_* addresses. Returns false on any I2C error (NACK/timeout).
bool FLIR_CCI_ReadReg16(uint16_t reg, uint16_t *value);
bool FLIR_CCI_WriteReg16(uint16_t reg, uint16_t value);

// --- Command handshake ----------------------------------------------------

// Reads the STATUS register and returns its raw value in *status. Convenience
// helpers decode the boot/busy bits and the command result code.
bool FLIR_CCI_ReadStatus(uint16_t *status);

// Blocks (bounded) until STATUS.BUSY clears, then returns the command result
// code (0 == success, negative == Lepton error). Returns false if the STATUS
// read itself failed or the busy wait timed out.
bool FLIR_CCI_WaitIdle(int8_t *resultCode);

// Runs a GET command: writes DATA LENGTH = `wordCount`, issues COMMAND
// (`commandBase | FLIR_CCI_TYPE_GET`), waits for idle, and reads `wordCount`
// result words into data[]. Returns false on I2C error, busy timeout, or a
// nonzero Lepton result code.
bool FLIR_CCI_GetAttribute(uint16_t commandBase, uint16_t *data, uint16_t wordCount);

// Runs a SET command: writes data[] into the DATA registers and DATA LENGTH,
// then issues COMMAND (`commandBase | FLIR_CCI_TYPE_SET`) and waits for idle.
bool FLIR_CCI_SetAttribute(uint16_t commandBase, const uint16_t *data, uint16_t wordCount);

// Runs a RUN command (no data): issues COMMAND (`commandBase |
// FLIR_CCI_TYPE_RUN`) and waits for idle.
bool FLIR_CCI_RunCommand(uint16_t commandBase);

// --- Higher-level helpers -------------------------------------------------

// Lightweight presence check used by the i2c_devices registry: reads STATUS
// and confirms the device ACKs. Does not require the camera to have finished
// booting. Returns false if the Lepton does not respond (e.g. unpowered).
bool FLIR_CCI_Verify(void);

// True once STATUS reports boot complete and not busy. Poll this after
// deasserting reset (the camera needs ~950ms to boot).
bool FLIR_CCI_IsBooted(void);

// Puts the Lepton into the video mode this project uses: AGC disabled and
// telemetry disabled, so VoSPI carries plain RAW14 frames the MCU processes
// itself (flir_process.c). Returns false if any CCI write failed.
bool FLIR_CCI_ConfigureRaw14Video(void);

// Reads the focal-plane-array (sensor) and AUX (housing) temperatures in
// degrees Celsius. Either pointer may be NULL. Returns false on CCI error.
bool FLIR_CCI_ReadTemperatures(float *fpaCelsius, float *auxCelsius);

// Prints the Lepton's CCI status (boot/busy bits, result code, temperatures,
// AGC/telemetry state) to the terminal. Backs the i2c_devices status dump and
// the FLIR status command.
void FLIR_CCI_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* FLIR_CCI_H */
