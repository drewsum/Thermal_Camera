
#include <xc.h>

// #include "application/error_handler.h"

#include "core/cause_of_reset.h"


// This function determines the cause of the most recent device reset and
// assigns it to the reset_cause enumeration
reset_cause_t getResetCause(void) {
 
    reset_cause_t reset_cause;

    // Deep Sleep exit, VBAT wake, and VBAT POR all re-arm a Power-on Reset on
    // this device, so they're checked ahead of the plain POR case below to
    // report the more specific cause instead of just "POR"
    if (RCONbits.DPSLP) {

        reset_cause = Deep_Sleep_Reset;
        RCONbits.DPSLP = 0;
        RCONbits.POR = 0;

    }

    else if (RCONbits.VBPOR) {

        reset_cause = VBAT_POR;
        RCONbits.VBPOR = 0;
        RCONbits.POR = 0;

    }

    else if (RCONbits.VBAT) {

        reset_cause = VBAT_Wake;
        RCONbits.VBAT = 0;
        RCONbits.POR = 0;

    }

    else if (RCONbits.POR) {

        reset_cause = POR_Reset;
        RCONbits.POR = 0;

    }

    else if (RCONbits.EXTR) {

        reset_cause = External_Reset;
        RCONbits.EXTR = 0;

    }

    else if (RCONbits.BOR) {

        reset_cause = BOR_Reset;
        RCONbits.BOR = 0;
        //error_handler.flags.vdd_brownout = 1;

        // HVDCORE also sets alongside BOR on this device (per silicon
        // errata) -- deliberately left uncleared here; hlvdCoreCheckAndClearEvent()
        // in hlvd.c owns reading/clearing that flag

    }
    
    else if (RCONbits.SWR) {
     
        reset_cause = Software_Reset;
        RCONbits.SWR = 0;
        
    }
    
    else if (RCONbits.CMR) {
     
        reset_cause = Config_Mismatch;
        RCONbits.CMR = 0;
        
    }
    
    else if (RCONbits.DMTO) {
     
        reset_cause = DMT_Reset;
        RCONbits.DMTO = 0;
        //error_handler.flags.DMT_timeout = 1;
        
    }
    
    else if (RCONbits.WDTO) {
     
        reset_cause = WDT_Reset;
        RCONbits.WDTO = 0;
        //error_handler.flags.WDT_timeout = 1;
        
    }
    
    else if (RCONbits.SLEEP) {
     
        reset_cause = Wake_From_Sleep;
        RCONbits.SLEEP = 0;
        
    }
    
    else if (RCONbits.IDLE) {
     
        reset_cause = Wake_From_Idle;
        RCONbits.IDLE = 0;
        
    }
    
    if (RCONbits.BCFGERR) {
    
        //error_handler.flags.configuration_error = 1;
        RCONbits.BCFGERR = 0;
        
    }
    
    if (RCONbits.BCFGFAIL) {
     
        //error_handler.flags.configuration_error = 1;
        RCONbits.BCFGFAIL = 0;
        
    }

    
    return reset_cause;
    
}

// This function returns a string describing the type of device reset that occurred
char * getResetCauseString(reset_cause_t input_cause) {
 
    static char *reset_descriptor_array[] = {
        
        "Undefined",
        "Primary Configuration Registers Error",
        "Primary/Secondary Configuration Registers Error",
        "Configuration Mismatch",
        "External Reset (Master clear)",
        "Software Reset",
        "Deadman Timer Reset",
        "Watchdog Timer Reset",
        "Wake from Sleep",
        "Wake from Idle",
        "Brown Out Reset",
        "Power On Reset",
        "Deep Sleep Exit",
        "VBAT Power-on Reset (VBAT missing/depleted, or first power-up)",
        "VBAT Mode Wake (VDD restored after running on VBAT battery backup)"

    };
    
    return reset_descriptor_array[input_cause];
    
}

