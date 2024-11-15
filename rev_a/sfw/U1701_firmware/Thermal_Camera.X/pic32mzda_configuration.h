
#ifndef _CONFIGURE_H    /* Guard against multiple inclusion */
#define _CONFIGURE_H


// PIC32MZ2064DAR176 Configuration Bit Settings

// 'C' source line config statements

// DEVCFG4
#pragma config SWDTPS = SPS1048576      // Sleep Mode Watchdog Timer Postscaler (1:1048576)

// DEVCFG3
#pragma config USERID = 0xFFFF          // Enter Hexadecimal value (Enter Hexadecimal value)
#pragma config FMIIEN = OFF             // Ethernet RMII/MII Enable (RMII Enabled)
#pragma config FETHIO = ON              // Ethernet I/O Pin Select (Default Ethernet I/O)
#pragma config PGL1WAY = ON             // Permission Group Lock One Way Configuration (Allow only one reconfiguration)
#pragma config PMDL1WAY = ON            // Peripheral Module Disable Configuration (Allow only one reconfiguration)
#pragma config IOL1WAY = ON             // Peripheral Pin Select Configuration (Allow only one reconfiguration)

// DEVCFG2
#pragma config FPLLIDIV = DIV_3         // System PLL Input Divider (3x Divider)
#pragma config FPLLRNG = RANGE_13_26_MHZ// System PLL Input Range (13-26 MHz Input)
#pragma config FPLLICLK = PLL_POSC      // System PLL Input Clock Selection (POSC is input to the System PLL)
#pragma config FPLLMULT = MUL_50        // System PLL Multiplier (PLL Multiply by 50)
#pragma config FPLLODIV = DIV_2         // System PLL Output Clock Divider (2x Divider)
#pragma config VBATBOREN = OFF          // VBAT BOR Enable (Disable ZPBOR during VBAT Mode)
#pragma config DSBOREN = ON             // Deep Sleep BOR Enable (Enable ZPBOR during Deep Sleep Mode)
#pragma config DSWDTPS = DSPS32         // Deep Sleep Watchdog Timer Postscaler (1:2^36)
#pragma config DSWDTOSC = SOSC          // Deep Sleep WDT Reference Clock Selection (Select SOSC as DSWDT Reference Clock)
#pragma config DSWDTEN = OFF            // Deep Sleep Watchdog Timer Enable (Disable DSWDT during Deep Sleep Mode)
#pragma config FDSEN = ON               // Deep Sleep Enable (Enable DSEN bit in DSCON)
#pragma config UPLLFSEL = FREQ_24MHZ    // USB PLL Input Frequency Selection (USB PLL input is 24 MHz)

// DEVCFG1
#pragma config FNOSC = SPLL             // Oscillator Selection Bits (System PLL)
#pragma config DMTINTV = WIN_127_128    // DMT Count Window Interval (Window/Interval value is 127/128 counter value)
#pragma config FSOSCEN = OFF            // Secondary Oscillator Enable (Disable Secondary Oscillator)
#pragma config IESO = OFF               // Internal/External Switch Over (Disabled)
#pragma config POSCMOD = EC             // Primary Oscillator Configuration (External clock mode)
#pragma config OSCIOFNC = OFF           // CLKO Output Signal Active on the OSCO Pin (Disabled)
#pragma config FCKSM = CSECME           // Clock Switching and Monitor Selection (Clock Switch Enabled, FSCM Enabled)
#pragma config WDTPS = PS8192           // Watchdog Timer Postscaler (1:8192)
#pragma config WDTSPGM = STOP           // Watchdog Timer Stop During Flash Programming (WDT stops during Flash programming)
#pragma config WINDIS = NORMAL          // Watchdog Timer Window Mode (Watchdog Timer is in non-Window mode)
#pragma config FWDTEN = OFF             // Watchdog Timer Enable (WDT Disabled)
#pragma config FWDTWINSZ = WINSZ_75     // Watchdog Timer Window Size (Window size is 75%)
#pragma config DMTCNT = DMT31           // Deadman Timer Count Selection (2^31 (2147483648))
#pragma config FDMTEN = OFF             // Deadman Timer Enable (Deadman Timer is disabled)

