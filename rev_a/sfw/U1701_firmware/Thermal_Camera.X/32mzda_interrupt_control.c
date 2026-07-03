
#include <xc.h>
#include <stdio.h>

#include "32mzda_interrupt_control.h"
#include "terminal_control.h"

// This function configures the system for multi-interrupt operation and
// assigns shadow registers sets to priority level ISRs
void interruptControllerInitialize(void) {
 
    // Enable multi-vector interrupt mode
    INTCONbits.MVEC = 1;
    
    // Assign shadow register sets to interrupt priorities
    // assign shadow set #7-#1 to priority level #7-#1 ISRs
    PRISS = 0x76543210;

}

// This function enables global interrupts
void enableGlobalInterrupts(void) {
 
    // This is built into the XC32 compiler
    __builtin_enable_interrupts();
    
}

// This function disables global interrupts
void disableGlobalInterrupts(void) {
 
    // This is built into the XC32 compiler
    __builtin_disable_interrupts();
    
}

// This function returns the state of global interrupt enable
// Returns 0 if global interrupts are disabled
// returns 1 if global interrupts are enabled
uint8_t getGlobalInterruptsState(void) {
 
    return __builtin_get_isr_state();
    
}

// This function explicitly sets the state of global interrupts
void setGlobalInterruptsState(uint8_t input_state) {

    __builtin_set_isr_state(input_state);
    
}


// This function allows for the setting/clearing of a given interrupt
// It manipulates that interrupt's 'Interrupt Enable' bit
void setInterruptEnable(interrupt_source_t input_interrupt, uint8_t input_state) {

    // Decide which interrupt control bits to manipulate based on which interrupt
    // is being enabled or disabled
    switch (input_interrupt) {
            
        case core_timer_interrupt:
            IEC0bits.CTIE = input_state;
            break;

        case core_software_interrupt_0:
            IEC0bits.CS0IE = input_state;
            break;

        case core_software_interrupt_1:
            IEC0bits.CS1IE = input_state;
            break;

        case external_interrupt_0:
            IEC0bits.INT0IE = input_state;
            break;

        case timer1:
            IEC0bits.T1IE = input_state;
            break;

        case input_capture_1_error:
            IEC0bits.IC1EIE = input_state;
            break;

        case input_capture_1:
            IEC0bits.IC1IE = input_state;
            break;

        case output_compare_1:
            IEC0bits.OC1IE = input_state;
            break;

        case external_interrupt_1:
            IEC0bits.INT1IE = input_state;
            break;

        case timer2:
            IEC0bits.T2IE = input_state;
            break;

        case input_capture_2_error:
            IEC0bits.IC2EIE = input_state;
            break;

        case input_capture_2:
            IEC0bits.IC2IE = input_state;
            break;

        case output_compare_2:
            IEC0bits.OC2IE = input_state;
            break;

        case external_interrupt_2:
            IEC0bits.INT2IE = input_state;
            break;

        case timer3:
            IEC0bits.T3IE = input_state;
            break;

        case input_capture_3_error:
            IEC0bits.IC3EIE = input_state;
            break;

        case input_capture_3:
            IEC0bits.IC3IE = input_state;
            break;

        case output_compare_3:
            IEC0bits.OC3IE = input_state;
            break;

        case external_interrupt_3:
            IEC0bits.INT3IE = input_state;
            break;

        case timer4:
            IEC0bits.T4IE = input_state;
            break;

        case input_capture_4_error:
            IEC0bits.IC4EIE = input_state;
            break;

        case input_capture_4:
            IEC0bits.IC4IE = input_state;
            break;

        case output_compare_4:
            IEC0bits.OC4IE = input_state;
            break;

        case external_interrupt_4:
            IEC0bits.INT4IE = input_state;
            break;

        case timer5:
            IEC0bits.T5IE = input_state;
            break;

        case input_capture_5_error:
            IEC0bits.IC5EIE = input_state;
            break;

        case input_capture_5:
            IEC0bits.IC5IE = input_state;
            break;

        case output_compare_5:
            IEC0bits.OC5IE = input_state;
            break;

        case timer6:
            IEC0bits.T6IE = input_state;
            break;

        case input_capture_6_error:
            IEC0bits.IC6EIE = input_state;
            break;

        case input_capture_6:
            IEC0bits.IC6IE = input_state;
            break;

        case output_compare_6:
            IEC0bits.OC6IE = input_state;
            break;

        case timer7:
            IEC1bits.T7IE = input_state;
            break;

        case input_capture_7_error:
            IEC1bits.IC7EIE = input_state;
            break;

        case input_capture_7:
            IEC1bits.IC7IE = input_state;
            break;

        case output_compare_7:
            IEC1bits.OC7IE = input_state;
            break;

        case timer8:
            IEC1bits.T8IE = input_state;
            break;

        case input_capture_8_error:
            IEC1bits.IC8EIE = input_state;
            break;

        case input_capture_8:
            IEC1bits.IC8IE = input_state;
            break;

        case output_compare_8:
            IEC1bits.OC8IE = input_state;
            break;

        case timer9:
            IEC1bits.T9IE = input_state;
            break;

        case input_capture_9_error:
            IEC1bits.IC9EIE = input_state;
            break;

        case input_capture_9:
            IEC1bits.IC9IE = input_state;
            break;

        case output_compare_9:
            IEC1bits.OC9IE = input_state;
            break;

        case adc_global_interrupt:
            IEC1bits.ADCIE = input_state;
            break;

        case adc_fifo_interrupt:
            IEC1bits.ADCFIFOIE = input_state;
            break;

        case adc_digital_comparator_1:
            IEC1bits.ADCDC1IE = input_state;
            break;

        case adc_digital_comparator_2:
            IEC1bits.ADCDC2IE = input_state;
            break;

        case adc_digital_comparator_3:
            IEC1bits.ADCDC3IE = input_state;
            break;

        case adc_digital_comparator_4:
            IEC1bits.ADCDC4IE = input_state;
            break;

        case adc_digital_comparator_5:
            IEC1bits.ADCDC5IE = input_state;
            break;

        case adc_digital_comparator_6:
            IEC1bits.ADCDC6IE = input_state;
            break;

        case adc_digital_filter_1:
            IEC1bits.ADCDF1IE = input_state;
            break;

        case adc_digital_filter_2:
            IEC1bits.ADCDF2IE = input_state;
            break;

        case adc_digital_filter_3:
            IEC1bits.ADCDF3IE = input_state;
            break;

        case adc_digital_filter_4:
            IEC1bits.ADCDF4IE = input_state;
            break;

        case adc_digital_filter_5:
            IEC1bits.ADCDF5IE = input_state;
            break;

        case adc_digital_filter_6:
            IEC1bits.ADCDF6IE = input_state;
            break;

        case adc_fault:
            IEC1bits.ADCFLTIE = input_state;
            break;

        case adc_data_0:
            IEC1bits.ADCD0IE = input_state;
            break;

        case adc_data_1:
            IEC1bits.ADCD1IE = input_state;
            break;

        case adc_data_2:
            IEC1bits.ADCD2IE = input_state;
            break;

        case adc_data_3:
            IEC1bits.ADCD3IE = input_state;
            break;

        case adc_data_4:
            IEC1bits.ADCD4IE = input_state;
            break;

        case adc_data_5:
            IEC2bits.ADCD5IE = input_state;
            break;

        case adc_data_6:
            IEC2bits.ADCD6IE = input_state;
            break;

        case adc_data_7:
            IEC2bits.ADCD7IE = input_state;
            break;

        case adc_data_8:
            IEC2bits.ADCD8IE = input_state;
            break;

        case adc_data_9:
            IEC2bits.ADCD9IE = input_state;
            break;

        case adc_data_10:
            IEC2bits.ADCD10IE = input_state;
            break;

        case adc_data_11:
            IEC2bits.ADCD11IE = input_state;
            break;

        case adc_data_12:
            IEC2bits.ADCD12IE = input_state;
            break;

        case adc_data_13:
            IEC2bits.ADCD13IE = input_state;
            break;

        case adc_data_14:
            IEC2bits.ADCD14IE = input_state;
            break;

        case adc_data_15:
            IEC2bits.ADCD15IE = input_state;
            break;

        case adc_data_16:
            IEC2bits.ADCD16IE = input_state;
            break;

        case adc_data_17:
            IEC2bits.ADCD17IE = input_state;
            break;

        case adc_data_18:
            IEC2bits.ADCD18IE = input_state;
            break;

        case adc_data_19:
            IEC2bits.ADCD19IE = input_state;
            break;

        case adc_data_20:
            IEC2bits.ADCD20IE = input_state;
            break;

        case adc_data_21:
            IEC2bits.ADCD21IE = input_state;
            break;

        case adc_data_22:
            IEC2bits.ADCD22IE = input_state;
            break;

        case adc_data_23:
            IEC2bits.ADCD23IE = input_state;
            break;

        case adc_data_24:
            IEC2bits.ADCD24IE = input_state;
            break;

        case adc_data_25:
            IEC2bits.ADCD25IE = input_state;
            break;

        case adc_data_26:
            IEC2bits.ADCD26IE = input_state;
            break;

        case adc_data_27:
            IEC2bits.ADCD27IE = input_state;
            break;

        case adc_data_28:
            IEC2bits.ADCD28IE = input_state;
            break;

        case adc_data_29:
            IEC2bits.ADCD29IE = input_state;
            break;

        case adc_data_30:
            IEC2bits.ADCD30IE = input_state;
            break;

        case adc_data_31:
            IEC2bits.ADCD31IE = input_state;
            break;

        case adc_data_32:
            IEC2bits.ADCD32IE = input_state;
            break;

        case adc_data_33:
            IEC2bits.ADCD33IE = input_state;
            break;

        case adc_data_34:
            IEC2bits.ADCD34IE = input_state;
            break;

        case adc_data_35:
            IEC2bits.ADCD35IE = input_state;
            break;

        case adc_data_36:
            IEC2bits.ADCD36IE = input_state;
            break;

        case adc_data_37:
            IEC3bits.ADCD37IE = input_state;
            break;

        case adc_data_38:
            IEC3bits.ADCD38IE = input_state;
            break;

        case adc_data_39:
            IEC3bits.ADCD39IE = input_state;
            break;

        case adc_data_40:
            IEC3bits.ADCD40IE = input_state;
            break;

        case adc_data_41:
            IEC3bits.ADCD41IE = input_state;
            break;

        case adc_data_42:
            IEC3bits.ADCD42IE = input_state;
            break;

        case adc_data_43:
            IEC3bits.ADCD43IE = input_state;
            break;

        case usb_suspend_resume_event:
            IEC3bits.USBSRIE = input_state;
            break;

        case core_performance_counter_interrupt:
            IEC3bits.CPCIE = input_state;
            break;

        case core_fast_debug_channel_interrupt:
            IEC3bits.CFDCIE = input_state;
            break;

        case system_bus_protection_violation:
            IEC3bits.SBIE = input_state;
            break;

        case spi1_fault:
            IEC3bits.SPI1EIE = input_state;
            break;

        case spi1_receive_done:
            IEC3bits.SPI1RXIE = input_state;
            break;

        case spi1_transfer_done:
            IEC3bits.SPI1TXIE = input_state;
            break;

        case uart1_fault:
            IEC3bits.U1EIE = input_state;
            break;

        case uart1_receive_done:
            IEC3bits.U1RXIE = input_state;
            break;

        case uart1_transfer_done:
            IEC3bits.U1TXIE = input_state;
            break;

        case i2c1_bus_collision_event:
            IEC3bits.I2C1BIE = input_state;
            break;

        case i2c1_client_event:
            IEC3bits.I2C1SIE = input_state;
            break;

        case i2c1_host_event:
            IEC3bits.I2C1MIE = input_state;
            break;

        case porta_input_change_interrupt:
            IEC3bits.CNAIE = input_state;
            break;

        case portb_input_change_interrupt:
            IEC3bits.CNBIE = input_state;
            break;

        case portc_input_change_interrupt:
            IEC3bits.CNCIE = input_state;
            break;

        case portd_input_change_interrupt:
            IEC3bits.CNDIE = input_state;
            break;

        case porte_input_change_interrupt:
            IEC3bits.CNEIE = input_state;
            break;

        case portf_input_change_interrupt:
            IEC3bits.CNFIE = input_state;
            break;

        case portg_input_change_interrupt:
            IEC3bits.CNGIE = input_state;
            break;

        case porth_input_change_interrupt:
            IEC3bits.CNHIE = input_state;
            break;

        case portj_input_change_interrupt:
            IEC3bits.CNJIE = input_state;
            break;

        case portk_input_change_interrupt:
            IEC3bits.CNKIE = input_state;
            break;

        case pmp:
            IEC4bits.PMPIE = input_state;
            break;

        case pmp_error:
            IEC4bits.PMPEIE = input_state;
            break;

        case comparator_1_interrupt:
            IEC4bits.CMP1IE = input_state;
            break;

        case comparator_2_interrupt:
            IEC4bits.CMP2IE = input_state;
            break;

        case usb_general_event:
            IEC4bits.USBIE = input_state;
            break;

        case usb_dma_event:
            IEC4bits.USBDMAIE = input_state;
            break;

        case dma_channel_0:
            IEC4bits.DMA0IE = input_state;
            break;

        case dma_channel_1:
            IEC4bits.DMA1IE = input_state;
            break;

        case dma_channel_2:
            IEC4bits.DMA2IE = input_state;
            break;

        case dma_channel_3:
            IEC4bits.DMA3IE = input_state;
            break;

        case dma_channel_4:
            IEC4bits.DMA4IE = input_state;
            break;

        case dma_channel_5:
            IEC4bits.DMA5IE = input_state;
            break;

        case dma_channel_6:
            IEC4bits.DMA6IE = input_state;
            break;

        case dma_channel_7:
            IEC4bits.DMA7IE = input_state;
            break;

        case spi2_fault:
            IEC4bits.SPI2EIE = input_state;
            break;

        case spi2_receive_done:
            IEC4bits.SPI2RXIE = input_state;
            break;

        case spi2_transfer_done:
            IEC4bits.SPI2TXIE = input_state;
            break;

        case uart2_fault:
            IEC4bits.U2EIE = input_state;
            break;

        case uart2_receive_done:
            IEC4bits.U2RXIE = input_state;
            break;

        case uart2_transfer_done:
            IEC4bits.U2TXIE = input_state;
            break;

        case i2c2_bus_collision_event:
            IEC4bits.I2C2BIE = input_state;
            break;

        case i2c2_client_event:
            IEC4bits.I2C2SIE = input_state;
            break;

        case i2c2_host_event:
            IEC4bits.I2C2MIE = input_state;
            break;

        case control_area_network_1:
            IEC4bits.CAN1IE = input_state;
            break;

        case control_area_network_2:
            IEC4bits.CAN2IE = input_state;
            break;

        case ethernet_interrupt:
            IEC4bits.ETHIE = input_state;
            break;

        case spi3_fault:
            IEC4bits.SPI3EIE = input_state;
            break;

        case spi3_receive_done:
            IEC4bits.SPI3RXIE = input_state;
            break;

        case spi3_transfer_done:
            IEC4bits.SPI3TXIE = input_state;
            break;

        case uart3_fault:
            IEC4bits.U3EIE = input_state;
            break;

        case uart3_receive_done:
            IEC4bits.U3RXIE = input_state;
            break;

        case uart3_transfer_done:
            IEC4bits.U3TXIE = input_state;
            break;

        case i2c3_bus_collision_event:
            IEC5bits.I2C3BIE = input_state;
            break;

        case i2c3_client_event:
            IEC5bits.I2C3SIE = input_state;
            break;

        case i2c3_host_event:
            IEC5bits.I2C3MIE = input_state;
            break;

        case spi4_fault:
            IEC5bits.SPI4EIE = input_state;
            break;

        case spi4_receive_done:
            IEC5bits.SPI4RXIE = input_state;
            break;

        case spi4_transfer_done:
            IEC5bits.SPI4TXIE = input_state;
            break;

        case real_time_clock:
            IEC5bits.RTCCIE = input_state;
            break;

        case flash_control_event:
            IEC5bits.FCEIE = input_state;
            break;

        case prefetch_module_sec_event:
            IEC5bits.PREIE = input_state;
            break;

        case sqi1_event:
            IEC5bits.SQI1IE = input_state;
            break;

        case uart4_fault:
            IEC5bits.U4EIE = input_state;
            break;

        case uart4_receive_done:
            IEC5bits.U4RXIE = input_state;
            break;

        case uart4_transfer_done:
            IEC5bits.U4TXIE = input_state;
            break;

        case i2c4_bus_collision_event:
            IEC5bits.I2C4BIE = input_state;
            break;

        case i2c4_client_event:
            IEC5bits.I2C4SIE = input_state;
            break;

        case i2c4_host_event:
            IEC5bits.I2C4MIE = input_state;
            break;

        case spi5_fault:
            IEC5bits.SPI5EIE = input_state;
            break;

        case spi5_receive_done:
            IEC5bits.SPI5RXIE = input_state;
            break;

        case spi5_transfer_done:
            IEC5bits.SPI5TXIE = input_state;
            break;

        case uart5_fault:
            IEC5bits.U5EIE = input_state;
            break;

        case uart5_receive_done:
            IEC5bits.U5RXIE = input_state;
            break;

        case uart5_transfer_done:
            IEC5bits.U5TXIE = input_state;
            break;

        case i2c5_bus_collision_event:
            IEC5bits.I2C5BIE = input_state;
            break;

        case i2c5_client_event:
            IEC5bits.I2C5SIE = input_state;
            break;

        case i2c5_host_event:
            IEC5bits.I2C5MIE = input_state;
            break;

        case spi6_fault:
            IEC5bits.SPI6IE = input_state;
            break;

        case spi6_receive_done:
            IEC5bits.SPI6RXIE = input_state;
            break;

        case spi6_transfer_done:
            IEC5bits.SPI6TXIE = input_state;
            break;

        case uart6_fault:
            IEC5bits.U6EIE = input_state;
            break;

        case uart6_receive_done:
            IEC5bits.U6RXIE = input_state;
            break;

        case uart6_transfer_done:
            IEC5bits.U6TXIE = input_state;
            break;

        case sdhc_interrupt:
            IEC5bits.SDHCIE = input_state;
            break;

        case glcd_interrupt:
            IEC6bits.GLCDIE = input_state;
            break;

        case gpu_interrupt:
            IEC6bits.GPUIE = input_state;
            break;

        case ctmu_interrupt:
            IEC6bits.CTMUIE = input_state;
            break;

        case adc_end_of_scan:
            IEC6bits.ADCEOSIE = input_state;
            break;

        case adc_analog_circuit_ready:
            IEC6bits.ADCARDYIE = input_state;
            break;

        case adc_update_ready:
            IEC6bits.ADCURDYIE = input_state;
            break;

        case adc0_early_interrupt:
            IEC6bits.ADC0EIE = input_state;
            break;

        case adc1_early_interrupt:
            IEC6bits.ADC1EIE = input_state;
            break;

        case adc2_early_interrupt:
            IEC6bits.ADC2EIE = input_state;
            break;

        case adc3_early_interrupt:
            IEC6bits.ADC3EIE = input_state;
            break;

        case adc4_early_interrupt:
            IEC6bits.ADC4EIE = input_state;
            break;

        case adc_group_early_interrupt_request:
            IEC6bits.ADCGRPIE = input_state;
            break;

        case adc7_early_interrupt:
            IEC6bits.ADC7EIE = input_state;
            break;

        case adc0_warm_interrupt:
            IEC6bits.ADC0WIE = input_state;
            break;

        case adc1_warm_interrupt:
            IEC6bits.ADC1WIE = input_state;
            break;

        case adc2_warm_interrupt:
            IEC6bits.ADC2WIE = input_state;
            break;

        case adc3_warm_interrupt:
            IEC6bits.ADC3WIE = input_state;
            break;

        case adc4_warm_interrupt:
            IEC6bits.ADC4WIE = input_state;
            break;

        case adc7_warm_interrupt:
            IEC6bits.ADC7WIE = input_state;
            break;

        case mpll_fault_interrupt:
            IEC6bits.MPLLFLTIE = input_state;
            break;
            
        default:
            break;
            
    }

}