// DEVCFG0
#pragma config DEBUG = OFF              // Background Debugger Enable (Debugger is disabled)
#pragma config JTAGEN = OFF             // JTAG Enable (JTAG Disabled)
#pragma config ICESEL = ICS_PGx1        // ICE/ICD Comm Channel Select (Communicate on PGEC1/PGED1)
#pragma config TRCEN = OFF              // Trace Enable (Trace features in the CPU are disabled)
#pragma config BOOTISA = MIPS32         // Boot ISA Selection (Boot code and Exception code is MIPS32)
#pragma config FECCCON = OFF_LOCKED     // Dynamic Flash ECC Configuration (ECC and Dynamic ECC are disabled (ECCCON bits are locked))
#pragma config FSLEEP = OFF             // Flash Sleep Mode (Flash is powered down when the device is in Sleep mode)
#pragma config DBGPER = PG_ALL          // Debug Mode CPU Access Permission (Allow CPU access to all permission regions)
#pragma config SMCLR = MCLR_NORM        // Soft Master Clear Enable (MCLR pin generates a normal system Reset)
#pragma config SOSCGAIN = GAIN_2X       // Secondary Oscillator Gain Control bits (2x gain setting)
#pragma config SOSCBOOST = ON           // Secondary Oscillator Boost Kick Start Enable bit (Boost the kick start of the oscillator)
#pragma config POSCGAIN = GAIN_2X       // Primary Oscillator Gain Control bits (2x gain setting)
#pragma config POSCBOOST = ON           // Primary Oscillator Boost Kick Start Enable bit (Boost the kick start of the oscillator)
#pragma config POSCFGAIN = GAIN_G3      // Primary Crystal Oscillator Final Gain Control (Gain is G3)
#pragma config POSCTYPE = CRYSTAL_12MHZ // Primary Oscillator Type bits (12 MHz Crystal used as Primary Oscillator)
#pragma config POSCAGCRNG = RANGE_1X    // Primary Crystal Oscillator AGC Lock Range bit (Range 1x)
#pragma config POSCAGC = ON             // Primary Oscillator Auto Gain Control bit (POSC Auto Gain Control Enabled)
#pragma config EJTAGBEN = NORMAL        // EJTAG Boot Enable (Normal EJTAG functionality)

// DEVCP0
#pragma config CP = OFF                 // Code Protect (Protection Disabled)

// SEQ3
#pragma config TSEQ = 0xFFFF            // Boot Flash True Sequence Number (Enter Hexadecimal value)
#pragma config CSEQ = 0x0               // Boot Flash Complement Sequence Number (Enter Hexadecimal value)


// ADEVCFG4
#pragma config_alt SWDTPS = SPS1048576  // Sleep Mode Watchdog Timer Postscaler (1:1048576)

// ADEVCFG3
#pragma config_alt USERID = 0xFFFF      // Enter Hexadecimal value (Enter Hexadecimal value)
#pragma config_alt FMIIEN = OFF         // Ethernet RMII/MII Enable (RMII Enabled)
#pragma config_alt FETHIO = ON          // Ethernet I/O Pin Select (Default Ethernet I/O)
#pragma config_alt PGL1WAY = ON         // Permission Group Lock One Way Configuration (Allow only one reconfiguration)
#pragma config_alt PMDL1WAY = OFF       // Peripheral Module Disable Configuration (Allow multiple reconfigurations)
#pragma config_alt IOL1WAY = ON         // Peripheral Pin Select Configuration (Allow only one reconfiguration)

// ADEVCFG2
#pragma config_alt FPLLIDIV = DIV_2     // System PLL Input Divider (2x Divider)
#pragma config_alt FPLLRNG = RANGE_13_26_MHZ// System PLL Input Range (13-26 MHz Input)
#pragma config_alt FPLLICLK = PLL_POSC  // System PLL Input Clock Selection (POSC is input to the System PLL)
#pragma config_alt FPLLMULT = MUL_50    // System PLL Multiplier (PLL Multiply by 50)
#pragma config_alt FPLLODIV = DIV_2     // System PLL Output Clock Divider (2x Divider)
#pragma config_alt VBATBOREN = ON       // VBAT BOR Enable (Enable ZPBOR during VBAT Mode)
#pragma config_alt DSBOREN = ON         // Deep Sleep BOR Enable (Enable ZPBOR during Deep Sleep Mode)
#pragma config_alt DSWDTPS = DSPS32     // Deep Sleep Watchdog Timer Postscaler (1:2^36)
#pragma config_alt DSWDTOSC = LPRC      // Deep Sleep WDT Reference Clock Selection (Select LPRC as DSWDT Reference clock)
#pragma config_alt DSWDTEN = ON         // Deep Sleep Watchdog Timer Enable (Enable DSWDT during Deep Sleep Mode)
#pragma config_alt FDSEN = ON           // Deep Sleep Enable (Enable DSEN bit in DSCON)
#pragma config_alt UPLLFSEL = FREQ_24MHZ// USB PLL Input Frequency Selection (USB PLL input is 24 MHz)

// ADEVCFG1
#pragma config_alt FNOSC = FRCDIV       // Oscillator Selection Bits (Fast RC Osc w/Div-by-N (FRCDIV))
#pragma config_alt DMTINTV = WIN_127_128// DMT Count Window Interval (Window/Interval value is 127/128 counter value)
#pragma config_alt FSOSCEN = OFF        // Secondary Oscillator Enable (Disable Secondary Oscillator)
#pragma config_alt IESO = OFF           // Internal/External Switch Over (Disabled)
#pragma config_alt POSCMOD = EC         // Primary Oscillator Configuration (External clock mode)
#pragma config_alt OSCIOFNC = OFF       // CLKO Output Signal Active on the OSCO Pin (Disabled)
#pragma config_alt FCKSM = CSECME       // Clock Switching and Monitor Selection (Clock Switch Enabled, FSCM Enabled)
#pragma config_alt WDTPS = PS8192       // Watchdog Timer Postscaler (1:8192)
#pragma config_alt WDTSPGM = STOP       // Watchdog Timer Stop During Flash Programming (WDT stops during Flash programming)
#pragma config_alt WINDIS = NORMAL      // Watchdog Timer Window Mode (Watchdog Timer is in non-Window mode)
#pragma config_alt FWDTEN = OFF         // Watchdog Timer Enable (WDT Disabled)
#pragma config_alt FWDTWINSZ = WINSZ_75 // Watchdog Timer Window Size (Window size is 75%)
#pragma config_alt DMTCNT = DMT31       // Deadman Timer Count Selection (2^31 (2147483648))
#pragma config_alt FDMTEN = OFF         // Deadman Timer Enable (Deadman Timer is disabled)