// This function allows for the reading of a given interrupt enable
// It reads the interrupt's 'Interrupt Enable' bit with IEC registers
// Returns the state of the given interrupt
uint8_t getInterruptEnable(interrupt_source_t input_interrupt) {

    // Decide which interrupt control bits to manipulate based on which interrupt
    // is being enabled or disabled
    switch (input_interrupt) {

        case core_timer_interrupt:
            return IEC0bits.CTIE;
            break;

        case core_software_interrupt_0:
            return IEC0bits.CS0IE;
            break;

        case core_software_interrupt_1:
            return IEC0bits.CS1IE;
            break;

        case external_interrupt_0:
            return IEC0bits.INT0IE;
            break;

        case timer1:
            return IEC0bits.T1IE;
            break;

        case input_capture_1_error:
            return IEC0bits.IC1EIE;
            break;

        case input_capture_1:
            return IEC0bits.IC1IE;
            break;

        case output_compare_1:
            return IEC0bits.OC1IE;
            break;

        case external_interrupt_1:
            return IEC0bits.INT1IE;
            break;

        case timer2:
            return IEC0bits.T2IE;
            break;

        case input_capture_2_error:
            return IEC0bits.IC2EIE;
            break;

        case input_capture_2:
            return IEC0bits.IC2IE;
            break;

        case output_compare_2:
            return IEC0bits.OC2IE;
            break;

        case external_interrupt_2:
            return IEC0bits.INT2IE;
            break;

        case timer3:
            return IEC0bits.T3IE;
            break;

        case input_capture_3_error:
            return IEC0bits.IC3EIE;
            break;

        case input_capture_3:
            return IEC0bits.IC3IE;
            break;

        case output_compare_3:
            return IEC0bits.OC3IE;
            break;

        case external_interrupt_3:
            return IEC0bits.INT3IE;
            break;

        case timer4:
            return IEC0bits.T4IE;
            break;

        case input_capture_4_error:
            return IEC0bits.IC4EIE;
            break;

        case input_capture_4:
            return IEC0bits.IC4IE;
            break;

        case output_compare_4:
            return IEC0bits.OC4IE;
            break;

        case external_interrupt_4:
            return IEC0bits.INT4IE;
            break;

        case timer5:
            return IEC0bits.T5IE;
            break;

        case input_capture_5_error:
            return IEC0bits.IC5EIE;
            break;

        case input_capture_5:
            return IEC0bits.IC5IE;
            break;

        case output_compare_5:
            return IEC0bits.OC5IE;
            break;

        case timer6:
            return IEC0bits.T6IE;
            break;

        case input_capture_6_error:
            return IEC0bits.IC6EIE;
            break;

        case input_capture_6:
            return IEC0bits.IC6IE;
            break;

        case output_compare_6:
            return IEC0bits.OC6IE;
            break;

        case timer7:
            return IEC1bits.T7IE;
            break;

        case input_capture_7_error:
            return IEC1bits.IC7EIE;
            break;

        case input_capture_7:
            return IEC1bits.IC7IE;
            break;

        case output_compare_7:
            return IEC1bits.OC7IE;
            break;

        case timer8:
            return IEC1bits.T8IE;
            break;

        case input_capture_8_error:
            return IEC1bits.IC8EIE;
            break;

        case input_capture_8:
            return IEC1bits.IC8IE;
            break;

        case output_compare_8:
            return IEC1bits.OC8IE;
            break;

        case timer9:
            return IEC1bits.T9IE;
            break;

        case input_capture_9_error:
            return IEC1bits.IC9EIE;
            break;

        case input_capture_9:
            return IEC1bits.IC9IE;
            break;

        case output_compare_9:
            return IEC1bits.OC9IE;
            break;

        case adc_global_interrupt:
            return IEC1bits.ADCIE;
            break;

        case adc_fifo_interrupt:
            return IEC1bits.ADCFIFOIE;
            break;

        case adc_digital_comparator_1:
            return IEC1bits.ADCDC1IE;
            break;

        case adc_digital_comparator_2:
            return IEC1bits.ADCDC2IE;
            break;

        case adc_digital_comparator_3:
            return IEC1bits.ADCDC3IE;
            break;

        case adc_digital_comparator_4:
            return IEC1bits.ADCDC4IE;
            break;

        case adc_digital_comparator_5:
            return IEC1bits.ADCDC5IE;
            break;

        case adc_digital_comparator_6:
            return IEC1bits.ADCDC6IE;
            break;

        case adc_digital_filter_1:
            return IEC1bits.ADCDF1IE;
            break;

        case adc_digital_filter_2:
            return IEC1bits.ADCDF2IE;
            break;

        case adc_digital_filter_3:
            return IEC1bits.ADCDF3IE;
            break;

        case adc_digital_filter_4:
            return IEC1bits.ADCDF4IE;
            break;

        case adc_digital_filter_5:
            return IEC1bits.ADCDF5IE;
            break;

        case adc_digital_filter_6:
            return IEC1bits.ADCDF6IE;
            break;

        case adc_fault:
            return IEC1bits.ADCFLTIE;
            break;

        case adc_data_0:
            return IEC1bits.ADCD0IE;
            break;

        case adc_data_1:
            return IEC1bits.ADCD1IE;
            break;

        case adc_data_2:
            return IEC1bits.ADCD2IE;
            break;

        case adc_data_3:
            return IEC1bits.ADCD3IE;
            break;

        case adc_data_4:
            return IEC1bits.ADCD4IE;
            break;

        case adc_data_5:
            return IEC2bits.ADCD5IE;
            break;

        case adc_data_6:
            return IEC2bits.ADCD6IE;
            break;

        case adc_data_7:
            return IEC2bits.ADCD7IE;
            break;

        case adc_data_8:
            return IEC2bits.ADCD8IE;
            break;

        case adc_data_9:
            return IEC2bits.ADCD9IE;
            break;

        case adc_data_10:
            return IEC2bits.ADCD10IE;
            break;

        case adc_data_11:
            return IEC2bits.ADCD11IE;
            break;

        case adc_data_12:
            return IEC2bits.ADCD12IE;
            break;

        case adc_data_13:
            return IEC2bits.ADCD13IE;
            break;

        case adc_data_14:
            return IEC2bits.ADCD14IE;
            break;

        case adc_data_15:
            return IEC2bits.ADCD15IE;
            break;

        case adc_data_16:
            return IEC2bits.ADCD16IE;
            break;

        case adc_data_17:
            return IEC2bits.ADCD17IE;
            break;

        case adc_data_18:
            return IEC2bits.ADCD18IE;
            break;

        case adc_data_19:
            return IEC2bits.ADCD19IE;
            break;

        case adc_data_20:
            return IEC2bits.ADCD20IE;
            break;

        case adc_data_21:
            return IEC2bits.ADCD21IE;
            break;

        case adc_data_22:
            return IEC2bits.ADCD22IE;
            break;

        case adc_data_23:
            return IEC2bits.ADCD23IE;
            break;

        case adc_data_24:
            return IEC2bits.ADCD24IE;
            break;

        case adc_data_25:
            return IEC2bits.ADCD25IE;
            break;

        case adc_data_26:
            return IEC2bits.ADCD26IE;
            break;

        case adc_data_27:
            return IEC2bits.ADCD27IE;
            break;

        case adc_data_28:
            return IEC2bits.ADCD28IE;
            break;

        case adc_data_29:
            return IEC2bits.ADCD29IE;
            break;

        case adc_data_30:
            return IEC2bits.ADCD30IE;
            break;

        case adc_data_31:
            return IEC2bits.ADCD31IE;
            break;

        case adc_data_32:
            return IEC2bits.ADCD32IE;
            break;

        case adc_data_33:
            return IEC2bits.ADCD33IE;
            break;

        case adc_data_34:
            return IEC2bits.ADCD34IE;
            break;

        case adc_data_35:
            return IEC2bits.ADCD35IE;
            break;

        case adc_data_36:
            return IEC2bits.ADCD36IE;
            break;

        case adc_data_37:
            return IEC3bits.ADCD37IE;
            break;

        case adc_data_38:
            return IEC3bits.ADCD38IE;
            break;

        case adc_data_39:
            return IEC3bits.ADCD39IE;
            break;

        case adc_data_40:
            return IEC3bits.ADCD40IE;
            break;

        case adc_data_41:
            return IEC3bits.ADCD41IE;
            break;

        case adc_data_42:
            return IEC3bits.ADCD42IE;
            break;

        case adc_data_43:
            return IEC3bits.ADCD43IE;
            break;

        case usb_suspend_resume_event:
            return IEC3bits.USBSRIE;
            break;

        case core_performance_counter_interrupt:
            return IEC3bits.CPCIE;
            break;

        case core_fast_debug_channel_interrupt:
            return IEC3bits.CFDCIE;
            break;

        case system_bus_protection_violation:
            return IEC3bits.SBIE;
            break;

        case spi1_fault:
            return IEC3bits.SPI1EIE;
            break;

        case spi1_receive_done:
            return IEC3bits.SPI1RXIE;
            break;

        case spi1_transfer_done:
            return IEC3bits.SPI1TXIE;
            break;

        case uart1_fault:
            return IEC3bits.U1EIE;
            break;

        case uart1_receive_done:
            return IEC3bits.U1RXIE;
            break;

        case uart1_transfer_done:
            return IEC3bits.U1TXIE;
            break;

        case i2c1_bus_collision_event:
            return IEC3bits.I2C1BIE;
            break;

        case i2c1_client_event:
            return IEC3bits.I2C1SIE;
            break;

        case i2c1_host_event:
            return IEC3bits.I2C1MIE;
            break;

        case porta_input_change_interrupt:
            return IEC3bits.CNAIE;
            break;

        case portb_input_change_interrupt:
            return IEC3bits.CNBIE;
            break;

        case portc_input_change_interrupt:
            return IEC3bits.CNCIE;
            break;

        case portd_input_change_interrupt:
            return IEC3bits.CNDIE;
            break;

        case porte_input_change_interrupt:
            return IEC3bits.CNEIE;
            break;

        case portf_input_change_interrupt:
            return IEC3bits.CNFIE;
            break;

        case portg_input_change_interrupt:
            return IEC3bits.CNGIE;
            break;

        case porth_input_change_interrupt:
            return IEC3bits.CNHIE;
            break;

        case portj_input_change_interrupt:
            return IEC3bits.CNJIE;
            break;

        case portk_input_change_interrupt:
            return IEC3bits.CNKIE;
            break;

        case pmp:
            return IEC4bits.PMPIE;
            break;

        case pmp_error:
            return IEC4bits.PMPEIE;
            break;

        case comparator_1_interrupt:
            return IEC4bits.CMP1IE;
            break;

        case comparator_2_interrupt:
            return IEC4bits.CMP2IE;
            break;

        case usb_general_event:
            return IEC4bits.USBIE;
            break;

        case usb_dma_event:
            return IEC4bits.USBDMAIE;
            break;

        case dma_channel_0:
            return IEC4bits.DMA0IE;
            break;

        case dma_channel_1:
            return IEC4bits.DMA1IE;
            break;

        case dma_channel_2:
            return IEC4bits.DMA2IE;
            break;

        case dma_channel_3:
            return IEC4bits.DMA3IE;
            break;

        case dma_channel_4:
            return IEC4bits.DMA4IE;
            break;

        case dma_channel_5:
            return IEC4bits.DMA5IE;
            break;

        case dma_channel_6:
            return IEC4bits.DMA6IE;
            break;

        case dma_channel_7:
            return IEC4bits.DMA7IE;
            break;

        case spi2_fault:
            return IEC4bits.SPI2EIE;
            break;

        case spi2_receive_done:
            return IEC4bits.SPI2RXIE;
            break;

        case spi2_transfer_done:
            return IEC4bits.SPI2TXIE;
            break;

        case uart2_fault:
            return IEC4bits.U2EIE;
            break;

        case uart2_receive_done:
            return IEC4bits.U2RXIE;
            break;

        case uart2_transfer_done:
            return IEC4bits.U2TXIE;
            break;

        case i2c2_bus_collision_event:
            return IEC4bits.I2C2BIE;
            break;

        case i2c2_client_event:
            return IEC4bits.I2C2SIE;
            break;

        case i2c2_host_event:
            return IEC4bits.I2C2MIE;
            break;

        case control_area_network_1:
            return IEC4bits.CAN1IE;
            break;

        case control_area_network_2:
            return IEC4bits.CAN2IE;
            break;

        case ethernet_interrupt:
            return IEC4bits.ETHIE;
            break;

        case spi3_fault:
            return IEC4bits.SPI3EIE;
            break;

        case spi3_receive_done:
            return IEC4bits.SPI3RXIE;
            break;

        case spi3_transfer_done:
            return IEC4bits.SPI3TXIE;
            break;

        case uart3_fault:
            return IEC4bits.U3EIE;
            break;

        case uart3_receive_done:
            return IEC4bits.U3RXIE;
            break;

        case uart3_transfer_done:
            return IEC4bits.U3TXIE;
            break;

        case i2c3_bus_collision_event:
            return IEC5bits.I2C3BIE;
            break;

        case i2c3_client_event:
            return IEC5bits.I2C3SIE;
            break;

        case i2c3_host_event:
            return IEC5bits.I2C3MIE;
            break;

        case spi4_fault:
            return IEC5bits.SPI4EIE;
            break;

        case spi4_receive_done:
            return IEC5bits.SPI4RXIE;
            break;

        case spi4_transfer_done:
            return IEC5bits.SPI4TXIE;
            break;

        case real_time_clock:
            return IEC5bits.RTCCIE;
            break;

        case flash_control_event:
            return IEC5bits.FCEIE;
            break;

        case prefetch_module_sec_event:
            return IEC5bits.PREIE;
            break;

        case sqi1_event:
            return IEC5bits.SQI1IE;
            break;

        case uart4_fault:
            return IEC5bits.U4EIE;
            break;

        case uart4_receive_done:
            return IEC5bits.U4RXIE;
            break;

        case uart4_transfer_done:
            return IEC5bits.U4TXIE;
            break;

        case i2c4_bus_collision_event:
            return IEC5bits.I2C4BIE;
            break;

        case i2c4_client_event:
            return IEC5bits.I2C4SIE;
            break;

        case i2c4_host_event:
            return IEC5bits.I2C4MIE;
            break;

        case spi5_fault:
            return IEC5bits.SPI5EIE;
            break;

        case spi5_receive_done:
            return IEC5bits.SPI5RXIE;
            break;

        case spi5_transfer_done:
            return IEC5bits.SPI5TXIE;
            break;

        case uart5_fault:
            return IEC5bits.U5EIE;
            break;

        case uart5_receive_done:
            return IEC5bits.U5RXIE;
            break;

        case uart5_transfer_done:
            return IEC5bits.U5TXIE;
            break;

        case i2c5_bus_collision_event:
            return IEC5bits.I2C5BIE;
            break;

        case i2c5_client_event:
            return IEC5bits.I2C5SIE;
            break;

        case i2c5_host_event:
            return IEC5bits.I2C5MIE;
            break;

        case spi6_fault:
            return IEC5bits.SPI6IE;
            break;

        case spi6_receive_done:
            return IEC5bits.SPI6RXIE;
            break;

        case spi6_transfer_done:
            return IEC5bits.SPI6TXIE;
            break;

        case uart6_fault:
            return IEC5bits.U6EIE;
            break;

        case uart6_receive_done:
            return IEC5bits.U6RXIE;
            break;

        case uart6_transfer_done:
            return IEC5bits.U6TXIE;
            break;

        case sdhc_interrupt:
            return IEC5bits.SDHCIE;
            break;

        case glcd_interrupt:
            return IEC6bits.GLCDIE;
            break;

        case gpu_interrupt:
            return IEC6bits.GPUIE;
            break;

        case ctmu_interrupt:
            return IEC6bits.CTMUIE;
            break;

        case adc_end_of_scan:
            return IEC6bits.ADCEOSIE;
            break;

        case adc_analog_circuit_ready:
            return IEC6bits.ADCARDYIE;
            break;

        case adc_update_ready:
            return IEC6bits.ADCURDYIE;
            break;

        case adc0_early_interrupt:
            return IEC6bits.ADC0EIE;
            break;

        case adc1_early_interrupt:
            return IEC6bits.ADC1EIE;
            break;

        case adc2_early_interrupt:
            return IEC6bits.ADC2EIE;
            break;

        case adc3_early_interrupt:
            return IEC6bits.ADC3EIE;
            break;

        case adc4_early_interrupt:
            return IEC6bits.ADC4EIE;
            break;

        case adc_group_early_interrupt_request:
            return IEC6bits.ADCGRPIE;
            break;

        case adc7_early_interrupt:
            return IEC6bits.ADC7EIE;
            break;

        case adc0_warm_interrupt:
            return IEC6bits.ADC0WIE;
            break;

        case adc1_warm_interrupt:
            return IEC6bits.ADC1WIE;
            break;

        case adc2_warm_interrupt:
            return IEC6bits.ADC2WIE;
            break;

        case adc3_warm_interrupt:
            return IEC6bits.ADC3WIE;
            break;

        case adc4_warm_interrupt:
            return IEC6bits.ADC4WIE;
            break;

        case adc7_warm_interrupt:
            return IEC6bits.ADC7WIE;
            break;

        case mpll_fault_interrupt:
            return IEC6bits.MPLLFLTIE;
            break;
            
        default:
            break;
            
    }

}