// ADEVCFG0
#pragma config_alt DEBUG = OFF          // Background Debugger Enable (Debugger is disabled)
#pragma config_alt JTAGEN = OFF         // JTAG Enable (JTAG Disabled)
#pragma config_alt ICESEL = ICS_PGx1    // ICE/ICD Comm Channel Select (Communicate on PGEC1/PGED1)
#pragma config_alt TRCEN = OFF          // Trace Enable (Trace features in the CPU are disabled)
#pragma config_alt BOOTISA = MIPS32     // Boot ISA Selection (Boot code and Exception code is MIPS32)
#pragma config_alt FECCCON = OFF_LOCKED // Dynamic Flash ECC Configuration (ECC and Dynamic ECC are disabled (ECCCON bits are locked))
#pragma config_alt FSLEEP = OFF         // Flash Sleep Mode (Flash is powered down when the device is in Sleep mode)
#pragma config_alt DBGPER = PG_ALL      // Debug Mode CPU Access Permission (Allow CPU access to all permission regions)
#pragma config_alt SMCLR = MCLR_NORM    // Soft Master Clear Enable (MCLR pin generates a normal system Reset)
#pragma config_alt SOSCGAIN = GAIN_2X   // Secondary Oscillator Gain Control bits (2x gain setting)
#pragma config_alt SOSCBOOST = ON       // Secondary Oscillator Boost Kick Start Enable bit (Boost the kick start of the oscillator)
#pragma config_alt POSCGAIN = GAIN_2X   // Primary Oscillator Gain Control bits (2x gain setting)
#pragma config_alt POSCBOOST = ON       // Primary Oscillator Boost Kick Start Enable bit (Boost the kick start of the oscillator)
#pragma config_alt POSCFGAIN = GAIN_G3  // Primary Crystal Oscillator Final Gain Control (Gain is G3)
#pragma config_alt POSCTYPE = CRYSTAL_12MHZ// Primary Oscillator Type bits (12 MHz Crystal used as Primary Oscillator)
#pragma config_alt POSCAGCRNG = RANGE_1X// Primary Crystal Oscillator AGC Lock Range bit (Range 1x)
#pragma config_alt POSCAGC = ON         // Primary Oscillator Auto Gain Control bit (POSC Auto Gain Control Enabled)
#pragma config_alt EJTAGBEN = NORMAL    // EJTAG Boot Enable (Normal EJTAG functionality)

// ADEVCP0
#pragma config_alt CP = OFF             // Code Protect (Protection Disabled)

// ASEQ3
#pragma config_alt TSEQ = 0xFFFE        // Boot Flash True Sequence Number (Enter Hexadecimal value)
#pragma config_alt CSEQ = 0x1           // Boot Flash Complement Sequence Number (Enter Hexadecimal value)

// AUBADEVCFG4
#pragma config_auba SWDTPS = SPS1048576 // Sleep Mode Watchdog Timer Postscaler (1:1048576)

// AUBADEVCFG3
#pragma config_auba USERID = 0xFFFF     // Enter Hexadecimal value (Enter Hexadecimal value)
#pragma config_auba FMIIEN = ON         // Ethernet RMII/MII Enable (MII Enabled)
#pragma config_auba FETHIO = ON         // Ethernet I/O Pin Select (Default Ethernet I/O)
#pragma config_auba PGL1WAY = ON        // Permission Group Lock One Way Configuration (Allow only one reconfiguration)
#pragma config_auba PMDL1WAY = ON       // Peripheral Module Disable Configuration (Allow only one reconfiguration)
#pragma config_auba IOL1WAY = ON        // Peripheral Pin Select Configuration (Allow only one reconfiguration)

// AUBADEVCFG2
#pragma config_auba FPLLIDIV = DIV_8    // System PLL Input Divider (8x Divider)
#pragma config_auba FPLLRNG = RANGE_34_68_MHZ// System PLL Input Range (34-68 MHz Input)
#pragma config_auba FPLLICLK = PLL_FRC  // System PLL Input Clock Selection (FRC is input to the System PLL)
#pragma config_auba FPLLMULT = MUL_128  // System PLL Multiplier (PLL Multiply by 128)
#pragma config_auba FPLLODIV = DIV_32   // System PLL Output Clock Divider (32x Divider)
#pragma config_auba VBATBOREN = ON      // VBAT BOR Enable (Enable ZPBOR during VBAT Mode)
#pragma config_auba DSBOREN = ON        // Deep Sleep BOR Enable (Enable ZPBOR during Deep Sleep Mode)
#pragma config_auba DSWDTPS = DSPS32    // Deep Sleep Watchdog Timer Postscaler (1:2^36)
#pragma config_auba DSWDTOSC = LPRC     // Deep Sleep WDT Reference Clock Selection (Select LPRC as DSWDT Reference clock)
#pragma config_auba DSWDTEN = ON        // Deep Sleep Watchdog Timer Enable (Enable DSWDT during Deep Sleep Mode)
#pragma config_auba FDSEN = ON          // Deep Sleep Enable (Enable DSEN bit in DSCON)
#pragma config_auba UPLLFSEL = FREQ_24MHZ// USB PLL Input Frequency Selection (USB PLL input is 24 MHz)