// This function allows for the modification of a given interrupt flag
// It sets the interrupt's 'Interrupt Flag' bit
void setInterruptFlag(interrupt_source_t input_interrupt, uint8_t flag_state) {
    
    // Decide which interrupt control bits to manipulate based on which interrupt
    // is being enabled or disabled
    switch (input_interrupt) {
        

        case core_timer_interrupt:
            IFS0bits.CTIF = flag_state;
            break;

        case core_software_interrupt_0:
            IFS0bits.CS0IF = flag_state;
            break;

        case core_software_interrupt_1:
            IFS0bits.CS1IF = flag_state;
            break;

        case external_interrupt_0:
            IFS0bits.INT0IF = flag_state;
            break;

        case timer1:
            IFS0bits.T1IF = flag_state;
            break;

        case input_capture_1_error:
            IFS0bits.IC1EIF = flag_state;
            break;

        case input_capture_1:
            IFS0bits.IC1IF = flag_state;
            break;

        case output_compare_1:
            IFS0bits.OC1IF = flag_state;
            break;

        case external_interrupt_1:
            IFS0bits.INT1IF = flag_state;
            break;

        case timer2:
            IFS0bits.T2IF = flag_state;
            break;

        case input_capture_2_error:
            IFS0bits.IC2EIF = flag_state;
            break;

        case input_capture_2:
            IFS0bits.IC2IF = flag_state;
            break;

        case output_compare_2:
            IFS0bits.OC2IF = flag_state;
            break;

        case external_interrupt_2:
            IFS0bits.INT2IF = flag_state;
            break;

        case timer3:
            IFS0bits.T3IF = flag_state;
            break;

        case input_capture_3_error:
            IFS0bits.IC3EIF = flag_state;
            break;

        case input_capture_3:
            IFS0bits.IC3IF = flag_state;
            break;

        case output_compare_3:
            IFS0bits.OC3IF = flag_state;
            break;

        case external_interrupt_3:
            IFS0bits.INT3IF = flag_state;
            break;

        case timer4:
            IFS0bits.T4IF = flag_state;
            break;

        case input_capture_4_error:
            IFS0bits.IC4EIF = flag_state;
            break;

        case input_capture_4:
            IFS0bits.IC4IF = flag_state;
            break;

        case output_compare_4:
            IFS0bits.OC4IF = flag_state;
            break;

        case external_interrupt_4:
            IFS0bits.INT4IF = flag_state;
            break;

        case timer5:
            IFS0bits.T5IF = flag_state;
            break;

        case input_capture_5_error:
            IFS0bits.IC5EIF = flag_state;
            break;

        case input_capture_5:
            IFS0bits.IC5IF = flag_state;
            break;

        case output_compare_5:
            IFS0bits.OC5IF = flag_state;
            break;

        case timer6:
            IFS0bits.T6IF = flag_state;
            break;

        case input_capture_6_error:
            IFS0bits.IC6EIF = flag_state;
            break;

        case input_capture_6:
            IFS0bits.IC6IF = flag_state;
            break;

        case output_compare_6:
            IFS0bits.OC6IF = flag_state;
            break;

        case timer7:
            IFS1bits.T7IF = flag_state;
            break;

        case input_capture_7_error:
            IFS1bits.IC7EIF = flag_state;
            break;

        case input_capture_7:
            IFS1bits.IC7IF = flag_state;
            break;

        case output_compare_7:
            IFS1bits.OC7IF = flag_state;
            break;

        case timer8:
            IFS1bits.T8IF = flag_state;
            break;

        case input_capture_8_error:
            IFS1bits.IC8EIF = flag_state;
            break;

        case input_capture_8:
            IFS1bits.IC8IF = flag_state;
            break;

        case output_compare_8:
            IFS1bits.OC8IF = flag_state;
            break;

        case timer9:
            IFS1bits.T9IF = flag_state;
            break;

        case input_capture_9_error:
            IFS1bits.IC9EIF = flag_state;
            break;

        case input_capture_9:
            IFS1bits.IC9IF = flag_state;
            break;

        case output_compare_9:
            IFS1bits.OC9IF = flag_state;
            break;

        case adc_global_interrupt:
            IFS1bits.ADCIF = flag_state;
            break;

        case adc_fifo_interrupt:
            IFS1bits.ADCFIFOIF = flag_state;
            break;

        case adc_digital_comparator_1:
            IFS1bits.ADCDC1IF = flag_state;
            break;

        case adc_digital_comparator_2:
            IFS1bits.ADCDC2IF = flag_state;
            break;

        case adc_digital_comparator_3:
            IFS1bits.ADCDC3IF = flag_state;
            break;

        case adc_digital_comparator_4:
            IFS1bits.ADCDC4IF = flag_state;
            break;

        case adc_digital_comparator_5:
            IFS1bits.ADCDC5IF = flag_state;
            break;

        case adc_digital_comparator_6:
            IFS1bits.ADCDC6IF = flag_state;
            break;

        case adc_digital_filter_1:
            IFS1bits.ADCDF1IF = flag_state;
            break;

        case adc_digital_filter_2:
            IFS1bits.ADCDF2IF = flag_state;
            break;

        case adc_digital_filter_3:
            IFS1bits.ADCDF3IF = flag_state;
            break;

        case adc_digital_filter_4:
            IFS1bits.ADCDF4IF = flag_state;
            break;

        case adc_digital_filter_5:
            IFS1bits.ADCDF5IF = flag_state;
            break;

        case adc_digital_filter_6:
            IFS1bits.ADCDF6IF = flag_state;
            break;

        case adc_fault:
            IFS1bits.ADCFLTIF = flag_state;
            break;

        case adc_data_0:
            IFS1bits.ADCD0IF = flag_state;
            break;

        case adc_data_1:
            IFS1bits.ADCD1IF = flag_state;
            break;

        case adc_data_2:
            IFS1bits.ADCD2IF = flag_state;
            break;

        case adc_data_3:
            IFS1bits.ADCD3IF = flag_state;
            break;

        case adc_data_4:
            IFS1bits.ADCD4IF = flag_state;
            break;

        case adc_data_5:
            IFS2bits.ADCD5IF = flag_state;
            break;

        case adc_data_6:
            IFS2bits.ADCD6IF = flag_state;
            break;

        case adc_data_7:
            IFS2bits.ADCD7IF = flag_state;
            break;

        case adc_data_8:
            IFS2bits.ADCD8IF = flag_state;
            break;

        case adc_data_9:
            IFS2bits.ADCD9IF = flag_state;
            break;

        case adc_data_10:
            IFS2bits.ADCD10IF = flag_state;
            break;

        case adc_data_11:
            IFS2bits.ADCD11IF = flag_state;
            break;

        case adc_data_12:
            IFS2bits.ADCD12IF = flag_state;
            break;

        case adc_data_13:
            IFS2bits.ADCD13IF = flag_state;
            break;

        case adc_data_14:
            IFS2bits.ADCD14IF = flag_state;
            break;

        case adc_data_15:
            IFS2bits.ADCD15IF = flag_state;
            break;

        case adc_data_16:
            IFS2bits.ADCD16IF = flag_state;
            break;

        case adc_data_17:
            IFS2bits.ADCD17IF = flag_state;
            break;

        case adc_data_18:
            IFS2bits.ADCD18IF = flag_state;
            break;

        case adc_data_19:
            IFS2bits.ADCD19IF = flag_state;
            break;

        case adc_data_20:
            IFS2bits.ADCD20IF = flag_state;
            break;

        case adc_data_21:
            IFS2bits.ADCD21IF = flag_state;
            break;

        case adc_data_22:
            IFS2bits.ADCD22IF = flag_state;
            break;

        case adc_data_23:
            IFS2bits.ADCD23IF = flag_state;
            break;

        case adc_data_24:
            IFS2bits.ADCD24IF = flag_state;
            break;

        case adc_data_25:
            IFS2bits.ADCD25IF = flag_state;
            break;

        case adc_data_26:
            IFS2bits.ADCD26IF = flag_state;
            break;

        case adc_data_27:
            IFS2bits.ADCD27IF = flag_state;
            break;

        case adc_data_28:
            IFS2bits.ADCD28IF = flag_state;
            break;

        case adc_data_29:
            IFS2bits.ADCD29IF = flag_state;
            break;

        case adc_data_30:
            IFS2bits.ADCD30IF = flag_state;
            break;

        case adc_data_31:
            IFS2bits.ADCD31IF = flag_state;
            break;

        case adc_data_32:
            IFS2bits.ADCD32IF = flag_state;
            break;

        case adc_data_33:
            IFS2bits.ADCD33IF = flag_state;
            break;

        case adc_data_34:
            IFS2bits.ADCD34IF = flag_state;
            break;

        case adc_data_35:
            IFS2bits.ADCD35IF = flag_state;
            break;

        case adc_data_36:
            IFS2bits.ADCD36IF = flag_state;
            break;

        case adc_data_37:
            IFS3bits.ADCD37IF = flag_state;
            break;

        case adc_data_38:
            IFS3bits.ADCD38IF = flag_state;
            break;

        case adc_data_39:
            IFS3bits.ADCD39IF = flag_state;
            break;

        case adc_data_40:
            IFS3bits.ADCD40IF = flag_state;
            break;

        case adc_data_41:
            IFS3bits.ADCD41IF = flag_state;
            break;

        case adc_data_42:
            IFS3bits.ADCD42IF = flag_state;
            break;

        case adc_data_43:
            IFS3bits.ADCD43IF = flag_state;
            break;

        case usb_suspend_resume_event:
            IFS3bits.USBSRIF = flag_state;
            break;

        case core_performance_counter_interrupt:
            IFS3bits.CPCIF = flag_state;
            break;

        case core_fast_debug_channel_interrupt:
            IFS3bits.CFDCIF = flag_state;
            break;

        case system_bus_protection_violation:
            IFS3bits.SBIF = flag_state;
            break;

        case spi1_fault:
            IFS3bits.SPI1EIF = flag_state;
            break;

        case spi1_receive_done:
            IFS3bits.SPI1RXIF = flag_state;
            break;

        case spi1_transfer_done:
            IFS3bits.SPI1TXIF = flag_state;
            break;

        case uart1_fault:
            IFS3bits.U1EIF = flag_state;
            break;

        case uart1_receive_done:
            IFS3bits.U1RXIF = flag_state;
            break;

        case uart1_transfer_done:
            IFS3bits.U1TXIF = flag_state;
            break;

        case i2c1_bus_collision_event:
            IFS3bits.I2C1BIF = flag_state;
            break;

        case i2c1_client_event:
            IFS3bits.I2C1SIF = flag_state;
            break;

        case i2c1_host_event:
            IFS3bits.I2C1MIF = flag_state;
            break;

        case porta_input_change_interrupt:
            IFS3bits.CNAIF = flag_state;
            break;

        case portb_input_change_interrupt:
            IFS3bits.CNBIF = flag_state;
            break;

        case portc_input_change_interrupt:
            IFS3bits.CNCIF = flag_state;
            break;

        case portd_input_change_interrupt:
            IFS3bits.CNDIF = flag_state;
            break;

        case porte_input_change_interrupt:
            IFS3bits.CNEIF = flag_state;
            break;

        case portf_input_change_interrupt:
            IFS3bits.CNFIF = flag_state;
            break;

        case portg_input_change_interrupt:
            IFS3bits.CNGIF = flag_state;
            break;

        case porth_input_change_interrupt:
            IFS3bits.CNHIF = flag_state;
            break;

        case portj_input_change_interrupt:
            IFS3bits.CNJIF = flag_state;
            break;

        case portk_input_change_interrupt:
            IFS3bits.CNKIF = flag_state;
            break;

        case pmp:
            IFS4bits.PMPIF = flag_state;
            break;

        case pmp_error:
            IFS4bits.PMPEIF = flag_state;
            break;

        case comparator_1_interrupt:
            IFS4bits.CMP1IF = flag_state;
            break;

        case comparator_2_interrupt:
            IFS4bits.CMP2IF = flag_state;
            break;

        case usb_general_event:
            IFS4bits.USBIF = flag_state;
            break;

        case usb_dma_event:
            IFS4bits.USBDMAIF = flag_state;
            break;

        case dma_channel_0:
            IFS4bits.DMA0IF = flag_state;
            break;

        case dma_channel_1:
            IFS4bits.DMA1IF = flag_state;
            break;

        case dma_channel_2:
            IFS4bits.DMA2IF = flag_state;
            break;

        case dma_channel_3:
            IFS4bits.DMA3IF = flag_state;
            break;

        case dma_channel_4:
            IFS4bits.DMA4IF = flag_state;
            break;

        case dma_channel_5:
            IFS4bits.DMA5IF = flag_state;
            break;

        case dma_channel_6:
            IFS4bits.DMA6IF = flag_state;
            break;

        case dma_channel_7:
            IFS4bits.DMA7IF = flag_state;
            break;

        case spi2_fault:
            IFS4bits.SPI2EIF = flag_state;
            break;

        case spi2_receive_done:
            IFS4bits.SPI2RXIF = flag_state;
            break;

        case spi2_transfer_done:
            IFS4bits.SPI2TXIF = flag_state;
            break;

        case uart2_fault:
            IFS4bits.U2EIF = flag_state;
            break;

        case uart2_receive_done:
            IFS4bits.U2RXIF = flag_state;
            break;

        case uart2_transfer_done:
            IFS4bits.U2TXIF = flag_state;
            break;

        case i2c2_bus_collision_event:
            IFS4bits.I2C2BIF = flag_state;
            break;

        case i2c2_client_event:
            IFS4bits.I2C2SIF = flag_state;
            break;

        case i2c2_host_event:
            IFS4bits.I2C2MIF = flag_state;
            break;

        case control_area_network_1:
            IFS4bits.CAN1IF = flag_state;
            break;

        case control_area_network_2:
            IFS4bits.CAN2IF = flag_state;
            break;

        case ethernet_interrupt:
            IFS4bits.ETHIF = flag_state;
            break;

        case spi3_fault:
            IFS4bits.SPI3EIF = flag_state;
            break;

        case spi3_receive_done:
            IFS4bits.SPI3RXIF = flag_state;
            break;

        case spi3_transfer_done:
            IFS4bits.SPI3TXIF = flag_state;
            break;

        case uart3_fault:
            IFS4bits.U3EIF = flag_state;
            break;

        case uart3_receive_done:
            IFS4bits.U3RXIF = flag_state;
            break;

        case uart3_transfer_done:
            IFS4bits.U3TXIF = flag_state;
            break;

        case i2c3_bus_collision_event:
            IFS5bits.I2C3BIF = flag_state;
            break;

        case i2c3_client_event:
            IFS5bits.I2C3SIF = flag_state;
            break;

        case i2c3_host_event:
            IFS5bits.I2C3MIF = flag_state;
            break;

        case spi4_fault:
            IFS5bits.SPI4EIF = flag_state;
            break;

        case spi4_receive_done:
            IFS5bits.SPI4RXIF = flag_state;
            break;

        case spi4_transfer_done:
            IFS5bits.SPI4TXIF = flag_state;
            break;

        case real_time_clock:
            IFS5bits.RTCCIF = flag_state;
            break;

        case flash_control_event:
            IFS5bits.FCEIF = flag_state;
            break;

        case prefetch_module_sec_event:
            IFS5bits.PREIF = flag_state;
            break;

        case sqi1_event:
            IFS5bits.SQI1IF = flag_state;
            break;

        case uart4_fault:
            IFS5bits.U4EIF = flag_state;
            break;

        case uart4_receive_done:
            IFS5bits.U4RXIF = flag_state;
            break;

        case uart4_transfer_done:
            IFS5bits.U4TXIF = flag_state;
            break;

        case i2c4_bus_collision_event:
            IFS5bits.I2C4BIF = flag_state;
            break;

        case i2c4_client_event:
            IFS5bits.I2C4SIF = flag_state;
            break;

        case i2c4_host_event:
            IFS5bits.I2C4MIF = flag_state;
            break;

        case spi5_fault:
            IFS5bits.SPI5EIF = flag_state;
            break;

        case spi5_receive_done:
            IFS5bits.SPI5RXIF = flag_state;
            break;

        case spi5_transfer_done:
            IFS5bits.SPI5TXIF = flag_state;
            break;

        case uart5_fault:
            IFS5bits.U5EIF = flag_state;
            break;

        case uart5_receive_done:
            IFS5bits.U5RXIF = flag_state;
            break;

        case uart5_transfer_done:
            IFS5bits.U5TXIF = flag_state;
            break;

        case i2c5_bus_collision_event:
            IFS5bits.I2C5BIF = flag_state;
            break;

        case i2c5_client_event:
            IFS5bits.I2C5SIF = flag_state;
            break;

        case i2c5_host_event:
            IFS5bits.I2C5MIF = flag_state;
            break;

        case spi6_fault:
            IFS5bits.SPI6IF = flag_state;
            break;

        case spi6_receive_done:
            IFS5bits.SPI6RXIF = flag_state;
            break;

        case spi6_transfer_done:
            IFS5bits.SPI6TX = flag_state;
            break;

        case uart6_fault:
            IFS5bits.U6EIF = flag_state;
            break;

        case uart6_receive_done:
            IFS5bits.U6RXIF = flag_state;
            break;

        case uart6_transfer_done:
            IFS5bits.U6TXIF = flag_state;
            break;

        case sdhc_interrupt:
            IFS5bits.SDHCIF = flag_state;
            break;

        case glcd_interrupt:
            IFS6bits.GLCDIF = flag_state;
            break;

        case gpu_interrupt:
            IFS6bits.GPUIF = flag_state;
            break;

        case ctmu_interrupt:
            IFS6bits.CTMUIF = flag_state;
            break;

        case adc_end_of_scan:
            IFS6bits.ADCEOSIF = flag_state;
            break;

        case adc_analog_circuit_ready:
            IFS6bits.ADCARDYIF = flag_state;
            break;

        case adc_update_ready:
            IFS6bits.ADCURDYIF = flag_state;
            break;

        case adc0_early_interrupt:
            IFS6bits.ADC0EIF = flag_state;
            break;

        case adc1_early_interrupt:
            IFS6bits.ADC1EIF = flag_state;
            break;

        case adc2_early_interrupt:
            IFS6bits.ADC2EIF = flag_state;
            break;

        case adc3_early_interrupt:
            IFS6bits.ADC3EIF = flag_state;
            break;

        case adc4_early_interrupt:
            IFS6bits.ADC4EIF = flag_state;
            break;

        case adc_group_early_interrupt_request:
            IFS6bits.ADCGRPIF = flag_state;
            break;

        case adc7_early_interrupt:
            IFS6bits.ADC7EIF = flag_state;
            break;

        case adc0_warm_interrupt:
            IFS6bits.ADC0WIF = flag_state;
            break;

        case adc1_warm_interrupt:
            IFS6bits.ADC1WIF = flag_state;
            break;

        case adc2_warm_interrupt:
            IFS6bits.ADC2WIF = flag_state;
            break;

        case adc3_warm_interrupt:
            IFS6bits.ADC3WIF = flag_state;
            break;

        case adc4_warm_interrupt:
            IFS6bits.ADC4WIF = flag_state;
            break;

        case adc7_warm_interrupt:
            IFS6bits.ADC7WIF = flag_state;
            break;

        case mpll_fault_interrupt:
            IFS6bits.MPLLFLTIF = flag_state;
            break;
            
        default:
            break;
            
    }
    
}