// AUBADEVCFG1
#pragma config_auba FNOSC = FRCDIV      // Oscillator Selection Bits (Fast RC Osc w/Div-by-N (FRCDIV))
#pragma config_auba DMTINTV = WIN_127_128// DMT Count Window Interval (Window/Interval value is 127/128 counter value)
#pragma config_auba FSOSCEN = ON        // Secondary Oscillator Enable (Enable Secondary Oscillator)
#pragma config_auba IESO = ON           // Internal/External Switch Over (Enabled)
#pragma config_auba POSCMOD = OFF       // Primary Oscillator Configuration (Primary osc disabled)
#pragma config_auba OSCIOFNC = OFF      // CLKO Output Signal Active on the OSCO Pin (Disabled)
#pragma config_auba FCKSM = CSECME      // Clock Switching and Monitor Selection (Clock Switch Enabled, FSCM Enabled)
#pragma config_auba WDTPS = PS1048576   // Watchdog Timer Postscaler (1:1048576)
#pragma config_auba WDTSPGM = STOP      // Watchdog Timer Stop During Flash Programming (WDT stops during Flash programming)
#pragma config_auba WINDIS = NORMAL     // Watchdog Timer Window Mode (Watchdog Timer is in non-Window mode)
#pragma config_auba FWDTEN = ON         // Watchdog Timer Enable (WDT Enabled)
#pragma config_auba FWDTWINSZ = WINSZ_25// Watchdog Timer Window Size (Window size is 25%)
#pragma config_auba DMTCNT = DMT31      // Deadman Timer Count Selection (2^31 (2147483648))
#pragma config_auba FDMTEN = ON         // Deadman Timer Enable (Deadman Timer is enabled)

// AUBADEVCFG0
#pragma config_auba DEBUG = OFF         // Background Debugger Enable (Debugger is disabled)
#pragma config_auba JTAGEN = ON         // JTAG Enable (JTAG Port Enabled)
#pragma config_auba ICESEL = ICS_PGx1   // ICE/ICD Comm Channel Select (Communicate on PGEC1/PGED1)
#pragma config_auba TRCEN = ON          // Trace Enable (Trace features in the CPU are enabled)
#pragma config_auba BOOTISA = MIPS32    // Boot ISA Selection (Boot code and Exception code is MIPS32)
#pragma config_auba FECCCON = OFF_UNLOCKED// Dynamic Flash ECC Configuration (ECC and Dynamic ECC are disabled (ECCCON bits are writable))
#pragma config_auba FSLEEP = OFF        // Flash Sleep Mode (Flash is powered down when the device is in Sleep mode)
#pragma config_auba DBGPER = PG_ALL     // Debug Mode CPU Access Permission (Allow CPU access to all permission regions)
#pragma config_auba SMCLR = MCLR_NORM   // Soft Master Clear Enable (MCLR pin generates a normal system Reset)
#pragma config_auba SOSCGAIN = GAIN_2X  // Secondary Oscillator Gain Control bits (2x gain setting)
#pragma config_auba SOSCBOOST = ON      // Secondary Oscillator Boost Kick Start Enable bit (Boost the kick start of the oscillator)
#pragma config_auba POSCGAIN = GAIN_2X  // Primary Oscillator Gain Control bits (2x gain setting)
#pragma config_auba POSCBOOST = ON      // Primary Oscillator Boost Kick Start Enable bit (Boost the kick start of the oscillator)
#pragma config_auba POSCFGAIN = GAIN_G3 // Primary Crystal Oscillator Final Gain Control (Gain is G3)
#pragma config_auba POSCTYPE = CRYSTAL_12MHZ// Primary Oscillator Type bits (12 MHz Crystal used as Primary Oscillator)
#pragma config_auba POSCAGCRNG = RANGE_1X// Primary Crystal Oscillator AGC Lock Range bit (Range 1x)
#pragma config_auba POSCAGC = ON        // Primary Oscillator Auto Gain Control bit (POSC Auto Gain Control Enabled)
#pragma config_auba EJTAGBEN = NORMAL   // EJTAG Boot Enable (Normal EJTAG functionality)