// This function allows for the reading of a given interrupt flag
// It reads the interrupt's 'Interrupt Flag' bit
// Returns the state of the given interrupt flag
uint8_t getInterruptFlag(interrupt_source_t input_interrupt) {
    
    // Decide which interrupt control bits to manipulate based on which interrupt
    // is being enabled or disabled
    switch (input_interrupt) {
        

        case core_timer_interrupt:
            return IFS0bits.CTIF;
            break;

        case core_software_interrupt_0:
            return IFS0bits.CS0IF;
            break;

        case core_software_interrupt_1:
            return IFS0bits.CS1IF;
            break;

        case external_interrupt_0:
            return IFS0bits.INT0IF;
            break;

        case timer1:
            return IFS0bits.T1IF;
            break;

        case input_capture_1_error:
            return IFS0bits.IC1EIF;
            break;

        case input_capture_1:
            return IFS0bits.IC1IF;
            break;

        case output_compare_1:
            return IFS0bits.OC1IF;
            break;

        case external_interrupt_1:
            return IFS0bits.INT1IF;
            break;

        case timer2:
            return IFS0bits.T2IF;
            break;

        case input_capture_2_error:
            return IFS0bits.IC2EIF;
            break;

        case input_capture_2:
            return IFS0bits.IC2IF;
            break;

        case output_compare_2:
            return IFS0bits.OC2IF;
            break;

        case external_interrupt_2:
            return IFS0bits.INT2IF;
            break;

        case timer3:
            return IFS0bits.T3IF;
            break;

        case input_capture_3_error:
            return IFS0bits.IC3EIF;
            break;

        case input_capture_3:
            return IFS0bits.IC3IF;
            break;

        case output_compare_3:
            return IFS0bits.OC3IF;
            break;

        case external_interrupt_3:
            return IFS0bits.INT3IF;
            break;

        case timer4:
            return IFS0bits.T4IF;
            break;

        case input_capture_4_error:
            return IFS0bits.IC4EIF;
            break;

        case input_capture_4:
            return IFS0bits.IC4IF;
            break;

        case output_compare_4:
            return IFS0bits.OC4IF;
            break;

        case external_interrupt_4:
            return IFS0bits.INT4IF;
            break;

        case timer5:
            return IFS0bits.T5IF;
            break;

        case input_capture_5_error:
            return IFS0bits.IC5EIF;
            break;

        case input_capture_5:
            return IFS0bits.IC5IF;
            break;

        case output_compare_5:
            return IFS0bits.OC5IF;
            break;

        case timer6:
            return IFS0bits.T6IF;
            break;

        case input_capture_6_error:
            return IFS0bits.IC6EIF;
            break;

        case input_capture_6:
            return IFS0bits.IC6IF;
            break;

        case output_compare_6:
            return IFS0bits.OC6IF;
            break;

        case timer7:
            return IFS1bits.T7IF;
            break;

        case input_capture_7_error:
            return IFS1bits.IC7EIF;
            break;

        case input_capture_7:
            return IFS1bits.IC7IF;
            break;

        case output_compare_7:
            return IFS1bits.OC7IF;
            break;

        case timer8:
            return IFS1bits.T8IF;
            break;

        case input_capture_8_error:
            return IFS1bits.IC8EIF;
            break;

        case input_capture_8:
            return IFS1bits.IC8IF;
            break;

        case output_compare_8:
            return IFS1bits.OC8IF;
            break;

        case timer9:
            return IFS1bits.T9IF;
            break;

        case input_capture_9_error:
            return IFS1bits.IC9EIF;
            break;

        case input_capture_9:
            return IFS1bits.IC9IF;
            break;

        case output_compare_9:
            return IFS1bits.OC9IF;
            break;

        case adc_global_interrupt:
            return IFS1bits.ADCIF;
            break;

        case adc_fifo_interrupt:
            return IFS1bits.ADCFIFOIF;
            break;

        case adc_digital_comparator_1:
            return IFS1bits.ADCDC1IF;
            break;

        case adc_digital_comparator_2:
            return IFS1bits.ADCDC2IF;
            break;

        case adc_digital_comparator_3:
            return IFS1bits.ADCDC3IF;
            break;

        case adc_digital_comparator_4:
            return IFS1bits.ADCDC4IF;
            break;

        case adc_digital_comparator_5:
            return IFS1bits.ADCDC5IF;
            break;

        case adc_digital_comparator_6:
            return IFS1bits.ADCDC6IF;
            break;

        case adc_digital_filter_1:
            return IFS1bits.ADCDF1IF;
            break;

        case adc_digital_filter_2:
            return IFS1bits.ADCDF2IF;
            break;

        case adc_digital_filter_3:
            return IFS1bits.ADCDF3IF;
            break;

        case adc_digital_filter_4:
            return IFS1bits.ADCDF4IF;
            break;

        case adc_digital_filter_5:
            return IFS1bits.ADCDF5IF;
            break;

        case adc_digital_filter_6:
            return IFS1bits.ADCDF6IF;
            break;

        case adc_fault:
            return IFS1bits.ADCFLTIF;
            break;

        case adc_data_0:
            return IFS1bits.ADCD0IF;
            break;

        case adc_data_1:
            return IFS1bits.ADCD1IF;
            break;

        case adc_data_2:
            return IFS1bits.ADCD2IF;
            break;

        case adc_data_3:
            return IFS1bits.ADCD3IF;
            break;

        case adc_data_4:
            return IFS1bits.ADCD4IF;
            break;

        case adc_data_5:
            return IFS2bits.ADCD5IF;
            break;

        case adc_data_6:
            return IFS2bits.ADCD6IF;
            break;

        case adc_data_7:
            return IFS2bits.ADCD7IF;
            break;

        case adc_data_8:
            return IFS2bits.ADCD8IF;
            break;

        case adc_data_9:
            return IFS2bits.ADCD9IF;
            break;

        case adc_data_10:
            return IFS2bits.ADCD10IF;
            break;

        case adc_data_11:
            return IFS2bits.ADCD11IF;
            break;

        case adc_data_12:
            return IFS2bits.ADCD12IF;
            break;

        case adc_data_13:
            return IFS2bits.ADCD13IF;
            break;

        case adc_data_14:
            return IFS2bits.ADCD14IF;
            break;

        case adc_data_15:
            return IFS2bits.ADCD15IF;
            break;

        case adc_data_16:
            return IFS2bits.ADCD16IF;
            break;

        case adc_data_17:
            return IFS2bits.ADCD17IF;
            break;

        case adc_data_18:
            return IFS2bits.ADCD18IF;
            break;

        case adc_data_19:
            return IFS2bits.ADCD19IF;
            break;

        case adc_data_20:
            return IFS2bits.ADCD20IF;
            break;

        case adc_data_21:
            return IFS2bits.ADCD21IF;
            break;

        case adc_data_22:
            return IFS2bits.ADCD22IF;
            break;

        case adc_data_23:
            return IFS2bits.ADCD23IF;
            break;

        case adc_data_24:
            return IFS2bits.ADCD24IF;
            break;

        case adc_data_25:
            return IFS2bits.ADCD25IF;
            break;

        case adc_data_26:
            return IFS2bits.ADCD26IF;
            break;

        case adc_data_27:
            return IFS2bits.ADCD27IF;
            break;

        case adc_data_28:
            return IFS2bits.ADCD28IF;
            break;

        case adc_data_29:
            return IFS2bits.ADCD29IF;
            break;

        case adc_data_30:
            return IFS2bits.ADCD30IF;
            break;

        case adc_data_31:
            return IFS2bits.ADCD31IF;
            break;

        case adc_data_32:
            return IFS2bits.ADCD32IF;
            break;

        case adc_data_33:
            return IFS2bits.ADCD33IF;
            break;

        case adc_data_34:
            return IFS2bits.ADCD34IF;
            break;

        case adc_data_35:
            return IFS2bits.ADCD35IF;
            break;

        case adc_data_36:
            return IFS2bits.ADCD36IF;
            break;

        case adc_data_37:
            return IFS3bits.ADCD37IF;
            break;

        case adc_data_38:
            return IFS3bits.ADCD38IF;
            break;

        case adc_data_39:
            return IFS3bits.ADCD39IF;
            break;

        case adc_data_40:
            return IFS3bits.ADCD40IF;
            break;

        case adc_data_41:
            return IFS3bits.ADCD41IF;
            break;

        case adc_data_42:
            return IFS3bits.ADCD42IF;
            break;

        case adc_data_43:
            return IFS3bits.ADCD43IF;
            break;

        case usb_suspend_resume_event:
            return IFS3bits.USBSRIF;
            break;

        case core_performance_counter_interrupt:
            return IFS3bits.CPCIF;
            break;

        case core_fast_debug_channel_interrupt:
            return IFS3bits.CFDCIF;
            break;

        case system_bus_protection_violation:
            return IFS3bits.SBIF;
            break;

        case spi1_fault:
            return IFS3bits.SPI1EIF;
            break;

        case spi1_receive_done:
            return IFS3bits.SPI1RXIF;
            break;

        case spi1_transfer_done:
            return IFS3bits.SPI1TXIF;
            break;

        case uart1_fault:
            return IFS3bits.U1EIF;
            break;

        case uart1_receive_done:
            return IFS3bits.U1RXIF;
            break;

        case uart1_transfer_done:
            return IFS3bits.U1TXIF;
            break;

        case i2c1_bus_collision_event:
            return IFS3bits.I2C1BIF;
            break;

        case i2c1_client_event:
            return IFS3bits.I2C1SIF;
            break;

        case i2c1_host_event:
            return IFS3bits.I2C1MIF;
            break;

        case porta_input_change_interrupt:
            return IFS3bits.CNAIF;
            break;

        case portb_input_change_interrupt:
            return IFS3bits.CNBIF;
            break;

        case portc_input_change_interrupt:
            return IFS3bits.CNCIF;
            break;

        case portd_input_change_interrupt:
            return IFS3bits.CNDIF;
            break;

        case porte_input_change_interrupt:
            return IFS3bits.CNEIF;
            break;

        case portf_input_change_interrupt:
            return IFS3bits.CNFIF;
            break;

        case portg_input_change_interrupt:
            return IFS3bits.CNGIF;
            break;

        case porth_input_change_interrupt:
            return IFS3bits.CNHIF;
            break;

        case portj_input_change_interrupt:
            return IFS3bits.CNJIF;
            break;

        case portk_input_change_interrupt:
            return IFS3bits.CNKIF;
            break;

        case pmp:
            return IFS4bits.PMPIF;
            break;

        case pmp_error:
            return IFS4bits.PMPEIF;
            break;

        case comparator_1_interrupt:
            return IFS4bits.CMP1IF;
            break;

        case comparator_2_interrupt:
            return IFS4bits.CMP2IF;
            break;

        case usb_general_event:
            return IFS4bits.USBIF;
            break;

        case usb_dma_event:
            return IFS4bits.USBDMAIF;
            break;

        case dma_channel_0:
            return IFS4bits.DMA0IF;
            break;

        case dma_channel_1:
            return IFS4bits.DMA1IF;
            break;

        case dma_channel_2:
            return IFS4bits.DMA2IF;
            break;

        case dma_channel_3:
            return IFS4bits.DMA3IF;
            break;

        case dma_channel_4:
            return IFS4bits.DMA4IF;
            break;

        case dma_channel_5:
            return IFS4bits.DMA5IF;
            break;

        case dma_channel_6:
            return IFS4bits.DMA6IF;
            break;

        case dma_channel_7:
            return IFS4bits.DMA7IF;
            break;

        case spi2_fault:
            return IFS4bits.SPI2EIF;
            break;

        case spi2_receive_done:
            return IFS4bits.SPI2RXIF;
            break;

        case spi2_transfer_done:
            return IFS4bits.SPI2TXIF;
            break;

        case uart2_fault:
            return IFS4bits.U2EIF;
            break;

        case uart2_receive_done:
            return IFS4bits.U2RXIF;
            break;

        case uart2_transfer_done:
            return IFS4bits.U2TXIF;
            break;

        case i2c2_bus_collision_event:
            return IFS4bits.I2C2BIF;
            break;

        case i2c2_client_event:
            return IFS4bits.I2C2SIF;
            break;

        case i2c2_host_event:
            return IFS4bits.I2C2MIF;
            break;

        case control_area_network_1:
            return IFS4bits.CAN1IF;
            break;

        case control_area_network_2:
            return IFS4bits.CAN2IF;
            break;

        case ethernet_interrupt:
            return IFS4bits.ETHIF;
            break;

        case spi3_fault:
            return IFS4bits.SPI3EIF;
            break;

        case spi3_receive_done:
            return IFS4bits.SPI3RXIF;
            break;

        case spi3_transfer_done:
            return IFS4bits.SPI3TXIF;
            break;

        case uart3_fault:
            return IFS4bits.U3EIF;
            break;

        case uart3_receive_done:
            return IFS4bits.U3RXIF;
            break;

        case uart3_transfer_done:
            return IFS4bits.U3TXIF;
            break;

        case i2c3_bus_collision_event:
            return IFS5bits.I2C3BIF;
            break;

        case i2c3_client_event:
            return IFS5bits.I2C3SIF;
            break;

        case i2c3_host_event:
            return IFS5bits.I2C3MIF;
            break;

        case spi4_fault:
            return IFS5bits.SPI4EIF;
            break;

        case spi4_receive_done:
            return IFS5bits.SPI4RXIF;
            break;

        case spi4_transfer_done:
            return IFS5bits.SPI4TXIF;
            break;

        case real_time_clock:
            return IFS5bits.RTCCIF;
            break;

        case flash_control_event:
            return IFS5bits.FCEIF;
            break;

        case prefetch_module_sec_event:
            return IFS5bits.PREIF;
            break;

        case sqi1_event:
            return IFS5bits.SQI1IF;
            break;

        case uart4_fault:
            return IFS5bits.U4EIF;
            break;

        case uart4_receive_done:
            return IFS5bits.U4RXIF;
            break;

        case uart4_transfer_done:
            return IFS5bits.U4TXIF;
            break;

        case i2c4_bus_collision_event:
            return IFS5bits.I2C4BIF;
            break;

        case i2c4_client_event:
            return IFS5bits.I2C4SIF;
            break;

        case i2c4_host_event:
            return IFS5bits.I2C4MIF;
            break;

        case spi5_fault:
            return IFS5bits.SPI5EIF;
            break;

        case spi5_receive_done:
            return IFS5bits.SPI5RXIF;
            break;

        case spi5_transfer_done:
            return IFS5bits.SPI5TXIF;
            break;

        case uart5_fault:
            return IFS5bits.U5EIF;
            break;

        case uart5_receive_done:
            return IFS5bits.U5RXIF;
            break;

        case uart5_transfer_done:
            return IFS5bits.U5TXIF;
            break;

        case i2c5_bus_collision_event:
            return IFS5bits.I2C5BIF;
            break;

        case i2c5_client_event:
            return IFS5bits.I2C5SIF;
            break;

        case i2c5_host_event:
            return IFS5bits.I2C5MIF;
            break;

        case spi6_fault:
            return IFS5bits.SPI6IF;
            break;

        case spi6_receive_done:
            return IFS5bits.SPI6RXIF;
            break;

        case spi6_transfer_done:
            return IFS5bits.SPI6TX;
            break;

        case uart6_fault:
            return IFS5bits.U6EIF;
            break;

        case uart6_receive_done:
            return IFS5bits.U6RXIF;
            break;

        case uart6_transfer_done:
            return IFS5bits.U6TXIF;
            break;

        case sdhc_interrupt:
            return IFS5bits.SDHCIF;
            break;

        case glcd_interrupt:
            return IFS6bits.GLCDIF;
            break;

        case gpu_interrupt:
            return IFS6bits.GPUIF;
            break;

        case ctmu_interrupt:
            return IFS6bits.CTMUIF;
            break;

        case adc_end_of_scan:
            return IFS6bits.ADCEOSIF;
            break;

        case adc_analog_circuit_ready:
            return IFS6bits.ADCARDYIF;
            break;

        case adc_update_ready:
            return IFS6bits.ADCURDYIF;
            break;

        case adc0_early_interrupt:
            return IFS6bits.ADC0EIF;
            break;

        case adc1_early_interrupt:
            return IFS6bits.ADC1EIF;
            break;

        case adc2_early_interrupt:
            return IFS6bits.ADC2EIF;
            break;

        case adc3_early_interrupt:
            return IFS6bits.ADC3EIF;
            break;

        case adc4_early_interrupt:
            return IFS6bits.ADC4EIF;
            break;

        case adc_group_early_interrupt_request:
            return IFS6bits.ADCGRPIF;
            break;

        case adc7_early_interrupt:
            return IFS6bits.ADC7EIF;
            break;

        case adc0_warm_interrupt:
            return IFS6bits.ADC0WIF;
            break;

        case adc1_warm_interrupt:
            return IFS6bits.ADC1WIF;
            break;

        case adc2_warm_interrupt:
            return IFS6bits.ADC2WIF;
            break;

        case adc3_warm_interrupt:
            return IFS6bits.ADC3WIF;
            break;

        case adc4_warm_interrupt:
            return IFS6bits.ADC4WIF;
            break;

        case adc7_warm_interrupt:
            return IFS6bits.ADC7WIF;
            break;

        case mpll_fault_interrupt:
            return IFS6bits.MPLLFLTIF;
            break;
            
        default:
            return 0;
            break;
            
    }
    
}

// This function sets the priority for a given interrupt
void setInterruptPriority(interrupt_source_t input_interrupt, uint8_t input_priority) {
    
    // verify input priority is between 0 and 7
    if (input_priority > 7) {
        
        input_priority = 7;
    
    }
    
    // Decide which interrupt control bits to manipulate based on which interrupt
    // is having its priority set
    switch (input_interrupt) {

        case core_timer_interrupt:
            IPC0bits.CTIP = input_priority;
            break;

        case core_software_interrupt_0:
            IPC0bits.CS0IP = input_priority;
            break;

        case core_software_interrupt_1:
            IPC0bits.CS1IP = input_priority;
            break;

        case external_interrupt_0:
            IPC0bits.INT0IP = input_priority;
            break;

        case timer1:
            IPC1bits.T1IP = input_priority;
            break;

        case input_capture_1_error:
            IPC1bits.IC1EIP = input_priority;
            break;

        case input_capture_1:
            IPC1bits.IC1IP = input_priority;
            break;

        case output_compare_1:
            IPC1bits.OC1IP = input_priority;
            break;

        case external_interrupt_1:
            IPC2bits.INT1IP = input_priority;
            break;

        case timer2:
            IPC2bits.T2IP = input_priority;
            break;

        case input_capture_2_error:
            IPC2bits.IC2EIP = input_priority;
            break;

        case input_capture_2:
            IPC2bits.IC2IP = input_priority;
            break;

        case output_compare_2:
            IPC3bits.OC2IP = input_priority;
            break;

        case external_interrupt_2:
            IPC3bits.INT2IP = input_priority;
            break;

        case timer3:
            IPC3bits.T3IP = input_priority;
            break;

        case input_capture_3_error:
            IPC3bits.IC3EIP = input_priority;
            break;

        case input_capture_3:
            IPC4bits.IC3IP = input_priority;
            break;

        case output_compare_3:
            IPC4bits.OC3IP = input_priority;
            break;

        case external_interrupt_3:
            IPC4bits.INT3IP = input_priority;
            break;

        case timer4:
            IPC4bits.T4IP = input_priority;
            break;

        case input_capture_4_error:
            IPC5bits.IC4EIP = input_priority;
            break;

        case input_capture_4:
            IPC5bits.IC4IP = input_priority;
            break;

        case output_compare_4:
            IPC5bits.OC4IP = input_priority;
            break;

        case external_interrupt_4:
            IPC5bits.INT4IP = input_priority;
            break;

        case timer5:
            IPC6bits.T5IP = input_priority;
            break;

        case input_capture_5_error:
            IPC6bits.IC5EIP = input_priority;
            break;

        case input_capture_5:
            IPC6bits.IC5IP = input_priority;
            break;

        // IRQ 27 has no named IPC6 field on this device, so set it directly
        // IPC6<28:26> = priority, IPC6<25:24> = subpriority
        case output_compare_5:
            IPC6CLR = 0x7 << 26;
            IPC6SET = input_priority << 26;
            break;

        case timer6:
            IPC7bits.T6IP = input_priority;
            break;

        case input_capture_6_error:
            IPC7bits.IC6EIP = input_priority;
            break;

        case input_capture_6:
            IPC7bits.IC6IP = input_priority;
            break;

        case output_compare_6:
            IPC7bits.OC6IP = input_priority;
            break;

        case timer7:
            IPC8bits.T7IP = input_priority;
            break;

        case input_capture_7_error:
            IPC8bits.IC7EIP = input_priority;
            break;

        case input_capture_7:
            IPC8bits.IC7IP = input_priority;
            break;

        case output_compare_7:
            IPC8bits.OC7IP = input_priority;
            break;

        case timer8:
            IPC9bits.T8IP = input_priority;
            break;

        case input_capture_8_error:
            IPC9bits.IC8EIP = input_priority;
            break;

        case input_capture_8:
            IPC9bits.IC8IP = input_priority;
            break;

        case output_compare_8:
            IPC9bits.OC8IP = input_priority;
            break;

        case timer9:
            IPC10bits.T9IP = input_priority;
            break;

        case input_capture_9_error:
            IPC10bits.IC9EIP = input_priority;
            break;

        case input_capture_9:
            IPC10bits.IC9IP = input_priority;
            break;

        case output_compare_9:
            IPC10bits.OC9IP = input_priority;
            break;

        case adc_global_interrupt:
            IPC11bits.ADCIP = input_priority;
            break;

        case adc_fifo_interrupt:
            IPC11bits.ADCFIFOIP = input_priority;
            break;

        case adc_digital_comparator_1:
            IPC11bits.ADCDC1IP = input_priority;
            break;

        case adc_digital_comparator_2:
            IPC11bits.ADCDC2IP = input_priority;
            break;

        case adc_digital_comparator_3:
            IPC12bits.ADCDC3IP = input_priority;
            break;

        case adc_digital_comparator_4:
            IPC12bits.ADCDC4IP = input_priority;
            break;

        case adc_digital_comparator_5:
            IPC12bits.ADCDC5IP = input_priority;
            break;

        case adc_digital_comparator_6:
            IPC12bits.ADCDC6IP = input_priority;
            break;

        case adc_digital_filter_1:
            IPC13bits.ADCDF1IP = input_priority;
            break;

        case adc_digital_filter_2:
            IPC13bits.ADCDF2IP = input_priority;
            break;

        case adc_digital_filter_3:
            IPC13bits.ADCDF3IP = input_priority;
            break;

        case adc_digital_filter_4:
            IPC13bits.ADCDF4IP = input_priority;
            break;

        case adc_digital_filter_5:
            IPC14bits.ADCDF5IP = input_priority;
            break;

        case adc_digital_filter_6:
            IPC14bits.ADCDF6IP = input_priority;
            break;

        case adc_fault:
            IPC14bits.ADCFLTIP = input_priority;
            break;

        case adc_data_0:
            IPC14bits.ADCD0IP = input_priority;
            break;

        case adc_data_1:
            IPC15bits.ADCD1IP = input_priority;
            break;

        case adc_data_2:
            IPC15bits.ADCD2IP = input_priority;
            break;

        case adc_data_3:
            IPC15bits.ADCD3IP = input_priority;
            break;

        case adc_data_4:
            IPC15bits.ADCD4IP = input_priority;
            break;

        case adc_data_5:
            IPC16bits.ADCD5IP = input_priority;
            break;

        case adc_data_6:
            IPC16bits.ADCD6IP = input_priority;
            break;

        case adc_data_7:
            IPC16bits.ADCD7IP = input_priority;
            break;

        case adc_data_8:
            IPC16bits.ADCD8IP = input_priority;
            break;

        case adc_data_9:
            IPC17bits.ADCD9IP = input_priority;
            break;

        case adc_data_10:
            IPC17bits.ADCD10IP = input_priority;
            break;

        case adc_data_11:
            IPC17bits.ADCD11IP = input_priority;
            break;

        case adc_data_12:
            IPC17bits.ADCD12IP = input_priority;
            break;

        case adc_data_13:
            IPC18bits.ADCD13IP = input_priority;
            break;

        case adc_data_14:
            IPC18bits.ADCD14IP = input_priority;
            break;

        case adc_data_15:
            IPC18bits.ADCD15IP = input_priority;
            break;

        case adc_data_16:
            IPC18bits.ADCD16IP = input_priority;
            break;

        case adc_data_17:
            IPC19bits.ADCD17IP = input_priority;
            break;

        case adc_data_18:
            IPC19bits.ADCD18IP = input_priority;
            break;

        case adc_data_19:
            IPC19bits.ADCD19IP = input_priority;
            break;

        case adc_data_20:
            IPC19bits.ADCD20IP = input_priority;
            break;

        case adc_data_21:
            IPC20bits.ADCD21IP = input_priority;
            break;

        case adc_data_22:
            IPC20bits.ADCD22IP = input_priority;
            break;

        case adc_data_23:
            IPC20bits.ADCD23IP = input_priority;
            break;

        case adc_data_24:
            IPC20bits.ADCD24IP = input_priority;
            break;

        case adc_data_25:
            IPC21bits.ADCD25IP = input_priority;
            break;

        case adc_data_26:
            IPC21bits.ADCD26IP = input_priority;
            break;

        case adc_data_27:
            IPC21bits.ADCD27IP = input_priority;
            break;

        case adc_data_28:
            IPC21bits.ADCD28IP = input_priority;
            break;

        case adc_data_29:
            IPC22bits.ADCD29IP = input_priority;
            break;

        case adc_data_30:
            IPC22bits.ADCD30IP = input_priority;
            break;

        case adc_data_31:
            IPC22bits.ADCD31IP = input_priority;
            break;

        case adc_data_32:
            IPC22bits.ADCD32IP = input_priority;
            break;

        case adc_data_33:
            IPC23bits.ADCD33IP = input_priority;
            break;

        case adc_data_34:
            IPC23bits.ADCD34IP = input_priority;
            break;

        case adc_data_35:
            IPC23bits.ADCD35IP = input_priority;
            break;

        case adc_data_36:
            IPC23bits.ADCD36IP = input_priority;
            break;

        case adc_data_37:
            IPC24bits.ADCD37IP = input_priority;
            break;

        case adc_data_38:
            IPC24bits.ADCD38IP = input_priority;
            break;

        case adc_data_39:
            IPC24bits.ADCD39IP = input_priority;
            break;

        case adc_data_40:
            IPC24bits.ADCD40IP = input_priority;
            break;

        case adc_data_41:
            IPC25bits.ADCD41IP = input_priority;
            break;

        case adc_data_42:
            IPC25bits.ADCD42IP = input_priority;
            break;

        case adc_data_43:
            IPC25bits.ADCD43IP = input_priority;
            break;

        case usb_suspend_resume_event:
            IPC25bits.USBSRIP = input_priority;
            break;

        case core_performance_counter_interrupt:
            IPC26bits.CPCIP = input_priority;
            break;

        case core_fast_debug_channel_interrupt:
            IPC26bits.CFDCIP = input_priority;
            break;

        case system_bus_protection_violation:
            IPC26bits.SBIP = input_priority;
            break;

        case spi1_fault:
            IPC27bits.SPI1EIP = input_priority;
            break;

        case spi1_receive_done:
            IPC27bits.SPI1RXIP = input_priority;
            break;

        case spi1_transfer_done:
            IPC27bits.SPI1TXIP = input_priority;
            break;

        case uart1_fault:
            IPC28bits.U1EIP = input_priority;
            break;

        case uart1_receive_done:
            IPC28bits.U1RXIP = input_priority;
            break;

        case uart1_transfer_done:
            IPC28bits.U1TXIP = input_priority;
            break;

        case i2c1_bus_collision_event:
            IPC28bits.I2C1BIP = input_priority;
            break;

        case i2c1_client_event:
            IPC29bits.I2C1SIP = input_priority;
            break;

        case i2c1_host_event:
            IPC29bits.I2C1MIP = input_priority;
            break;

        case porta_input_change_interrupt:
            IPC29bits.CNAIP = input_priority;
            break;

        case portb_input_change_interrupt:
            IPC29bits.CNBIP = input_priority;
            break;

        case portc_input_change_interrupt:
            IPC30bits.CNCIP = input_priority;
            break;

        case portd_input_change_interrupt:
            IPC30bits.CNDIP = input_priority;
            break;

        case porte_input_change_interrupt:
            IPC30bits.CNEIP = input_priority;
            break;

        case portf_input_change_interrupt:
            IPC30bits.CNFIP = input_priority;
            break;

        case portg_input_change_interrupt:
            IPC31bits.CNGIP = input_priority;
            break;

        case porth_input_change_interrupt:
            IPC31bits.CNHIP = input_priority;
            break;

        case portj_input_change_interrupt:
            IPC31bits.CNJIP = input_priority;
            break;

        case portk_input_change_interrupt:
            IPC31bits.CNKIP = input_priority;
            break;

        case pmp:
            IPC32bits.PMPIP = input_priority;
            break;

        case pmp_error:
            IPC32bits.PMPEIP = input_priority;
            break;

        case comparator_1_interrupt:
            IPC32bits.CMP1IP = input_priority;
            break;

        case comparator_2_interrupt:
            IPC32bits.CMP2IP = input_priority;
            break;

        case usb_general_event:
            IPC33bits.USBIP = input_priority;
            break;

        case usb_dma_event:
            IPC33bits.USBDMAIP = input_priority;
            break;

        case dma_channel_0:
            IPC33bits.DMA0IP = input_priority;
            break;

        case dma_channel_1:
            IPC33bits.DMA1IP = input_priority;
            break;

        case dma_channel_2:
            IPC34bits.DMA2IP = input_priority;
            break;

        case dma_channel_3:
            IPC34bits.DMA3IP = input_priority;
            break;

        case dma_channel_4:
            IPC34bits.DMA4IP = input_priority;
            break;

        case dma_channel_5:
            IPC34bits.DMA5IP = input_priority;
            break;

        case dma_channel_6:
            IPC35bits.DMA6IP = input_priority;
            break;

        case dma_channel_7:
            IPC35bits.DMA7IP = input_priority;
            break;

        case spi2_fault:
            IPC35bits.SPI2EIP = input_priority;
            break;

        case spi2_receive_done:
            IPC35bits.SPI2RXIP = input_priority;
            break;

        case spi2_transfer_done:
            IPC36bits.SPI2TXIP = input_priority;
            break;

        case uart2_fault:
            IPC36bits.U2EIP = input_priority;
            break;

        case uart2_receive_done:
            IPC36bits.U2RXIP = input_priority;
            break;

        case uart2_transfer_done:
            IPC36bits.U2TXIP = input_priority;
            break;

        case i2c2_bus_collision_event:
            IPC37bits.I2C2BIP = input_priority;
            break;

        case i2c2_client_event:
            IPC37bits.I2C2SIP = input_priority;
            break;

        case i2c2_host_event:
            IPC37bits.I2C2MIP = input_priority;
            break;

        case control_area_network_1:
            IPC37bits.CAN1IP = input_priority;
            break;

        case control_area_network_2:
            IPC38bits.CAN2IP = input_priority;
            break;

        case ethernet_interrupt:
            IPC38bits.ETHIP = input_priority;
            break;

        case spi3_fault:
            IPC38bits.SPI3EIP = input_priority;
            break;

        case spi3_receive_done:
            IPC38bits.SPI3RXIP = input_priority;
            break;

        case spi3_transfer_done:
            IPC39bits.SPI3TXIP = input_priority;
            break;

        case uart3_fault:
            IPC39bits.U3EIP = input_priority;
            break;

        case uart3_receive_done:
            IPC39bits.U3RXIP = input_priority;
            break;

        case uart3_transfer_done:
            IPC39bits.U3TXIP = input_priority;
            break;

        case i2c3_bus_collision_event:
            IPC40bits.I2C3BIP = input_priority;
            break;

        case i2c3_client_event:
            IPC40bits.I2C3SIP = input_priority;
            break;

        case i2c3_host_event:
            IPC40bits.I2C3MIP = input_priority;
            break;

        case spi4_fault:
            IPC40bits.SPI4EIP = input_priority;
            break;

        case spi4_receive_done:
            IPC41bits.SPI4RXIP = input_priority;
            break;

        case spi4_transfer_done:
            IPC41bits.SPI4TXIP = input_priority;
            break;

        case real_time_clock:
            IPC41bits.RTCCIP = input_priority;
            break;

        case flash_control_event:
            IPC41bits.FCEIP = input_priority;
            break;

        case prefetch_module_sec_event:
            IPC42bits.PREIP = input_priority;
            break;

        case sqi1_event:
            IPC42bits.SQI1IP = input_priority;
            break;

        case uart4_fault:
            IPC42bits.U4EIP = input_priority;
            break;

        case uart4_receive_done:
            IPC42bits.U4RXIP = input_priority;
            break;

        case uart4_transfer_done:
            IPC43bits.U4TXIP = input_priority;
            break;

        case i2c4_bus_collision_event:
            IPC43bits.I2C4BIP = input_priority;
            break;

        case i2c4_client_event:
            IPC43bits.I2C4SIP = input_priority;
            break;

        case i2c4_host_event:
            IPC43bits.I2C4MIP = input_priority;
            break;

        case spi5_fault:
            IPC44bits.SPI5EIP = input_priority;
            break;

        case spi5_receive_done:
            IPC44bits.SPI5RXIP = input_priority;
            break;

        case spi5_transfer_done:
            IPC44bits.SPI5TXIP = input_priority;
            break;

        case uart5_fault:
            IPC44bits.U5EIP = input_priority;
            break;

        case uart5_receive_done:
            IPC45bits.U5RXIP = input_priority;
            break;

        case uart5_transfer_done:
            IPC45bits.U5TXIP = input_priority;
            break;

        case i2c5_bus_collision_event:
            IPC45bits.I2C5BIP = input_priority;
            break;

        case i2c5_client_event:
            IPC45bits.I2C5SIP = input_priority;
            break;

        case i2c5_host_event:
            IPC46bits.I2C5MIP = input_priority;
            break;

        case spi6_fault:
            IPC46bits.SPI6EIP = input_priority;
            break;

        case spi6_receive_done:
            IPC46bits.SPI6RXIP = input_priority;
            break;

        case spi6_transfer_done:
            IPC46bits.SPI6TXIP = input_priority;
            break;

        case uart6_fault:
            IPC47bits.U6EIP = input_priority;
            break;

        case uart6_receive_done:
            IPC47bits.U6RXIP = input_priority;
            break;

        case uart6_transfer_done:
            IPC47bits.U6TXIP = input_priority;
            break;

        // IRQ 191 has no named IPC47 field on this device, so set it directly
        // IPC47<28:26> = priority, IPC47<25:24> = subpriority
        case sdhc_interrupt:
            IPC47CLR = 0x7 << 26;
            IPC47SET = input_priority << 26;
            break;

        case glcd_interrupt:
            IPC48bits.GLCDIP = input_priority;
            break;

        case gpu_interrupt:
            IPC48bits.GPUIP = input_priority;
            break;

        case ctmu_interrupt:
            IPC48bits.CTMUIP = input_priority;
            break;

        case adc_end_of_scan:
            IPC49bits.ADCEOSIP = input_priority;
            break;

        case adc_analog_circuit_ready:
            IPC49bits.ADCARDYIP = input_priority;
            break;

        case adc_update_ready:
            IPC49bits.ADCURDYIP = input_priority;
            break;

        case adc0_early_interrupt:
            IPC49bits.ADC0EIP = input_priority;
            break;

        case adc1_early_interrupt:
            IPC50bits.ADC1EIP = input_priority;
            break;

        case adc2_early_interrupt:
            IPC50bits.ADC2EIP = input_priority;
            break;

        case adc3_early_interrupt:
            IPC50bits.ADC3EIP = input_priority;
            break;

        case adc4_early_interrupt:
            IPC50bits.ADC4EIP = input_priority;
            break;

        case adc_group_early_interrupt_request:
            IPC51bits.ADCGRPEIP = input_priority;
            break;

        case adc7_early_interrupt:
            IPC51bits.ADC7EIP = input_priority;
            break;

        case adc0_warm_interrupt:
            IPC51bits.ADC0WIP = input_priority;
            break;

        case adc1_warm_interrupt:
            IPC52bits.ADC1WIP = input_priority;
            break;

        case adc2_warm_interrupt:
            IPC52bits.ADC2WIP = input_priority;
            break;

        case adc3_warm_interrupt:
            IPC52bits.ADC3WIP = input_priority;
            break;

        case adc4_warm_interrupt:
            IPC52bits.ADC4WIP = input_priority;
            break;

        case adc7_warm_interrupt:
            IPC53bits.ADC7WIP = input_priority;
            break;

        case mpll_fault_interrupt:
            IPC53bits.MPLLFLTIP = input_priority;
            break;
            
        default:
            break;
            
    }
    
}