// AUBADEVCP0
#pragma config_auba CP = OFF            // Code Protect (Protection Disabled)

// AUBASEQ3
#pragma config_auba TSEQ = 0xFFFD       // Boot Flash True Sequence Number (Enter Hexadecimal value)
#pragma config_auba CSEQ = 0x2          // Boot Flash Complement Sequence Number (Enter Hexadecimal value)


// UBADEVCFG4
#pragma config_uba SWDTPS = SPS1048576  // Sleep Mode Watchdog Timer Postscaler (1:1048576)

// UBADEVCFG3
#pragma config_uba USERID = 0xFFFF      // Enter Hexadecimal value (Enter Hexadecimal value)
#pragma config_uba FMIIEN = ON          // Ethernet RMII/MII Enable (MII Enabled)
#pragma config_uba FETHIO = ON          // Ethernet I/O Pin Select (Default Ethernet I/O)
#pragma config_uba PGL1WAY = ON         // Permission Group Lock One Way Configuration (Allow only one reconfiguration)
#pragma config_uba PMDL1WAY = ON        // Peripheral Module Disable Configuration (Allow only one reconfiguration)
#pragma config_uba IOL1WAY = ON         // Peripheral Pin Select Configuration (Allow only one reconfiguration)

// UBADEVCFG2
#pragma config_uba FPLLIDIV = DIV_8     // System PLL Input Divider (8x Divider)
#pragma config_uba FPLLRNG = RANGE_34_68_MHZ// System PLL Input Range (34-68 MHz Input)
#pragma config_uba FPLLICLK = PLL_FRC   // System PLL Input Clock Selection (FRC is input to the System PLL)
#pragma config_uba FPLLMULT = MUL_128   // System PLL Multiplier (PLL Multiply by 128)
#pragma config_uba FPLLODIV = DIV_32    // System PLL Output Clock Divider (32x Divider)
#pragma config_uba VBATBOREN = ON       // VBAT BOR Enable (Enable ZPBOR during VBAT Mode)
#pragma config_uba DSBOREN = ON         // Deep Sleep BOR Enable (Enable ZPBOR during Deep Sleep Mode)
#pragma config_uba DSWDTPS = DSPS32     // Deep Sleep Watchdog Timer Postscaler (1:2^36)
#pragma config_uba DSWDTOSC = LPRC      // Deep Sleep WDT Reference Clock Selection (Select LPRC as DSWDT Reference clock)
#pragma config_uba DSWDTEN = ON         // Deep Sleep Watchdog Timer Enable (Enable DSWDT during Deep Sleep Mode)
#pragma config_uba FDSEN = ON           // Deep Sleep Enable (Enable DSEN bit in DSCON)
#pragma config_uba UPLLFSEL = FREQ_24MHZ// USB PLL Input Frequency Selection (USB PLL input is 24 MHz)