// This function sets the subpriority for a given interrupt
void setInterruptSubpriority(interrupt_source_t input_interrupt, uint8_t input_subpriority) {

    // check if input subpriority is valid
    if (input_subpriority > 3) {
     
        input_subpriority = 3;
        
    }
    
    // Decide which interrupt control bits to manipulate based on which interrupt
    // is having its priority set
    switch (input_interrupt) {


        case core_timer_interrupt:
            IPC0bits.CTIS = input_subpriority;
            break;

        case core_software_interrupt_0:
            IPC0bits.CS0IS = input_subpriority;
            break;

        case core_software_interrupt_1:
            IPC0bits.CS1IS = input_subpriority;
            break;

        case external_interrupt_0:
            IPC0bits.INT0IS = input_subpriority;
            break;

        case timer1:
            IPC1bits.T1IS = input_subpriority;
            break;

        case input_capture_1_error:
            IPC1bits.IC1EIS = input_subpriority;
            break;

        case input_capture_1:
            IPC1bits.IC1IS = input_subpriority;
            break;

        case output_compare_1:
            IPC1bits.OC1IS = input_subpriority;
            break;

        case external_interrupt_1:
            IPC2bits.INT1IS = input_subpriority;
            break;

        case timer2:
            IPC2bits.T2IS = input_subpriority;
            break;

        case input_capture_2_error:
            IPC2bits.IC2EIS = input_subpriority;
            break;

        case input_capture_2:
            IPC2bits.IC2IS = input_subpriority;
            break;

        case output_compare_2:
            IPC3bits.OC2IS = input_subpriority;
            break;

        case external_interrupt_2:
            IPC3bits.INT2IS = input_subpriority;
            break;

        case timer3:
            IPC3bits.T3IS = input_subpriority;
            break;

        case input_capture_3_error:
            IPC3bits.IC3EIS = input_subpriority;
            break;

        case input_capture_3:
            IPC4bits.IC3IS = input_subpriority;
            break;

        case output_compare_3:
            IPC4bits.OC3IS = input_subpriority;
            break;

        case external_interrupt_3:
            IPC4bits.INT3IS = input_subpriority;
            break;

        case timer4:
            IPC4bits.T4IS = input_subpriority;
            break;

        case input_capture_4_error:
            IPC5bits.IC4EIS = input_subpriority;
            break;

        case input_capture_4:
            IPC5bits.IC4IS = input_subpriority;
            break;

        case output_compare_4:
            IPC5bits.OC4IS = input_subpriority;
            break;

        case external_interrupt_4:
            IPC5bits.INT4IS = input_subpriority;
            break;

        case timer5:
            IPC6bits.T5IS = input_subpriority;
            break;

        case input_capture_5_error:
            IPC6bits.IC5EIS = input_subpriority;
            break;

        case input_capture_5:
            IPC6bits.IC5IS = input_subpriority;
            break;

        // IRQ 27 has no named IPC6 field on this device, so set it directly
        // IPC6<28:26> = priority, IPC6<25:24> = subpriority
        case output_compare_5:
            IPC6CLR = 0x3 << 24;
            IPC6SET = input_subpriority << 24;
            break;

        case timer6:
            IPC7bits.T6IS = input_subpriority;
            break;

        case input_capture_6_error:
            IPC7bits.IC6EIS = input_subpriority;
            break;

        case input_capture_6:
            IPC7bits.IC6IS = input_subpriority;
            break;

        case output_compare_6:
            IPC7bits.OC6IS = input_subpriority;
            break;

        case timer7:
            IPC8bits.T7IS = input_subpriority;
            break;

        case input_capture_7_error:
            IPC8bits.IC7EIS = input_subpriority;
            break;

        case input_capture_7:
            IPC8bits.IC7IS = input_subpriority;
            break;

        case output_compare_7:
            IPC8bits.OC7IS = input_subpriority;
            break;

        case timer8:
            IPC9bits.T8IS = input_subpriority;
            break;

        case input_capture_8_error:
            IPC9bits.IC8EIS = input_subpriority;
            break;

        case input_capture_8:
            IPC9bits.IC8IS = input_subpriority;
            break;

        case output_compare_8:
            IPC9bits.OC8IS = input_subpriority;
            break;

        case timer9:
            IPC10bits.T9IS = input_subpriority;
            break;

        case input_capture_9_error:
            IPC10bits.IC9EIS = input_subpriority;
            break;

        case input_capture_9:
            IPC10bits.IC9IS = input_subpriority;
            break;

        case output_compare_9:
            IPC10bits.OC9IS = input_subpriority;
            break;

        case adc_global_interrupt:
            IPC11bits.ADCIS = input_subpriority;
            break;

        case adc_fifo_interrupt:
            IPC11bits.ADCFIFOIS = input_subpriority;
            break;

        case adc_digital_comparator_1:
            IPC11bits.ADCDC1IS = input_subpriority;
            break;

        case adc_digital_comparator_2:
            IPC11bits.ADCDC2IS = input_subpriority;
            break;

        case adc_digital_comparator_3:
            IPC12bits.ADCDC3IS = input_subpriority;
            break;

        case adc_digital_comparator_4:
            IPC12bits.ADCDC4IS = input_subpriority;
            break;

        case adc_digital_comparator_5:
            IPC12bits.ADCDC5IS = input_subpriority;
            break;

        case adc_digital_comparator_6:
            IPC12bits.ADCDC6IS = input_subpriority;
            break;

        case adc_digital_filter_1:
            IPC13bits.ADCDF1IS = input_subpriority;
            break;

        case adc_digital_filter_2:
            IPC13bits.ADCDF2IS = input_subpriority;
            break;

        case adc_digital_filter_3:
            IPC13bits.ADCDF3IS = input_subpriority;
            break;

        case adc_digital_filter_4:
            IPC13bits.ADCDF4IS = input_subpriority;
            break;

        case adc_digital_filter_5:
            IPC14bits.ADCDF5IS = input_subpriority;
            break;

        case adc_digital_filter_6:
            IPC14bits.ADCDF6IS = input_subpriority;
            break;

        case adc_fault:
            IPC14bits.ADCFLTIS = input_subpriority;
            break;

        case adc_data_0:
            IPC14bits.ADCD0IS = input_subpriority;
            break;

        case adc_data_1:
            IPC15bits.ADCD1IS = input_subpriority;
            break;

        case adc_data_2:
            IPC15bits.ADCD2IS = input_subpriority;
            break;

        case adc_data_3:
            IPC15bits.ADCD3IS = input_subpriority;
            break;

        case adc_data_4:
            IPC15bits.ADCD4IS = input_subpriority;
            break;

        case adc_data_5:
            IPC16bits.ADCD5IS = input_subpriority;
            break;

        case adc_data_6:
            IPC16bits.ADCD6IS = input_subpriority;
            break;

        case adc_data_7:
            IPC16bits.ADCD7IS = input_subpriority;
            break;

        case adc_data_8:
            IPC16bits.ADCD8IS = input_subpriority;
            break;

        case adc_data_9:
            IPC17bits.ADCD9IS = input_subpriority;
            break;

        case adc_data_10:
            IPC17bits.ADCD10IS = input_subpriority;
            break;

        case adc_data_11:
            IPC17bits.ADCD11IS = input_subpriority;
            break;

        case adc_data_12:
            IPC17bits.ADCD12IS = input_subpriority;
            break;

        case adc_data_13:
            IPC18bits.ADCD13IS = input_subpriority;
            break;

        case adc_data_14:
            IPC18bits.ADCD14IS = input_subpriority;
            break;

        case adc_data_15:
            IPC18bits.ADCD15IS = input_subpriority;
            break;

        case adc_data_16:
            IPC18bits.ADCD16IS = input_subpriority;
            break;

        case adc_data_17:
            IPC19bits.ADCD17IS = input_subpriority;
            break;

        case adc_data_18:
            IPC19bits.ADCD18IS = input_subpriority;
            break;

        case adc_data_19:
            IPC19bits.ADCD19IS = input_subpriority;
            break;

        case adc_data_20:
            IPC19bits.ADCD20IS = input_subpriority;
            break;

        case adc_data_21:
            IPC20bits.ADCD21IS = input_subpriority;
            break;

        case adc_data_22:
            IPC20bits.ADCD22IS = input_subpriority;
            break;

        case adc_data_23:
            IPC20bits.ADCD23IS = input_subpriority;
            break;

        case adc_data_24:
            IPC20bits.ADCD24IS = input_subpriority;
            break;

        case adc_data_25:
            IPC21bits.ADCD25IS = input_subpriority;
            break;

        case adc_data_26:
            IPC21bits.ADCD26IS = input_subpriority;
            break;

        case adc_data_27:
            IPC21bits.ADCD27IS = input_subpriority;
            break;

        case adc_data_28:
            IPC21bits.ADCD28IS = input_subpriority;
            break;

        case adc_data_29:
            IPC22bits.ADCD29IS = input_subpriority;
            break;

        case adc_data_30:
            IPC22bits.ADCD30IS = input_subpriority;
            break;

        case adc_data_31:
            IPC22bits.ADCD31IS = input_subpriority;
            break;

        case adc_data_32:
            IPC22bits.ADCD32IS = input_subpriority;
            break;

        case adc_data_33:
            IPC23bits.ADCD33IS = input_subpriority;
            break;

        case adc_data_34:
            IPC23bits.ADCD34IS = input_subpriority;
            break;

        case adc_data_35:
            IPC23bits.ADCD35IS = input_subpriority;
            break;

        case adc_data_36:
            IPC23bits.ADCD36IS = input_subpriority;
            break;

        case adc_data_37:
            IPC24bits.ADCD37IS = input_subpriority;
            break;

        case adc_data_38:
            IPC24bits.ADCD38IS = input_subpriority;
            break;

        case adc_data_39:
            IPC24bits.ADCD39IS = input_subpriority;
            break;

        case adc_data_40:
            IPC24bits.ADCD40IS = input_subpriority;
            break;

        case adc_data_41:
            IPC25bits.ADCD41IS = input_subpriority;
            break;

        case adc_data_42:
            IPC25bits.ADCD42IS = input_subpriority;
            break;

        case adc_data_43:
            IPC25bits.ADCD43IS = input_subpriority;
            break;

        case usb_suspend_resume_event:
            IPC25bits.USBSRIS = input_subpriority;
            break;

        case core_performance_counter_interrupt:
            IPC26bits.CPCIS = input_subpriority;
            break;

        case core_fast_debug_channel_interrupt:
            IPC26bits.CFDCIS = input_subpriority;
            break;

        case system_bus_protection_violation:
            IPC26bits.SBIS = input_subpriority;
            break;

        case spi1_fault:
            IPC27bits.SPI1EIS = input_subpriority;
            break;

        case spi1_receive_done:
            IPC27bits.SPI1RXIS = input_subpriority;
            break;

        case spi1_transfer_done:
            IPC27bits.SPI1TXIS = input_subpriority;
            break;

        case uart1_fault:
            IPC28bits.U1EIS = input_subpriority;
            break;

        case uart1_receive_done:
            IPC28bits.U1RXIS = input_subpriority;
            break;

        case uart1_transfer_done:
            IPC28bits.U1TXIS = input_subpriority;
            break;

        case i2c1_bus_collision_event:
            IPC28bits.I2C1BIS = input_subpriority;
            break;

        case i2c1_client_event:
            IPC29bits.I2C1SIS = input_subpriority;
            break;

        case i2c1_host_event:
            IPC29bits.I2C1MIS = input_subpriority;
            break;

        case porta_input_change_interrupt:
            IPC29bits.CNAIS = input_subpriority;
            break;

        case portb_input_change_interrupt:
            IPC29bits.CNBIS = input_subpriority;
            break;

        case portc_input_change_interrupt:
            IPC30bits.CNCIS = input_subpriority;
            break;

        case portd_input_change_interrupt:
            IPC30bits.CNDIS = input_subpriority;
            break;

        case porte_input_change_interrupt:
            IPC30bits.CNEIS = input_subpriority;
            break;

        case portf_input_change_interrupt:
            IPC30bits.CNFIS = input_subpriority;
            break;

        case portg_input_change_interrupt:
            IPC31bits.CNGIS = input_subpriority;
            break;

        case porth_input_change_interrupt:
            IPC31bits.CNHIS = input_subpriority;
            break;

        case portj_input_change_interrupt:
            IPC31bits.CNJIS = input_subpriority;
            break;

        case portk_input_change_interrupt:
            IPC31bits.CNKIS = input_subpriority;
            break;

        case pmp:
            IPC32bits.PMPIS = input_subpriority;
            break;

        case pmp_error:
            IPC32bits.PMPEIS = input_subpriority;
            break;

        case comparator_1_interrupt:
            IPC32bits.CMP1IS = input_subpriority;
            break;

        case comparator_2_interrupt:
            IPC32bits.CMP2IS = input_subpriority;
            break;

        case usb_general_event:
            IPC33bits.USBIS = input_subpriority;
            break;

        case usb_dma_event:
            IPC33bits.USBDMAIS = input_subpriority;
            break;

        case dma_channel_0:
            IPC33bits.DMA0IS = input_subpriority;
            break;

        case dma_channel_1:
            IPC33bits.DMA1IS = input_subpriority;
            break;

        case dma_channel_2:
            IPC34bits.DMA2IS = input_subpriority;
            break;

        case dma_channel_3:
            IPC34bits.DMA3IS = input_subpriority;
            break;

        case dma_channel_4:
            IPC34bits.DMA4IS = input_subpriority;
            break;

        case dma_channel_5:
            IPC34bits.DMA5IS = input_subpriority;
            break;

        case dma_channel_6:
            IPC35bits.DMA6IS = input_subpriority;
            break;

        case dma_channel_7:
            IPC35bits.DMA7IS = input_subpriority;
            break;

        case spi2_fault:
            IPC35bits.SPI2EIS = input_subpriority;
            break;

        case spi2_receive_done:
            IPC35bits.SPI2RXIS = input_subpriority;
            break;

        case spi2_transfer_done:
            IPC36bits.SPI2TXIS = input_subpriority;
            break;

        case uart2_fault:
            IPC36bits.U2EIS = input_subpriority;
            break;

        case uart2_receive_done:
            IPC36bits.U2RXIS = input_subpriority;
            break;

        case uart2_transfer_done:
            IPC36bits.U2TXIS = input_subpriority;
            break;

        case i2c2_bus_collision_event:
            IPC37bits.I2C2BIS = input_subpriority;
            break;

        case i2c2_client_event:
            IPC37bits.I2C2SIS = input_subpriority;
            break;

        case i2c2_host_event:
            IPC37bits.I2C2MIS = input_subpriority;
            break;

        case control_area_network_1:
            IPC37bits.CAN1IS = input_subpriority;
            break;

        case control_area_network_2:
            IPC38bits.CAN2IS = input_subpriority;
            break;

        case ethernet_interrupt:
            IPC38bits.ETHIS = input_subpriority;
            break;

        case spi3_fault:
            IPC38bits.SPI3EIS = input_subpriority;
            break;

        case spi3_receive_done:
            IPC38bits.SPI3RXIS = input_subpriority;
            break;

        case spi3_transfer_done:
            IPC39bits.SPI3TXIS = input_subpriority;
            break;

        case uart3_fault:
            IPC39bits.U3EIS = input_subpriority;
            break;

        case uart3_receive_done:
            IPC39bits.U3RXIS = input_subpriority;
            break;

        case uart3_transfer_done:
            IPC39bits.U3TXIS = input_subpriority;
            break;

        case i2c3_bus_collision_event:
            IPC40bits.I2C3BIS = input_subpriority;
            break;

        case i2c3_client_event:
            IPC40bits.I2C3SIS = input_subpriority;
            break;

        case i2c3_host_event:
            IPC40bits.I2C3MIS = input_subpriority;
            break;

        case spi4_fault:
            IPC40bits.SPI4EIS = input_subpriority;
            break;

        case spi4_receive_done:
            IPC41bits.SPI4RXIS = input_subpriority;
            break;

        case spi4_transfer_done:
            IPC41bits.SPI4TXIS = input_subpriority;
            break;

        case real_time_clock:
            IPC41bits.RTCCIS = input_subpriority;
            break;

        case flash_control_event:
            IPC41bits.FCEIS = input_subpriority;
            break;

        case prefetch_module_sec_event:
            IPC42bits.PREIS = input_subpriority;
            break;

        case sqi1_event:
            IPC42bits.SQI1IS = input_subpriority;
            break;

        case uart4_fault:
            IPC42bits.U4EIS = input_subpriority;
            break;

        case uart4_receive_done:
            IPC42bits.U4RXIS = input_subpriority;
            break;

        case uart4_transfer_done:
            IPC43bits.U4TXIS = input_subpriority;
            break;

        case i2c4_bus_collision_event:
            IPC43bits.I2C4BIS = input_subpriority;
            break;

        case i2c4_client_event:
            IPC43bits.I2C4SIS = input_subpriority;
            break;

        case i2c4_host_event:
            IPC43bits.I2C4MIS = input_subpriority;
            break;

        case spi5_fault:
            IPC44bits.SPI5EIS = input_subpriority;
            break;

        case spi5_receive_done:
            IPC44bits.SPI5RXIS = input_subpriority;
            break;

        case spi5_transfer_done:
            IPC44bits.SPI5TXIS = input_subpriority;
            break;

        case uart5_fault:
            IPC44bits.U5EIS = input_subpriority;
            break;

        case uart5_receive_done:
            IPC45bits.U5RXIS = input_subpriority;
            break;

        case uart5_transfer_done:
            IPC45bits.U5TXIS = input_subpriority;
            break;

        case i2c5_bus_collision_event:
            IPC45bits.I2C5BIS = input_subpriority;
            break;

        case i2c5_client_event:
            IPC45bits.I2C5SIS = input_subpriority;
            break;

        case i2c5_host_event:
            IPC46bits.I2C5MIS = input_subpriority;
            break;

        case spi6_fault:
            IPC46bits.SPI6EIS = input_subpriority;
            break;

        case spi6_receive_done:
            IPC46bits.SPI6RXIS = input_subpriority;
            break;

        case spi6_transfer_done:
            IPC46bits.SPI6TXIS = input_subpriority;
            break;

        case uart6_fault:
            IPC47bits.U6EIS = input_subpriority;
            break;

        case uart6_receive_done:
            IPC47bits.U6RXIS = input_subpriority;
            break;

        case uart6_transfer_done:
            IPC47bits.U6TXIS = input_subpriority;
            break;

        // IRQ 191 has no named IPC47 field on this device, so set it directly
        // IPC47<28:26> = priority, IPC47<25:24> = subpriority
        case sdhc_interrupt:
            IPC47CLR = 0x3 << 24;
            IPC47SET = input_subpriority << 24;
            break;

        case glcd_interrupt:
            IPC48bits.GLCDIS = input_subpriority;
            break;

        case gpu_interrupt:
            IPC48bits.GPUIS = input_subpriority;
            break;

        case ctmu_interrupt:
            IPC48bits.CTMUIS = input_subpriority;
            break;

        case adc_end_of_scan:
            IPC49bits.ADCEOSIS = input_subpriority;
            break;

        case adc_analog_circuit_ready:
            IPC49bits.ADCARDYIS = input_subpriority;
            break;

        case adc_update_ready:
            IPC49bits.ADCURDYIS = input_subpriority;
            break;

        case adc0_early_interrupt:
            IPC49bits.ADC0EIS = input_subpriority;
            break;

        case adc1_early_interrupt:
            IPC50bits.ADC1EIS = input_subpriority;
            break;

        case adc2_early_interrupt:
            IPC50bits.ADC2EIS = input_subpriority;
            break;

        case adc3_early_interrupt:
            IPC50bits.ADC3EIS = input_subpriority;
            break;

        case adc4_early_interrupt:
            IPC50bits.ADC4EIS = input_subpriority;
            break;

        case adc_group_early_interrupt_request:
            IPC51bits.ADCGRPEIS = input_subpriority;
            break;

        case adc7_early_interrupt:
            IPC51bits.ADC7EIS = input_subpriority;
            break;

        case adc0_warm_interrupt:
            IPC51bits.ADC0WIS = input_subpriority;
            break;

        case adc1_warm_interrupt:
            IPC52bits.ADC1WIS = input_subpriority;
            break;

        case adc2_warm_interrupt:
            IPC52bits.ADC2WIS = input_subpriority;
            break;

        case adc3_warm_interrupt:
            IPC52bits.ADC3WIS = input_subpriority;
            break;

        case adc4_warm_interrupt:
            IPC52bits.ADC4WIS = input_subpriority;
            break;

        case adc7_warm_interrupt:
            IPC53bits.ADC7WIS = input_subpriority;
            break;

        case mpll_fault_interrupt:
            IPC53bits.MPLLFLTIS = input_subpriority;
            break;
            
        default:
            break;
            
    }
    
    
    

}

// This function returns the given interrupt priority
uint8_t getInterruptPriority(interrupt_source_t input_interrupt) {
 
    // Decide which interrupt control bits to return based on which interrupt
    switch (input_interrupt) {

        case core_timer_interrupt:
            return IPC0bits.CTIP;
            break;

        case core_software_interrupt_0:
            return IPC0bits.CS0IP;
            break;

        case core_software_interrupt_1:
            return IPC0bits.CS1IP;
            break;

        case external_interrupt_0:
            return IPC0bits.INT0IP;
            break;

        case timer1:
            return IPC1bits.T1IP;
            break;

        case input_capture_1_error:
            return IPC1bits.IC1EIP;
            break;

        case input_capture_1:
            return IPC1bits.IC1IP;
            break;

        case output_compare_1:
            return IPC1bits.OC1IP;
            break;

        case external_interrupt_1:
            return IPC2bits.INT1IP;
            break;

        case timer2:
            return IPC2bits.T2IP;
            break;

        case input_capture_2_error:
            return IPC2bits.IC2EIP;
            break;

        case input_capture_2:
            return IPC2bits.IC2IP;
            break;

        case output_compare_2:
            return IPC3bits.OC2IP;
            break;

        case external_interrupt_2:
            return IPC3bits.INT2IP;
            break;

        case timer3:
            return IPC3bits.T3IP;
            break;

        case input_capture_3_error:
            return IPC3bits.IC3EIP;
            break;

        case input_capture_3:
            return IPC4bits.IC3IP;
            break;

        case output_compare_3:
            return IPC4bits.OC3IP;
            break;

        case external_interrupt_3:
            return IPC4bits.INT3IP;
            break;

        case timer4:
            return IPC4bits.T4IP;
            break;

        case input_capture_4_error:
            return IPC5bits.IC4EIP;
            break;

        case input_capture_4:
            return IPC5bits.IC4IP;
            break;

        case output_compare_4:
            return IPC5bits.OC4IP;
            break;

        case external_interrupt_4:
            return IPC5bits.INT4IP;
            break;

        case timer5:
            return IPC6bits.T5IP;
            break;

        case input_capture_5_error:
            return IPC6bits.IC5EIP;
            break;

        case input_capture_5:
            return IPC6bits.IC5IP;
            break;

        // IRQ 27 has no named IPC6 field on this device, read it directly
        case output_compare_5:
            return (IPC6 >> 26) & 0x7;
            break;

        case timer6:
            return IPC7bits.T6IP;
            break;

        case input_capture_6_error:
            return IPC7bits.IC6EIP;
            break;

        case input_capture_6:
            return IPC7bits.IC6IP;
            break;

        case output_compare_6:
            return IPC7bits.OC6IP;
            break;

        case timer7:
            return IPC8bits.T7IP;
            break;

        case input_capture_7_error:
            return IPC8bits.IC7EIP;
            break;

        case input_capture_7:
            return IPC8bits.IC7IP;
            break;

        case output_compare_7:
            return IPC8bits.OC7IP;
            break;

        case timer8:
            return IPC9bits.T8IP;
            break;

        case input_capture_8_error:
            return IPC9bits.IC8EIP;
            break;

        case input_capture_8:
            return IPC9bits.IC8IP;
            break;

        case output_compare_8:
            return IPC9bits.OC8IP;
            break;

        case timer9:
            return IPC10bits.T9IP;
            break;

        case input_capture_9_error:
            return IPC10bits.IC9EIP;
            break;

        case input_capture_9:
            return IPC10bits.IC9IP;
            break;

        case output_compare_9:
            return IPC10bits.OC9IP;
            break;

        case adc_global_interrupt:
            return IPC11bits.ADCIP;
            break;

        case adc_fifo_interrupt:
            return IPC11bits.ADCFIFOIP;
            break;

        case adc_digital_comparator_1:
            return IPC11bits.ADCDC1IP;
            break;

        case adc_digital_comparator_2:
            return IPC11bits.ADCDC2IP;
            break;

        case adc_digital_comparator_3:
            return IPC12bits.ADCDC3IP;
            break;

        case adc_digital_comparator_4:
            return IPC12bits.ADCDC4IP;
            break;

        case adc_digital_comparator_5:
            return IPC12bits.ADCDC5IP;
            break;

        case adc_digital_comparator_6:
            return IPC12bits.ADCDC6IP;
            break;

        case adc_digital_filter_1:
            return IPC13bits.ADCDF1IP;
            break;

        case adc_digital_filter_2:
            return IPC13bits.ADCDF2IP;
            break;

        case adc_digital_filter_3:
            return IPC13bits.ADCDF3IP;
            break;

        case adc_digital_filter_4:
            return IPC13bits.ADCDF4IP;
            break;

        case adc_digital_filter_5:
            return IPC14bits.ADCDF5IP;
            break;

        case adc_digital_filter_6:
            return IPC14bits.ADCDF6IP;
            break;

        case adc_fault:
            return IPC14bits.ADCFLTIP;
            break;

        case adc_data_0:
            return IPC14bits.ADCD0IP;
            break;

        case adc_data_1:
            return IPC15bits.ADCD1IP;
            break;

        case adc_data_2:
            return IPC15bits.ADCD2IP;
            break;

        case adc_data_3:
            return IPC15bits.ADCD3IP;
            break;

        case adc_data_4:
            return IPC15bits.ADCD4IP;
            break;

        case adc_data_5:
            return IPC16bits.ADCD5IP;
            break;

        case adc_data_6:
            return IPC16bits.ADCD6IP;
            break;

        case adc_data_7:
            return IPC16bits.ADCD7IP;
            break;

        case adc_data_8:
            return IPC16bits.ADCD8IP;
            break;

        case adc_data_9:
            return IPC17bits.ADCD9IP;
            break;

        case adc_data_10:
            return IPC17bits.ADCD10IP;
            break;

        case adc_data_11:
            return IPC17bits.ADCD11IP;
            break;

        case adc_data_12:
            return IPC17bits.ADCD12IP;
            break;

        case adc_data_13:
            return IPC18bits.ADCD13IP;
            break;

        case adc_data_14:
            return IPC18bits.ADCD14IP;
            break;

        case adc_data_15:
            return IPC18bits.ADCD15IP;
            break;

        case adc_data_16:
            return IPC18bits.ADCD16IP;
            break;

        case adc_data_17:
            return IPC19bits.ADCD17IP;
            break;

        case adc_data_18:
            return IPC19bits.ADCD18IP;
            break;

        case adc_data_19:
            return IPC19bits.ADCD19IP;
            break;

        case adc_data_20:
            return IPC19bits.ADCD20IP;
            break;

        case adc_data_21:
            return IPC20bits.ADCD21IP;
            break;

        case adc_data_22:
            return IPC20bits.ADCD22IP;
            break;

        case adc_data_23:
            return IPC20bits.ADCD23IP;
            break;

        case adc_data_24:
            return IPC20bits.ADCD24IP;
            break;

        case adc_data_25:
            return IPC21bits.ADCD25IP;
            break;

        case adc_data_26:
            return IPC21bits.ADCD26IP;
            break;

        case adc_data_27:
            return IPC21bits.ADCD27IP;
            break;

        case adc_data_28:
            return IPC21bits.ADCD28IP;
            break;

        case adc_data_29:
            return IPC22bits.ADCD29IP;
            break;

        case adc_data_30:
            return IPC22bits.ADCD30IP;
            break;

        case adc_data_31:
            return IPC22bits.ADCD31IP;
            break;

        case adc_data_32:
            return IPC22bits.ADCD32IP;
            break;

        case adc_data_33:
            return IPC23bits.ADCD33IP;
            break;

        case adc_data_34:
            return IPC23bits.ADCD34IP;
            break;

        case adc_data_35:
            return IPC23bits.ADCD35IP;
            break;

        case adc_data_36:
            return IPC23bits.ADCD36IP;
            break;

        case adc_data_37:
            return IPC24bits.ADCD37IP;
            break;

        case adc_data_38:
            return IPC24bits.ADCD38IP;
            break;

        case adc_data_39:
            return IPC24bits.ADCD39IP;
            break;

        case adc_data_40:
            return IPC24bits.ADCD40IP;
            break;

        case adc_data_41:
            return IPC25bits.ADCD41IP;
            break;

        case adc_data_42:
            return IPC25bits.ADCD42IP;
            break;

        case adc_data_43:
            return IPC25bits.ADCD43IP;
            break;

        case usb_suspend_resume_event:
            return IPC25bits.USBSRIP;
            break;

        case core_performance_counter_interrupt:
            return IPC26bits.CPCIP;
            break;

        case core_fast_debug_channel_interrupt:
            return IPC26bits.CFDCIP;
            break;

        case system_bus_protection_violation:
            return IPC26bits.SBIP;
            break;

        case spi1_fault:
            return IPC27bits.SPI1EIP;
            break;

        case spi1_receive_done:
            return IPC27bits.SPI1RXIP;
            break;

        case spi1_transfer_done:
            return IPC27bits.SPI1TXIP;
            break;

        case uart1_fault:
            return IPC28bits.U1EIP;
            break;

        case uart1_receive_done:
            return IPC28bits.U1RXIP;
            break;

        case uart1_transfer_done:
            return IPC28bits.U1TXIP;
            break;

        case i2c1_bus_collision_event:
            return IPC28bits.I2C1BIP;
            break;

        case i2c1_client_event:
            return IPC29bits.I2C1SIP;
            break;

        case i2c1_host_event:
            return IPC29bits.I2C1MIP;
            break;

        case porta_input_change_interrupt:
            return IPC29bits.CNAIP;
            break;

        case portb_input_change_interrupt:
            return IPC29bits.CNBIP;
            break;

        case portc_input_change_interrupt:
            return IPC30bits.CNCIP;
            break;

        case portd_input_change_interrupt:
            return IPC30bits.CNDIP;
            break;

        case porte_input_change_interrupt:
            return IPC30bits.CNEIP;
            break;

        case portf_input_change_interrupt:
            return IPC30bits.CNFIP;
            break;

        case portg_input_change_interrupt:
            return IPC31bits.CNGIP;
            break;

        case porth_input_change_interrupt:
            return IPC31bits.CNHIP;
            break;

        case portj_input_change_interrupt:
            return IPC31bits.CNJIP;
            break;

        case portk_input_change_interrupt:
            return IPC31bits.CNKIP;
            break;

        case pmp:
            return IPC32bits.PMPIP;
            break;

        case pmp_error:
            return IPC32bits.PMPEIP;
            break;

        case comparator_1_interrupt:
            return IPC32bits.CMP1IP;
            break;

        case comparator_2_interrupt:
            return IPC32bits.CMP2IP;
            break;

        case usb_general_event:
            return IPC33bits.USBIP;
            break;

        case usb_dma_event:
            return IPC33bits.USBDMAIP;
            break;

        case dma_channel_0:
            return IPC33bits.DMA0IP;
            break;

        case dma_channel_1:
            return IPC33bits.DMA1IP;
            break;

        case dma_channel_2:
            return IPC34bits.DMA2IP;
            break;

        case dma_channel_3:
            return IPC34bits.DMA3IP;
            break;

        case dma_channel_4:
            return IPC34bits.DMA4IP;
            break;

        case dma_channel_5:
            return IPC34bits.DMA5IP;
            break;

        case dma_channel_6:
            return IPC35bits.DMA6IP;
            break;

        case dma_channel_7:
            return IPC35bits.DMA7IP;
            break;

        case spi2_fault:
            return IPC35bits.SPI2EIP;
            break;

        case spi2_receive_done:
            return IPC35bits.SPI2RXIP;
            break;

        case spi2_transfer_done:
            return IPC36bits.SPI2TXIP;
            break;

        case uart2_fault:
            return IPC36bits.U2EIP;
            break;

        case uart2_receive_done:
            return IPC36bits.U2RXIP;
            break;

        case uart2_transfer_done:
            return IPC36bits.U2TXIP;
            break;

        case i2c2_bus_collision_event:
            return IPC37bits.I2C2BIP;
            break;

        case i2c2_client_event:
            return IPC37bits.I2C2SIP;
            break;

        case i2c2_host_event:
            return IPC37bits.I2C2MIP;
            break;

        case control_area_network_1:
            return IPC37bits.CAN1IP;
            break;

        case control_area_network_2:
            return IPC38bits.CAN2IP;
            break;

        case ethernet_interrupt:
            return IPC38bits.ETHIP;
            break;

        case spi3_fault:
            return IPC38bits.SPI3EIP;
            break;

        case spi3_receive_done:
            return IPC38bits.SPI3RXIP;
            break;

        case spi3_transfer_done:
            return IPC39bits.SPI3TXIP;
            break;

        case uart3_fault:
            return IPC39bits.U3EIP;
            break;

        case uart3_receive_done:
            return IPC39bits.U3RXIP;
            break;

        case uart3_transfer_done:
            return IPC39bits.U3TXIP;
            break;

        case i2c3_bus_collision_event:
            return IPC40bits.I2C3BIP;
            break;

        case i2c3_client_event:
            return IPC40bits.I2C3SIP;
            break;

        case i2c3_host_event:
            return IPC40bits.I2C3MIP;
            break;

        case spi4_fault:
            return IPC40bits.SPI4EIP;
            break;

        case spi4_receive_done:
            return IPC41bits.SPI4RXIP;
            break;

        case spi4_transfer_done:
            return IPC41bits.SPI4TXIP;
            break;

        case real_time_clock:
            return IPC41bits.RTCCIP;
            break;

        case flash_control_event:
            return IPC41bits.FCEIP;
            break;

        case prefetch_module_sec_event:
            return IPC42bits.PREIP;
            break;

        case sqi1_event:
            return IPC42bits.SQI1IP;
            break;

        case uart4_fault:
            return IPC42bits.U4EIP;
            break;

        case uart4_receive_done:
            return IPC42bits.U4RXIP;
            break;

        case uart4_transfer_done:
            return IPC43bits.U4TXIP;
            break;

        case i2c4_bus_collision_event:
            return IPC43bits.I2C4BIP;
            break;

        case i2c4_client_event:
            return IPC43bits.I2C4SIP;
            break;

        case i2c4_host_event:
            return IPC43bits.I2C4MIP;
            break;

        case spi5_fault:
            return IPC44bits.SPI5EIP;
            break;

        case spi5_receive_done:
            return IPC44bits.SPI5RXIP;
            break;

        case spi5_transfer_done:
            return IPC44bits.SPI5TXIP;
            break;

        case uart5_fault:
            return IPC44bits.U5EIP;
            break;

        case uart5_receive_done:
            return IPC45bits.U5RXIP;
            break;

        case uart5_transfer_done:
            return IPC45bits.U5TXIP;
            break;

        case i2c5_bus_collision_event:
            return IPC45bits.I2C5BIP;
            break;

        case i2c5_client_event:
            return IPC45bits.I2C5SIP;
            break;

        case i2c5_host_event:
            return IPC46bits.I2C5MIP;
            break;

        case spi6_fault:
            return IPC46bits.SPI6EIP;
            break;

        case spi6_receive_done:
            return IPC46bits.SPI6RXIP;
            break;

        case spi6_transfer_done:
            return IPC46bits.SPI6TXIP;
            break;

        case uart6_fault:
            return IPC47bits.U6EIP;
            break;

        case uart6_receive_done:
            return IPC47bits.U6RXIP;
            break;

        case uart6_transfer_done:
            return IPC47bits.U6TXIP;
            break;

        // IRQ 191 has no named IPC47 field on this device, read it directly
        case sdhc_interrupt:
            return (IPC47 >> 26) & 0x7;
            break;

        case glcd_interrupt:
            return IPC48bits.GLCDIP;
            break;

        case gpu_interrupt:
            return IPC48bits.GPUIP;
            break;

        case ctmu_interrupt:
            return IPC48bits.CTMUIP;
            break;

        case adc_end_of_scan:
            return IPC49bits.ADCEOSIP;
            break;

        case adc_analog_circuit_ready:
            return IPC49bits.ADCARDYIP;
            break;

        case adc_update_ready:
            return IPC49bits.ADCURDYIP;
            break;

        case adc0_early_interrupt:
            return IPC49bits.ADC0EIP;
            break;

        case adc1_early_interrupt:
            return IPC50bits.ADC1EIP;
            break;

        case adc2_early_interrupt:
            return IPC50bits.ADC2EIP;
            break;

        case adc3_early_interrupt:
            return IPC50bits.ADC3EIP;
            break;

        case adc4_early_interrupt:
            return IPC50bits.ADC4EIP;
            break;

        case adc_group_early_interrupt_request:
            return IPC51bits.ADCGRPEIP;
            break;

        case adc7_early_interrupt:
            return IPC51bits.ADC7EIP;
            break;

        case adc0_warm_interrupt:
            return IPC51bits.ADC0WIP;
            break;

        case adc1_warm_interrupt:
            return IPC52bits.ADC1WIP;
            break;

        case adc2_warm_interrupt:
            return IPC52bits.ADC2WIP;
            break;

        case adc3_warm_interrupt:
            return IPC52bits.ADC3WIP;
            break;

        case adc4_warm_interrupt:
            return IPC52bits.ADC4WIP;
            break;

        case adc7_warm_interrupt:
            return IPC53bits.ADC7WIP;
            break;

        case mpll_fault_interrupt:
            return IPC53bits.MPLLFLTIP;
            break;
            
        default:
            return 0;
            break;
            
    }

    
}