// UBADEVCFG1
#pragma config_uba FNOSC = FRCDIV       // Oscillator Selection Bits (Fast RC Osc w/Div-by-N (FRCDIV))
#pragma config_uba DMTINTV = WIN_127_128// DMT Count Window Interval (Window/Interval value is 127/128 counter value)
#pragma config_uba FSOSCEN = ON         // Secondary Oscillator Enable (Enable Secondary Oscillator)
#pragma config_uba IESO = ON            // Internal/External Switch Over (Enabled)
#pragma config_uba POSCMOD = OFF        // Primary Oscillator Configuration (Primary osc disabled)
#pragma config_uba OSCIOFNC = OFF       // CLKO Output Signal Active on the OSCO Pin (Disabled)
#pragma config_uba FCKSM = CSECME       // Clock Switching and Monitor Selection (Clock Switch Enabled, FSCM Enabled)
#pragma config_uba WDTPS = PS1048576    // Watchdog Timer Postscaler (1:1048576)
#pragma config_uba WDTSPGM = STOP       // Watchdog Timer Stop During Flash Programming (WDT stops during Flash programming)
#pragma config_uba WINDIS = NORMAL      // Watchdog Timer Window Mode (Watchdog Timer is in non-Window mode)
#pragma config_uba FWDTEN = ON          // Watchdog Timer Enable (WDT Enabled)
#pragma config_uba FWDTWINSZ = WINSZ_25 // Watchdog Timer Window Size (Window size is 25%)
#pragma config_uba DMTCNT = DMT31       // Deadman Timer Count Selection (2^31 (2147483648))
#pragma config_uba FDMTEN = ON          // Deadman Timer Enable (Deadman Timer is enabled)

// UBADEVCFG0
#pragma config_uba DEBUG = OFF          // Background Debugger Enable (Debugger is disabled)
#pragma config_uba JTAGEN = ON          // JTAG Enable (JTAG Port Enabled)
#pragma config_uba ICESEL = ICS_PGx1    // ICE/ICD Comm Channel Select (Communicate on PGEC1/PGED1)
#pragma config_uba TRCEN = ON           // Trace Enable (Trace features in the CPU are enabled)
#pragma config_uba BOOTISA = MIPS32     // Boot ISA Selection (Boot code and Exception code is MIPS32)
#pragma config_uba FECCCON = OFF_UNLOCKED// Dynamic Flash ECC Configuration (ECC and Dynamic ECC are disabled (ECCCON bits are writable))
#pragma config_uba FSLEEP = OFF         // Flash Sleep Mode (Flash is powered down when the device is in Sleep mode)
#pragma config_uba DBGPER = PG_ALL      // Debug Mode CPU Access Permission (Allow CPU access to all permission regions)
#pragma config_uba SMCLR = MCLR_NORM    // Soft Master Clear Enable (MCLR pin generates a normal system Reset)
#pragma config_uba SOSCGAIN = GAIN_2X   // Secondary Oscillator Gain Control bits (2x gain setting)
#pragma config_uba SOSCBOOST = ON       // Secondary Oscillator Boost Kick Start Enable bit (Boost the kick start of the oscillator)
#pragma config_uba POSCGAIN = GAIN_2X   // Primary Oscillator Gain Control bits (2x gain setting)
#pragma config_uba POSCBOOST = ON       // Primary Oscillator Boost Kick Start Enable bit (Boost the kick start of the oscillator)
#pragma config_uba POSCFGAIN = GAIN_G3  // Primary Crystal Oscillator Final Gain Control (Gain is G3)
#pragma config_uba POSCTYPE = CRYSTAL_12MHZ// Primary Oscillator Type bits (12 MHz Crystal used as Primary Oscillator)
#pragma config_uba POSCAGCRNG = RANGE_1X// Primary Crystal Oscillator AGC Lock Range bit (Range 1x)
#pragma config_uba POSCAGC = ON         // Primary Oscillator Auto Gain Control bit (POSC Auto Gain Control Enabled)
#pragma config_uba EJTAGBEN = NORMAL    // EJTAG Boot Enable (Normal EJTAG functionality)

// UBADEVCP0
#pragma config_uba CP = OFF             // Code Protect (Protection Disabled)

// UBASEQ3
#pragma config_uba TSEQ = 0xFFFC        // Boot Flash True Sequence Number (Enter Hexadecimal value)
#pragma config_uba CSEQ = 0x3           // Boot Flash Complement Sequence Number (Enter Hexadecimal value)

#endif /* _CONFIGURE_H */

/* *****************************************************************************
 End of File
 */