// This function returns the given interrupt subpriority
uint8_t getInterruptSubriority(interrupt_source_t input_interrupt) {
 
    // Decide which interrupt control bits to manipulate based on which interrupt
    // is having its priority set
    switch (input_interrupt) {

        case core_timer_interrupt:
            return IPC0bits.CTIS;
            break;

        case core_software_interrupt_0:
            return IPC0bits.CS0IS;
            break;

        case core_software_interrupt_1:
            return IPC0bits.CS1IS;
            break;

        case external_interrupt_0:
            return IPC0bits.INT0IS;
            break;

        case timer1:
            return IPC1bits.T1IS;
            break;

        case input_capture_1_error:
            return IPC1bits.IC1EIS;
            break;

        case input_capture_1:
            return IPC1bits.IC1IS;
            break;

        case output_compare_1:
            return IPC1bits.OC1IS;
            break;

        case external_interrupt_1:
            return IPC2bits.INT1IS;
            break;

        case timer2:
            return IPC2bits.T2IS;
            break;

        case input_capture_2_error:
            return IPC2bits.IC2EIS;
            break;

        case input_capture_2:
            return IPC2bits.IC2IS;
            break;

        case output_compare_2:
            return IPC3bits.OC2IS;
            break;

        case external_interrupt_2:
            return IPC3bits.INT2IS;
            break;

        case timer3:
            return IPC3bits.T3IS;
            break;

        case input_capture_3_error:
            return IPC3bits.IC3EIS;
            break;

        case input_capture_3:
            return IPC4bits.IC3IS;
            break;

        case output_compare_3:
            return IPC4bits.OC3IS;
            break;

        case external_interrupt_3:
            return IPC4bits.INT3IS;
            break;

        case timer4:
            return IPC4bits.T4IS;
            break;

        case input_capture_4_error:
            return IPC5bits.IC4EIS;
            break;

        case input_capture_4:
            return IPC5bits.IC4IS;
            break;

        case output_compare_4:
            return IPC5bits.OC4IS;
            break;

        case external_interrupt_4:
            return IPC5bits.INT4IS;
            break;

        case timer5:
            return IPC6bits.T5IS;
            break;

        case input_capture_5_error:
            return IPC6bits.IC5EIS;
            break;

        case input_capture_5:
            return IPC6bits.IC5IS;
            break;

        // IRQ 27 has no named IPC6 field on this device, read it directly
        case output_compare_5:
            return (IPC6 >> 24) & 0x3;
            break;

        case timer6:
            return IPC7bits.T6IS;
            break;

        case input_capture_6_error:
            return IPC7bits.IC6EIS;
            break;

        case input_capture_6:
            return IPC7bits.IC6IS;
            break;

        case output_compare_6:
            return IPC7bits.OC6IS;
            break;

        case timer7:
            return IPC8bits.T7IS;
            break;

        case input_capture_7_error:
            return IPC8bits.IC7EIS;
            break;

        case input_capture_7:
            return IPC8bits.IC7IS;
            break;

        case output_compare_7:
            return IPC8bits.OC7IS;
            break;

        case timer8:
            return IPC9bits.T8IS;
            break;

        case input_capture_8_error:
            return IPC9bits.IC8EIS;
            break;

        case input_capture_8:
            return IPC9bits.IC8IS;
            break;

        case output_compare_8:
            return IPC9bits.OC8IS;
            break;

        case timer9:
            return IPC10bits.T9IS;
            break;

        case input_capture_9_error:
            return IPC10bits.IC9EIS;
            break;

        case input_capture_9:
            return IPC10bits.IC9IS;
            break;

        case output_compare_9:
            return IPC10bits.OC9IS;
            break;

        case adc_global_interrupt:
            return IPC11bits.ADCIS;
            break;

        case adc_fifo_interrupt:
            return IPC11bits.ADCFIFOIS;
            break;

        case adc_digital_comparator_1:
            return IPC11bits.ADCDC1IS;
            break;

        case adc_digital_comparator_2:
            return IPC11bits.ADCDC2IS;
            break;

        case adc_digital_comparator_3:
            return IPC12bits.ADCDC3IS;
            break;

        case adc_digital_comparator_4:
            return IPC12bits.ADCDC4IS;
            break;

        case adc_digital_comparator_5:
            return IPC12bits.ADCDC5IS;
            break;

        case adc_digital_comparator_6:
            return IPC12bits.ADCDC6IS;
            break;

        case adc_digital_filter_1:
            return IPC13bits.ADCDF1IS;
            break;

        case adc_digital_filter_2:
            return IPC13bits.ADCDF2IS;
            break;

        case adc_digital_filter_3:
            return IPC13bits.ADCDF3IS;
            break;

        case adc_digital_filter_4:
            return IPC13bits.ADCDF4IS;
            break;

        case adc_digital_filter_5:
            return IPC14bits.ADCDF5IS;
            break;

        case adc_digital_filter_6:
            return IPC14bits.ADCDF6IS;
            break;

        case adc_fault:
            return IPC14bits.ADCFLTIS;
            break;

        case adc_data_0:
            return IPC14bits.ADCD0IS;
            break;

        case adc_data_1:
            return IPC15bits.ADCD1IS;
            break;

        case adc_data_2:
            return IPC15bits.ADCD2IS;
            break;

        case adc_data_3:
            return IPC15bits.ADCD3IS;
            break;

        case adc_data_4:
            return IPC15bits.ADCD4IS;
            break;

        case adc_data_5:
            return IPC16bits.ADCD5IS;
            break;

        case adc_data_6:
            return IPC16bits.ADCD6IS;
            break;

        case adc_data_7:
            return IPC16bits.ADCD7IS;
            break;

        case adc_data_8:
            return IPC16bits.ADCD8IS;
            break;

        case adc_data_9:
            return IPC17bits.ADCD9IS;
            break;

        case adc_data_10:
            return IPC17bits.ADCD10IS;
            break;

        case adc_data_11:
            return IPC17bits.ADCD11IS;
            break;

        case adc_data_12:
            return IPC17bits.ADCD12IS;
            break;

        case adc_data_13:
            return IPC18bits.ADCD13IS;
            break;

        case adc_data_14:
            return IPC18bits.ADCD14IS;
            break;

        case adc_data_15:
            return IPC18bits.ADCD15IS;
            break;

        case adc_data_16:
            return IPC18bits.ADCD16IS;
            break;

        case adc_data_17:
            return IPC19bits.ADCD17IS;
            break;

        case adc_data_18:
            return IPC19bits.ADCD18IS;
            break;

        case adc_data_19:
            return IPC19bits.ADCD19IS;
            break;

        case adc_data_20:
            return IPC19bits.ADCD20IS;
            break;

        case adc_data_21:
            return IPC20bits.ADCD21IS;
            break;

        case adc_data_22:
            return IPC20bits.ADCD22IS;
            break;

        case adc_data_23:
            return IPC20bits.ADCD23IS;
            break;

        case adc_data_24:
            return IPC20bits.ADCD24IS;
            break;

        case adc_data_25:
            return IPC21bits.ADCD25IS;
            break;

        case adc_data_26:
            return IPC21bits.ADCD26IS;
            break;

        case adc_data_27:
            return IPC21bits.ADCD27IS;
            break;

        case adc_data_28:
            return IPC21bits.ADCD28IS;
            break;

        case adc_data_29:
            return IPC22bits.ADCD29IS;
            break;

        case adc_data_30:
            return IPC22bits.ADCD30IS;
            break;

        case adc_data_31:
            return IPC22bits.ADCD31IS;
            break;

        case adc_data_32:
            return IPC22bits.ADCD32IS;
            break;

        case adc_data_33:
            return IPC23bits.ADCD33IS;
            break;

        case adc_data_34:
            return IPC23bits.ADCD34IS;
            break;

        case adc_data_35:
            return IPC23bits.ADCD35IS;
            break;

        case adc_data_36:
            return IPC23bits.ADCD36IS;
            break;

        case adc_data_37:
            return IPC24bits.ADCD37IS;
            break;

        case adc_data_38:
            return IPC24bits.ADCD38IS;
            break;

        case adc_data_39:
            return IPC24bits.ADCD39IS;
            break;

        case adc_data_40:
            return IPC24bits.ADCD40IS;
            break;

        case adc_data_41:
            return IPC25bits.ADCD41IS;
            break;

        case adc_data_42:
            return IPC25bits.ADCD42IS;
            break;

        case adc_data_43:
            return IPC25bits.ADCD43IS;
            break;

        case usb_suspend_resume_event:
            return IPC25bits.USBSRIS;
            break;

        case core_performance_counter_interrupt:
            return IPC26bits.CPCIS;
            break;

        case core_fast_debug_channel_interrupt:
            return IPC26bits.CFDCIS;
            break;

        case system_bus_protection_violation:
            return IPC26bits.SBIS;
            break;

        case spi1_fault:
            return IPC27bits.SPI1EIS;
            break;

        case spi1_receive_done:
            return IPC27bits.SPI1RXIS;
            break;

        case spi1_transfer_done:
            return IPC27bits.SPI1TXIS;
            break;

        case uart1_fault:
            return IPC28bits.U1EIS;
            break;

        case uart1_receive_done:
            return IPC28bits.U1RXIS;
            break;

        case uart1_transfer_done:
            return IPC28bits.U1TXIS;
            break;

        case i2c1_bus_collision_event:
            return IPC28bits.I2C1BIS;
            break;

        case i2c1_client_event:
            return IPC29bits.I2C1SIS;
            break;

        case i2c1_host_event:
            return IPC29bits.I2C1MIS;
            break;

        case porta_input_change_interrupt:
            return IPC29bits.CNAIS;
            break;

        case portb_input_change_interrupt:
            return IPC29bits.CNBIS;
            break;

        case portc_input_change_interrupt:
            return IPC30bits.CNCIS;
            break;

        case portd_input_change_interrupt:
            return IPC30bits.CNDIS;
            break;

        case porte_input_change_interrupt:
            return IPC30bits.CNEIS;
            break;

        case portf_input_change_interrupt:
            return IPC30bits.CNFIS;
            break;

        case portg_input_change_interrupt:
            return IPC31bits.CNGIS;
            break;

        case porth_input_change_interrupt:
            return IPC31bits.CNHIS;
            break;

        case portj_input_change_interrupt:
            return IPC31bits.CNJIS;
            break;

        case portk_input_change_interrupt:
            return IPC31bits.CNKIS;
            break;

        case pmp:
            return IPC32bits.PMPIS;
            break;

        case pmp_error:
            return IPC32bits.PMPEIS;
            break;

        case comparator_1_interrupt:
            return IPC32bits.CMP1IS;
            break;

        case comparator_2_interrupt:
            return IPC32bits.CMP2IS;
            break;

        case usb_general_event:
            return IPC33bits.USBIS;
            break;

        case usb_dma_event:
            return IPC33bits.USBDMAIS;
            break;

        case dma_channel_0:
            return IPC33bits.DMA0IS;
            break;

        case dma_channel_1:
            return IPC33bits.DMA1IS;
            break;

        case dma_channel_2:
            return IPC34bits.DMA2IS;
            break;

        case dma_channel_3:
            return IPC34bits.DMA3IS;
            break;

        case dma_channel_4:
            return IPC34bits.DMA4IS;
            break;

        case dma_channel_5:
            return IPC34bits.DMA5IS;
            break;

        case dma_channel_6:
            return IPC35bits.DMA6IS;
            break;

        case dma_channel_7:
            return IPC35bits.DMA7IS;
            break;

        case spi2_fault:
            return IPC35bits.SPI2EIS;
            break;

        case spi2_receive_done:
            return IPC35bits.SPI2RXIS;
            break;

        case spi2_transfer_done:
            return IPC36bits.SPI2TXIS;
            break;

        case uart2_fault:
            return IPC36bits.U2EIS;
            break;

        case uart2_receive_done:
            return IPC36bits.U2RXIS;
            break;

        case uart2_transfer_done:
            return IPC36bits.U2TXIS;
            break;

        case i2c2_bus_collision_event:
            return IPC37bits.I2C2BIS;
            break;

        case i2c2_client_event:
            return IPC37bits.I2C2SIS;
            break;

        case i2c2_host_event:
            return IPC37bits.I2C2MIS;
            break;

        case control_area_network_1:
            return IPC37bits.CAN1IS;
            break;

        case control_area_network_2:
            return IPC38bits.CAN2IS;
            break;

        case ethernet_interrupt:
            return IPC38bits.ETHIS;
            break;

        case spi3_fault:
            return IPC38bits.SPI3EIS;
            break;

        case spi3_receive_done:
            return IPC38bits.SPI3RXIS;
            break;

        case spi3_transfer_done:
            return IPC39bits.SPI3TXIS;
            break;

        case uart3_fault:
            return IPC39bits.U3EIS;
            break;

        case uart3_receive_done:
            return IPC39bits.U3RXIS;
            break;

        case uart3_transfer_done:
            return IPC39bits.U3TXIS;
            break;

        case i2c3_bus_collision_event:
            return IPC40bits.I2C3BIS;
            break;

        case i2c3_client_event:
            return IPC40bits.I2C3SIS;
            break;

        case i2c3_host_event:
            return IPC40bits.I2C3MIS;
            break;

        case spi4_fault:
            return IPC40bits.SPI4EIS;
            break;

        case spi4_receive_done:
            return IPC41bits.SPI4RXIS;
            break;

        case spi4_transfer_done:
            return IPC41bits.SPI4TXIS;
            break;

        case real_time_clock:
            return IPC41bits.RTCCIS;
            break;

        case flash_control_event:
            return IPC41bits.FCEIS;
            break;

        case prefetch_module_sec_event:
            return IPC42bits.PREIS;
            break;

        case sqi1_event:
            return IPC42bits.SQI1IS;
            break;

        case uart4_fault:
            return IPC42bits.U4EIS;
            break;

        case uart4_receive_done:
            return IPC42bits.U4RXIS;
            break;

        case uart4_transfer_done:
            return IPC43bits.U4TXIS;
            break;

        case i2c4_bus_collision_event:
            return IPC43bits.I2C4BIS;
            break;

        case i2c4_client_event:
            return IPC43bits.I2C4SIS;
            break;

        case i2c4_host_event:
            return IPC43bits.I2C4MIS;
            break;

        case spi5_fault:
            return IPC44bits.SPI5EIS;
            break;

        case spi5_receive_done:
            return IPC44bits.SPI5RXIS;
            break;

        case spi5_transfer_done:
            return IPC44bits.SPI5TXIS;
            break;

        case uart5_fault:
            return IPC44bits.U5EIS;
            break;

        case uart5_receive_done:
            return IPC45bits.U5RXIS;
            break;

        case uart5_transfer_done:
            return IPC45bits.U5TXIS;
            break;

        case i2c5_bus_collision_event:
            return IPC45bits.I2C5BIS;
            break;

        case i2c5_client_event:
            return IPC45bits.I2C5SIS;
            break;

        case i2c5_host_event:
            return IPC46bits.I2C5MIS;
            break;

        case spi6_fault:
            return IPC46bits.SPI6EIS;
            break;

        case spi6_receive_done:
            return IPC46bits.SPI6RXIS;
            break;

        case spi6_transfer_done:
            return IPC46bits.SPI6TXIS;
            break;

        case uart6_fault:
            return IPC47bits.U6EIS;
            break;

        case uart6_receive_done:
            return IPC47bits.U6RXIS;
            break;

        case uart6_transfer_done:
            return IPC47bits.U6TXIS;
            break;

        // IRQ 191 has no named IPC47 field on this device, read it directly
        case sdhc_interrupt:
            return (IPC47 >> 24) & 0x3;
            break;

        case glcd_interrupt:
            return IPC48bits.GLCDIS;
            break;

        case gpu_interrupt:
            return IPC48bits.GPUIS;
            break;

        case ctmu_interrupt:
            return IPC48bits.CTMUIS;
            break;

        case adc_end_of_scan:
            return IPC49bits.ADCEOSIS;
            break;

        case adc_analog_circuit_ready:
            return IPC49bits.ADCARDYIS;
            break;

        case adc_update_ready:
            return IPC49bits.ADCURDYIS;
            break;

        case adc0_early_interrupt:
            return IPC49bits.ADC0EIS;
            break;

        case adc1_early_interrupt:
            return IPC50bits.ADC1EIS;
            break;

        case adc2_early_interrupt:
            return IPC50bits.ADC2EIS;
            break;

        case adc3_early_interrupt:
            return IPC50bits.ADC3EIS;
            break;

        case adc4_early_interrupt:
            return IPC50bits.ADC4EIS;
            break;

        case adc_group_early_interrupt_request:
            return IPC51bits.ADCGRPEIS;
            break;

        case adc7_early_interrupt:
            return IPC51bits.ADC7EIS;
            break;

        case adc0_warm_interrupt:
            return IPC51bits.ADC0WIS;
            break;

        case adc1_warm_interrupt:
            return IPC52bits.ADC1WIS;
            break;

        case adc2_warm_interrupt:
            return IPC52bits.ADC2WIS;
            break;

        case adc3_warm_interrupt:
            return IPC52bits.ADC3WIS;
            break;

        case adc4_warm_interrupt:
            return IPC52bits.ADC4WIS;
            break;

        case adc7_warm_interrupt:
            return IPC53bits.ADC7WIS;
            break;

        case mpll_fault_interrupt:
            return IPC53bits.MPLLFLTIS;
            break;
            
        default:
            return 0;
            break;
            
    }
    
}


// This function enables a particular interrupt
// Returns 0 if no errors
// Returns 1 if errors
void enableInterrupt(interrupt_source_t input_interrupt) {
 
    setInterruptEnable(input_interrupt, 1);
    
}

// This function disables selected interrupt
void disableInterrupt(interrupt_source_t input_interrupt) {
 
    setInterruptEnable(input_interrupt, 0);
    
}

// This function clears selected interrupt flag
void clearInterruptFlag(interrupt_source_t input_interrupt) {
 
    setInterruptFlag(input_interrupt, 0);
    
}

// This function returns a string of the given interrupt name
char * getInterruptNameStringPadded(interrupt_source_t input_interrupt) {
    
    char *interrupt_descriptor_array[] = {
        
        "core_timer_interrupt               ",
        "core_software_interrupt_0          ",
        "core_software_interrupt_1          ",
        "external_interrupt_0               ",
        "timer1                             ",
        "input_capture_1_error              ",
        "input_capture_1                    ",
        "output_compare_1                   ",
        "external_interrupt_1               ",
        "timer2                             ",
        "input_capture_2_error              ",
        "input_capture_2                    ",
        "output_compare_2                   ",
        "external_interrupt_2               ",
        "timer3                             ",
        "input_capture_3_error              ",
        "input_capture_3                    ",
        "output_compare_3                   ",
        "external_interrupt_3               ",
        "timer4                             ",
        "input_capture_4_error              ",
        "input_capture_4                    ",
        "output_compare_4                   ",
        "external_interrupt_4               ",
        "timer5                             ",
        "input_capture_5_error              ",
        "input_capture_5                    ",
        "output_compare_5                   ",
        "timer6                             ",
        "input_capture_6_error              ",
        "input_capture_6                    ",
        "output_compare_6                   ",
        "timer7                             ",
        "input_capture_7_error              ",
        "input_capture_7                    ",
        "output_compare_7                   ",
        "timer8                             ",
        "input_capture_8_error              ",
        "input_capture_8                    ",
        "output_compare_8                   ",
        "timer9                             ",
        "input_capture_9_error              ",
        "input_capture_9                    ",
        "output_compare_9                   ",
        "adc_global_interrupt               ",
        "adc_fifo_interrupt                 ",
        "adc_digital_comparator_1           ",
        "adc_digital_comparator_2           ",
        "adc_digital_comparator_3           ",
        "adc_digital_comparator_4           ",
        "adc_digital_comparator_5           ",
        "adc_digital_comparator_6           ",
        "adc_digital_filter_1               ",
        "adc_digital_filter_2               ",
        "adc_digital_filter_3               ",
        "adc_digital_filter_4               ",
        "adc_digital_filter_5               ",
        "adc_digital_filter_6               ",
        "adc_fault                          ",
        "adc_data_0                         ",
        "adc_data_1                         ",
        "adc_data_2                         ",
        "adc_data_3                         ",
        "adc_data_4                         ",
        "adc_data_5                         ",
        "adc_data_6                         ",
        "adc_data_7                         ",
        "adc_data_8                         ",
        "adc_data_9                         ",
        "adc_data_10                        ",
        "adc_data_11                        ",
        "adc_data_12                        ",
        "adc_data_13                        ",
        "adc_data_14                        ",
        "adc_data_15                        ",
        "adc_data_16                        ",
        "adc_data_17                        ",
        "adc_data_18                        ",
        "adc_data_19                        ",
        "adc_data_20                        ",
        "adc_data_21                        ",
        "adc_data_22                        ",
        "adc_data_23                        ",
        "adc_data_24                        ",
        "adc_data_25                        ",
        "adc_data_26                        ",
        "adc_data_27                        ",
        "adc_data_28                        ",
        "adc_data_29                        ",
        "adc_data_30                        ",
        "adc_data_31                        ",
        "adc_data_32                        ",
        "adc_data_33                        ",
        "adc_data_34                        ",
        "adc_data_35                        ",
        "adc_data_36                        ",
        "adc_data_37                        ",
        "adc_data_38                        ",
        "adc_data_39                        ",
        "adc_data_40                        ",
        "adc_data_41                        ",
        "adc_data_42                        ",
        "adc_data_43                        ",
        "usb_suspend_resume_event           ",
        "core_performance_counter_interrupt ",
        "core_fast_debug_channel_interrupt  ",
        "system_bus_protection_violation    ",
        "crypto_engine_event                ",
        "reserved                           ",
        "spi1_fault                         ",
        "spi1_receive_done                  ",
        "spi1_transfer_done                 ",
        "uart1_fault                        ",
        "uart1_receive_done                 ",
        "uart1_transfer_done                ",
        "i2c1_bus_collision_event           ",
        "i2c1_client_event                  ",
        "i2c1_host_event                    ",
        "porta_input_change_interrupt       ",
        "portb_input_change_interrupt       ",
        "portc_input_change_interrupt       ",
        "portd_input_change_interrupt       ",
        "porte_input_change_interrupt       ",
        "portf_input_change_interrupt       ",
        "portg_input_change_interrupt       ",
        "porth_input_change_interrupt       ",
        "portj_input_change_interrupt       ",
        "portk_input_change_interrupt       ",
        "pmp                                ",
        "pmp_error                          ",
        "comparator_1_interrupt             ",
        "comparator_2_interrupt             ",
        "usb_general_event                  ",
        "usb_dma_event                      ",
        "dma_channel_0                      ",
        "dma_channel_1                      ",
        "dma_channel_2                      ",
        "dma_channel_3                      ",
        "dma_channel_4                      ",
        "dma_channel_5                      ",
        "dma_channel_6                      ",
        "dma_channel_7                      ",
        "spi2_fault                         ",
        "spi2_receive_done                  ",
        "spi2_transfer_done                 ",
        "uart2_fault                        ",
        "uart2_receive_done                 ",
        "uart2_transfer_done                ",
        "i2c2_bus_collision_event           ",
        "i2c2_client_event                  ",
        "i2c2_host_event                    ",
        "control_area_network_1             ",
        "control_area_network_2             ",
        "ethernet_interrupt                 ",
        "spi3_fault                         ",
        "spi3_receive_done                  ",
        "spi3_transfer_done                 ",
        "uart3_fault                        ",
        "uart3_receive_done                 ",
        "uart3_transfer_done                ",
        "i2c3_bus_collision_event           ",
        "i2c3_client_event                  ",
        "i2c3_host_event                    ",
        "spi4_fault                         ",
        "spi4_receive_done                  ",
        "spi4_transfer_done                 ",
        "real_time_clock                    ",
        "flash_control_event                ",
        "prefetch_module_sec_event          ",
        "sqi1_event                         ",
        "uart4_fault                        ",
        "uart4_receive_done                 ",
        "uart4_transfer_done                ",
        "i2c4_bus_collision_event           ",
        "i2c4_client_event                  ",
        "i2c4_host_event                    ",
        "spi5_fault                         ",
        "spi5_receive_done                  ",
        "spi5_transfer_done                 ",
        "uart5_fault                        ",
        "uart5_receive_done                 ",
        "uart5_transfer_done                ",
        "i2c5_bus_collision_event           ",
        "i2c5_client_event                  ",
        "i2c5_host_event                    ",
        "spi6_fault                         ",
        "spi6_receive_done                  ",
        "spi6_transfer_done                 ",
        "uart6_fault                        ",
        "uart6_receive_done                 ",
        "uart6_transfer_done                ",
        "sdhc_interrupt                     ",
        "glcd_interrupt                     ",
        "gpu_interrupt                      ",
        "reserved                           ",
        "ctmu_interrupt                     ",
        "adc_end_of_scan                    ",
        "adc_analog_circuit_ready           ",
        "adc_update_ready                   ",
        "adc0_early_interrupt               ",
        "adc1_early_interrupt               ",
        "adc2_early_interrupt               ",
        "adc3_early_interrupt               ",
        "adc4_early_interrupt               ",
        "reserved                           ",
        "adc_group_early_interrupt_request  ",
        "adc7_early_interrupt               ",
        "adc0_warm_interrupt                ",
        "adc1_warm_interrupt                ",
        "adc2_warm_interrupt                ",
        "adc3_warm_interrupt                ",
        "adc4_warm_interrupt                ",
        "reserved                           ",
        "reserved                           ",
        "adc7_warm_interrupt                ",
        "mpll_fault_interrupt               "

    };
    
    return interrupt_descriptor_array[input_interrupt];
    
}


// This function prints information on all interrupt settings
void printInterruptStatus(void) {
    
    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Interrupt Controller Status:\n\r");

    terminalTextAttributesReset();
    
    if (getGlobalInterruptsState()) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Global Interrupt Enable: %s\n\r", getGlobalInterruptsState() ? "T" : "F");
    
    // Print interrupt vector mode
    if (INTCONbits.MVEC) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Interrupt Vector Mode: %s\n\r", INTCONbits.MVEC ? "Multi-Vector" : "Single-Vector");

    // Print interrupt priority shadow register settings
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Interrupt Priority Shadow Register Setting: 0x%08X\n\r", PRISS);
    
    // Print latest serviced interrupt priority
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Latest interrupt priority serviced: 0x%08X\n\r", INTSTATbits.SRIPL);
    
    // Print latest serviced interrupt
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Latest interrupt serviced: 0x%08X\n\r", INTSTATbits.SIRQ);
    
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("\n\rInterrupts in list are marked green if they are enabled or have IPL > 0\n\r");
    
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\n\rInterrupt sources:\n\r");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("###  Name                     EN?  IPL ISL IRQ?\n\r");
    
    terminalTextAttributesReset();

    // Loop through all possible interrupts
    uint8_t i;
    for (i = 0; i <= 215; i++) {
     
        if (i % 2 == 0) {
         
            
            if (getInterruptEnable(i) || getInterruptPriority(i) > 0) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            
        }
        
        else {
         
            if (getInterruptEnable(i) || getInterruptPriority(i) > 0) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
            else terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
            
        }
        
        printf("%03d  %s %c    %d   %d    %c\n\r", 
                i,
                getInterruptNameStringPadded(i),
                getInterruptEnable(i) ? 'T' : 'F',
                getInterruptPriority(i),
                getInterruptSubriority(i),
                getInterruptFlag(i) ? 'T' : 'F');

    }

    terminalTextAttributesReset();    
    
}