
#include <xc.h>
#include <stdio.h>

#include "32mz_interrupt_control.h"
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

            break;

        case core_software_interrupt_0:

            break;

        case core_software_interrupt_1:

            break;

        case external_interrupt_0:

            break;

        case timer1:

            break;

        case input_capture_1_error:

            break;

        case input_capture_1:

            break;

        case output_compare_1:

            break;

        case external_interrupt_1:

            break;

        case timer2:

            break;

        case input_capture_2_error:

            break;

        case input_capture_2:

            break;

        case output_compare_2:

            break;

        case external_interrupt_2:

            break;

        case timer3:

            break;

        case input_capture_3_error:

            break;

        case input_capture_3:

            break;

        case output_compare_3:

            break;

        case external_interrupt_3:

            break;

        case timer4:

            break;

        case input_capture_4_error:

            break;

        case input_capture_4:

            break;

        case output_compare_4:

            break;

        case external_interrupt_4:

            break;

        case timer5:

            break;

        case input_capture_5_error:

            break;

        case input_capture_5:

            break;

        case output_compare_5:

            break;

        case timer6:

            break;

        case input_capture_6_error:

            break;

        case input_capture_6:

            break;

        case output_compare_6:

            break;

        case timer7:

            break;

        case input_capture_7_error:

            break;

        case input_capture_7:

            break;

        case output_compare_7:

            break;

        case timer8:

            break;

        case input_capture_8_error:

            break;

        case input_capture_8:

            break;

        case output_compare_8:

            break;

        case timer9:

            break;

        case input_capture_9_error:

            break;

        case input_capture_9:

            break;

        case output_compare_9:

            break;

        case adc_global_interrupt:

            break;

        case adc_fifo_interrupt:

            break;

        case adc_digital_comparator_1:

            break;

        case adc_digital_comparator_2:

            break;

        case adc_digital_comparator_3:

            break;

        case adc_digital_comparator_4:

            break;

        case adc_digital_comparator_5:

            break;

        case adc_digital_comparator_6:

            break;

        case adc_digital_filter_1:

            break;

        case adc_digital_filter_2:

            break;

        case adc_digital_filter_3:

            break;

        case adc_digital_filter_4:

            break;

        case adc_digital_filter_5:

            break;

        case adc_digital_filter_6:

            break;

        case adc_fault:

            break;

        case adc_data_0:

            break;

        case adc_data_1:

            break;

        case adc_data_2:

            break;

        case adc_data_3:

            break;

        case adc_data_4:

            break;

        case adc_data_5:

            break;

        case adc_data_6:

            break;

        case adc_data_7:

            break;

        case adc_data_8:

            break;

        case adc_data_9:

            break;

        case adc_data_10:

            break;

        case adc_data_11:

            break;

        case adc_data_12:

            break;

        case adc_data_13:

            break;

        case adc_data_14:

            break;

        case adc_data_15:

            break;

        case adc_data_16:

            break;

        case adc_data_17:

            break;

        case adc_data_18:

            break;

        case adc_data_19:

            break;

        case adc_data_20:

            break;

        case adc_data_21:

            break;

        case adc_data_22:

            break;

        case adc_data_23:

            break;

        case adc_data_24:

            break;

        case adc_data_25:

            break;

        case adc_data_26:

            break;

        case adc_data_27:

            break;

        case adc_data_28:

            break;

        case adc_data_29:

            break;

        case adc_data_30:

            break;

        case adc_data_31:

            break;

        case adc_data_32:

            break;

        case adc_data_33:

            break;

        case adc_data_34:

            break;

        case adc_data_35:

            break;

        case adc_data_36:

            break;

        case adc_data_37:

            break;

        case adc_data_38:

            break;

        case adc_data_39:

            break;

        case adc_data_40:

            break;

        case adc_data_41:

            break;

        case adc_data_42:

            break;

        case adc_data_43:

            break;

        case usb_suspend_resume_event:

            break;

        case core_performance_counter_interrupt:

            break;

        case core_fast_debug_channel_interrupt:

            break;

        case system_bus_protection_violation:

            break;

        case crypto_engine_event:

            break;

        case spi1_fault:

            break;

        case spi1_receive_done:

            break;

        case spi1_transfer_done:

            break;

        case uart1_fault:

            break;

        case uart1_receive_done:

            break;

        case uart1_transfer_done:

            break;

        case i2c1_bus_collision_event:

            break;

        case i2c1_client_event:

            break;

        case i2c1_host_event:

            break;

        case porta_input_change_interrupt:

            break;

        case portb_input_change_interrupt:

            break;

        case portc_input_change_interrupt:

            break;

        case portd_input_change_interrupt:

            break;

        case porte_input_change_interrupt:

            break;

        case portf_input_change_interrupt:

            break;

        case portg_input_change_interrupt:

            break;

        case porth_input_change_interrupt:

            break;

        case portj_input_change_interrupt:

            break;

        case portk_input_change_interrupt:

            break;

        case pmp:

            break;

        case pmp_error:

            break;

        case comparator_1_interrupt:

            break;

        case comparator_2_interrupt:

            break;

        case usb_general_event:

            break;

        case usb_dma_event:

            break;

        case dma_channel_0:

            break;

        case dma_channel_1:

            break;

        case dma_channel_2:

            break;

        case dma_channel_3:

            break;

        case dma_channel_4:

            break;

        case dma_channel_5:

            break;

        case dma_channel_6:

            break;

        case dma_channel_7:

            break;

        case spi2_fault:

            break;

        case spi2_receive_done:

            break;

        case spi2_transfer_done:

            break;

        case uart2_fault:

            break;

        case uart2_receive_done:

            break;

        case uart2_transfer_done:

            break;

        case i2c2_bus_collision_event:

            break;

        case i2c2_client_event:

            break;

        case i2c2_host_event:

            break;

        case control_area_network_1:

            break;

        case control_area_network_2:

            break;

        case ethernet_interrupt:

            break;

        case spi3_fault:

            break;

        case spi3_receive_done:

            break;

        case spi3_transfer_done:

            break;

        case uart3_fault:

            break;

        case uart3_receive_done:

            break;

        case uart3_transfer_done:

            break;

        case i2c3_bus_collision_event:

            break;

        case i2c3_client_event:

            break;

        case i2c3_host_event:

            break;

        case spi4_fault:

            break;

        case spi4_receive_done:

            break;

        case spi4_transfer_done:

            break;

        case real_time_clock:

            break;

        case flash_control_event:

            break;

        case prefetch_module_sec_event:

            break;

        case sqi1_event:

            break;

        case uart4_fault:

            break;

        case uart4_receive_done:

            break;

        case uart4_transfer_done:

            break;

        case i2c4_bus_collision_event:

            break;

        case i2c4_client_event:

            break;

        case i2c4_host_event:

            break;

        case spi5_fault:

            break;

        case spi5_receive_done:

            break;

        case spi5_transfer_done:

            break;

        case uart5_fault:

            break;

        case uart5_receive_done:

            break;

        case uart5_transfer_done:

            break;

        case i2c5_bus_collision_event:

            break;

        case i2c5_client_event:

            break;

        case i2c5_host_event:

            break;

        case spi6_fault:

            break;

        case spi6_receive_done:

            break;

        case spi6_transfer_done:

            break;

        case uart6_fault:

            break;

        case uart6_receive_done:

            break;

        case uart6_transfer_done:

            break;

        case sdhc_interrupt:

            break;

        case glcd_interrupt:

            break;

        case gpu_interrupt:

            break;

        case ctmu_interrupt:

            break;

        case adc_end_of_scan:

            break;

        case adc_analog_circuit_ready:

            break;

        case adc_update_ready:

            break;

        case adc0_early_interrupt:

            break;

        case adc1_early_interrupt:

            break;

        case adc2_early_interrupt:

            break;

        case adc3_early_interrupt:

            break;

        case adc4_early_interrupt:

            break;

        case adc_group_early_interrupt_request:

            break;

        case adc7_early_interrupt:

            break;

        case adc0_warm_interrupt:

            break;

        case adc1_warm_interrupt:

            break;

        case adc2_warm_interrupt:

            break;

        case adc3_warm_interrupt:

            break;

        case adc4_warm_interrupt:

            break;

        case adc7_warm_interrupt:

            break;

        case mpll_fault_interrupt:

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

            break;

        case core_software_interrupt_0:

            break;

        case core_software_interrupt_1:

            break;

        case external_interrupt_0:

            break;

        case timer1:

            break;

        case input_capture_1_error:

            break;

        case input_capture_1:

            break;

        case output_compare_1:

            break;

        case external_interrupt_1:

            break;

        case timer2:

            break;

        case input_capture_2_error:

            break;

        case input_capture_2:

            break;

        case output_compare_2:

            break;

        case external_interrupt_2:

            break;

        case timer3:

            break;

        case input_capture_3_error:

            break;

        case input_capture_3:

            break;

        case output_compare_3:

            break;

        case external_interrupt_3:

            break;

        case timer4:

            break;

        case input_capture_4_error:

            break;

        case input_capture_4:

            break;

        case output_compare_4:

            break;

        case external_interrupt_4:

            break;

        case timer5:

            break;

        case input_capture_5_error:

            break;

        case input_capture_5:

            break;

        case output_compare_5:

            break;

        case timer6:

            break;

        case input_capture_6_error:

            break;

        case input_capture_6:

            break;

        case output_compare_6:

            break;

        case timer7:

            break;

        case input_capture_7_error:

            break;

        case input_capture_7:

            break;

        case output_compare_7:

            break;

        case timer8:

            break;

        case input_capture_8_error:

            break;

        case input_capture_8:

            break;

        case output_compare_8:

            break;

        case timer9:

            break;

        case input_capture_9_error:

            break;

        case input_capture_9:

            break;

        case output_compare_9:

            break;

        case adc_global_interrupt:

            break;

        case adc_fifo_interrupt:

            break;

        case adc_digital_comparator_1:

            break;

        case adc_digital_comparator_2:

            break;

        case adc_digital_comparator_3:

            break;

        case adc_digital_comparator_4:

            break;

        case adc_digital_comparator_5:

            break;

        case adc_digital_comparator_6:

            break;

        case adc_digital_filter_1:

            break;

        case adc_digital_filter_2:

            break;

        case adc_digital_filter_3:

            break;

        case adc_digital_filter_4:

            break;

        case adc_digital_filter_5:

            break;

        case adc_digital_filter_6:

            break;

        case adc_fault:

            break;

        case adc_data_0:

            break;

        case adc_data_1:

            break;

        case adc_data_2:

            break;

        case adc_data_3:

            break;

        case adc_data_4:

            break;

        case adc_data_5:

            break;

        case adc_data_6:

            break;

        case adc_data_7:

            break;

        case adc_data_8:

            break;

        case adc_data_9:

            break;

        case adc_data_10:

            break;

        case adc_data_11:

            break;

        case adc_data_12:

            break;

        case adc_data_13:

            break;

        case adc_data_14:

            break;

        case adc_data_15:

            break;

        case adc_data_16:

            break;

        case adc_data_17:

            break;

        case adc_data_18:

            break;

        case adc_data_19:

            break;

        case adc_data_20:

            break;

        case adc_data_21:

            break;

        case adc_data_22:

            break;

        case adc_data_23:

            break;

        case adc_data_24:

            break;

        case adc_data_25:

            break;

        case adc_data_26:

            break;

        case adc_data_27:

            break;

        case adc_data_28:

            break;

        case adc_data_29:

            break;

        case adc_data_30:

            break;

        case adc_data_31:

            break;

        case adc_data_32:

            break;

        case adc_data_33:

            break;

        case adc_data_34:

            break;

        case adc_data_35:

            break;

        case adc_data_36:

            break;

        case adc_data_37:

            break;

        case adc_data_38:

            break;

        case adc_data_39:

            break;

        case adc_data_40:

            break;

        case adc_data_41:

            break;

        case adc_data_42:

            break;

        case adc_data_43:

            break;

        case usb_suspend_resume_event:

            break;

        case core_performance_counter_interrupt:

            break;

        case core_fast_debug_channel_interrupt:

            break;

        case system_bus_protection_violation:

            break;

        case crypto_engine_event:

            break;

        case spi1_fault:

            break;

        case spi1_receive_done:

            break;

        case spi1_transfer_done:

            break;

        case uart1_fault:

            break;

        case uart1_receive_done:

            break;

        case uart1_transfer_done:

            break;

        case i2c1_bus_collision_event:

            break;

        case i2c1_client_event:

            break;

        case i2c1_host_event:

            break;

        case porta_input_change_interrupt:

            break;

        case portb_input_change_interrupt:

            break;

        case portc_input_change_interrupt:

            break;

        case portd_input_change_interrupt:

            break;

        case porte_input_change_interrupt:

            break;

        case portf_input_change_interrupt:

            break;

        case portg_input_change_interrupt:

            break;

        case porth_input_change_interrupt:

            break;

        case portj_input_change_interrupt:

            break;

        case portk_input_change_interrupt:

            break;

        case pmp:

            break;

        case pmp_error:

            break;

        case comparator_1_interrupt:

            break;

        case comparator_2_interrupt:

            break;

        case usb_general_event:

            break;

        case usb_dma_event:

            break;

        case dma_channel_0:

            break;

        case dma_channel_1:

            break;

        case dma_channel_2:

            break;

        case dma_channel_3:

            break;

        case dma_channel_4:

            break;

        case dma_channel_5:

            break;

        case dma_channel_6:

            break;

        case dma_channel_7:

            break;

        case spi2_fault:

            break;

        case spi2_receive_done:

            break;

        case spi2_transfer_done:

            break;

        case uart2_fault:

            break;

        case uart2_receive_done:

            break;

        case uart2_transfer_done:

            break;

        case i2c2_bus_collision_event:

            break;

        case i2c2_client_event:

            break;

        case i2c2_host_event:

            break;

        case control_area_network_1:

            break;

        case control_area_network_2:

            break;

        case ethernet_interrupt:

            break;

        case spi3_fault:

            break;

        case spi3_receive_done:

            break;

        case spi3_transfer_done:

            break;

        case uart3_fault:

            break;

        case uart3_receive_done:

            break;

        case uart3_transfer_done:

            break;

        case i2c3_bus_collision_event:

            break;

        case i2c3_client_event:

            break;

        case i2c3_host_event:

            break;

        case spi4_fault:

            break;

        case spi4_receive_done:

            break;

        case spi4_transfer_done:

            break;

        case real_time_clock:

            break;

        case flash_control_event:

            break;

        case prefetch_module_sec_event:

            break;

        case sqi1_event:

            break;

        case uart4_fault:

            break;

        case uart4_receive_done:

            break;

        case uart4_transfer_done:

            break;

        case i2c4_bus_collision_event:

            break;

        case i2c4_client_event:

            break;

        case i2c4_host_event:

            break;

        case spi5_fault:

            break;

        case spi5_receive_done:

            break;

        case spi5_transfer_done:

            break;

        case uart5_fault:

            break;

        case uart5_receive_done:

            break;

        case uart5_transfer_done:

            break;

        case i2c5_bus_collision_event:

            break;

        case i2c5_client_event:

            break;

        case i2c5_host_event:

            break;

        case spi6_fault:

            break;

        case spi6_receive_done:

            break;

        case spi6_transfer_done:

            break;

        case uart6_fault:

            break;

        case uart6_receive_done:

            break;

        case uart6_transfer_done:

            break;

        case sdhc_interrupt:

            break;

        case glcd_interrupt:

            break;

        case gpu_interrupt:

            break;

        case ctmu_interrupt:

            break;

        case adc_end_of_scan:

            break;

        case adc_analog_circuit_ready:

            break;

        case adc_update_ready:

            break;

        case adc0_early_interrupt:

            break;

        case adc1_early_interrupt:

            break;

        case adc2_early_interrupt:

            break;

        case adc3_early_interrupt:

            break;

        case adc4_early_interrupt:

            break;

        case adc_group_early_interrupt_request:

            break;

        case adc7_early_interrupt:

            break;

        case adc0_warm_interrupt:

            break;

        case adc1_warm_interrupt:

            break;

        case adc2_warm_interrupt:

            break;

        case adc3_warm_interrupt:

            break;

        case adc4_warm_interrupt:

            break;

        case adc7_warm_interrupt:

            break;

        case mpll_fault_interrupt:

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

            break;

        case core_software_interrupt_0:

            break;

        case core_software_interrupt_1:

            break;

        case external_interrupt_0:

            break;

        case timer1:

            break;

        case input_capture_1_error:

            break;

        case input_capture_1:

            break;

        case output_compare_1:

            break;

        case external_interrupt_1:

            break;

        case timer2:

            break;

        case input_capture_2_error:

            break;

        case input_capture_2:

            break;

        case output_compare_2:

            break;

        case external_interrupt_2:

            break;

        case timer3:

            break;

        case input_capture_3_error:

            break;

        case input_capture_3:

            break;

        case output_compare_3:

            break;

        case external_interrupt_3:

            break;

        case timer4:

            break;

        case input_capture_4_error:

            break;

        case input_capture_4:

            break;

        case output_compare_4:

            break;

        case external_interrupt_4:

            break;

        case timer5:

            break;

        case input_capture_5_error:

            break;

        case input_capture_5:

            break;

        case output_compare_5:

            break;

        case timer6:

            break;

        case input_capture_6_error:

            break;

        case input_capture_6:

            break;

        case output_compare_6:

            break;

        case timer7:

            break;

        case input_capture_7_error:

            break;

        case input_capture_7:

            break;

        case output_compare_7:

            break;

        case timer8:

            break;

        case input_capture_8_error:

            break;

        case input_capture_8:

            break;

        case output_compare_8:

            break;

        case timer9:

            break;

        case input_capture_9_error:

            break;

        case input_capture_9:

            break;

        case output_compare_9:

            break;

        case adc_global_interrupt:

            break;

        case adc_fifo_interrupt:

            break;

        case adc_digital_comparator_1:

            break;

        case adc_digital_comparator_2:

            break;

        case adc_digital_comparator_3:

            break;

        case adc_digital_comparator_4:

            break;

        case adc_digital_comparator_5:

            break;

        case adc_digital_comparator_6:

            break;

        case adc_digital_filter_1:

            break;

        case adc_digital_filter_2:

            break;

        case adc_digital_filter_3:

            break;

        case adc_digital_filter_4:

            break;

        case adc_digital_filter_5:

            break;

        case adc_digital_filter_6:

            break;

        case adc_fault:

            break;

        case adc_data_0:

            break;

        case adc_data_1:

            break;

        case adc_data_2:

            break;

        case adc_data_3:

            break;

        case adc_data_4:

            break;

        case adc_data_5:

            break;

        case adc_data_6:

            break;

        case adc_data_7:

            break;

        case adc_data_8:

            break;

        case adc_data_9:

            break;

        case adc_data_10:

            break;

        case adc_data_11:

            break;

        case adc_data_12:

            break;

        case adc_data_13:

            break;

        case adc_data_14:

            break;

        case adc_data_15:

            break;

        case adc_data_16:

            break;

        case adc_data_17:

            break;

        case adc_data_18:

            break;

        case adc_data_19:

            break;

        case adc_data_20:

            break;

        case adc_data_21:

            break;

        case adc_data_22:

            break;

        case adc_data_23:

            break;

        case adc_data_24:

            break;

        case adc_data_25:

            break;

        case adc_data_26:

            break;

        case adc_data_27:

            break;

        case adc_data_28:

            break;

        case adc_data_29:

            break;

        case adc_data_30:

            break;

        case adc_data_31:

            break;

        case adc_data_32:

            break;

        case adc_data_33:

            break;

        case adc_data_34:

            break;

        case adc_data_35:

            break;

        case adc_data_36:

            break;

        case adc_data_37:

            break;

        case adc_data_38:

            break;

        case adc_data_39:

            break;

        case adc_data_40:

            break;

        case adc_data_41:

            break;

        case adc_data_42:

            break;

        case adc_data_43:

            break;

        case usb_suspend_resume_event:

            break;

        case core_performance_counter_interrupt:

            break;

        case core_fast_debug_channel_interrupt:

            break;

        case system_bus_protection_violation:

            break;

        case crypto_engine_event:

            break;

        case spi1_fault:

            break;

        case spi1_receive_done:

            break;

        case spi1_transfer_done:

            break;

        case uart1_fault:

            break;

        case uart1_receive_done:

            break;

        case uart1_transfer_done:

            break;

        case i2c1_bus_collision_event:

            break;

        case i2c1_client_event:

            break;

        case i2c1_host_event:

            break;

        case porta_input_change_interrupt:

            break;

        case portb_input_change_interrupt:

            break;

        case portc_input_change_interrupt:

            break;

        case portd_input_change_interrupt:

            break;

        case porte_input_change_interrupt:

            break;

        case portf_input_change_interrupt:

            break;

        case portg_input_change_interrupt:

            break;

        case porth_input_change_interrupt:

            break;

        case portj_input_change_interrupt:

            break;

        case portk_input_change_interrupt:

            break;

        case pmp:

            break;

        case pmp_error:

            break;

        case comparator_1_interrupt:

            break;

        case comparator_2_interrupt:

            break;

        case usb_general_event:

            break;

        case usb_dma_event:

            break;

        case dma_channel_0:

            break;

        case dma_channel_1:

            break;

        case dma_channel_2:

            break;

        case dma_channel_3:

            break;

        case dma_channel_4:

            break;

        case dma_channel_5:

            break;

        case dma_channel_6:

            break;

        case dma_channel_7:

            break;

        case spi2_fault:

            break;

        case spi2_receive_done:

            break;

        case spi2_transfer_done:

            break;

        case uart2_fault:

            break;

        case uart2_receive_done:

            break;

        case uart2_transfer_done:

            break;

        case i2c2_bus_collision_event:

            break;

        case i2c2_client_event:

            break;

        case i2c2_host_event:

            break;

        case control_area_network_1:

            break;

        case control_area_network_2:

            break;

        case ethernet_interrupt:

            break;

        case spi3_fault:

            break;

        case spi3_receive_done:

            break;

        case spi3_transfer_done:

            break;

        case uart3_fault:

            break;

        case uart3_receive_done:

            break;

        case uart3_transfer_done:

            break;

        case i2c3_bus_collision_event:

            break;

        case i2c3_client_event:

            break;

        case i2c3_host_event:

            break;

        case spi4_fault:

            break;

        case spi4_receive_done:

            break;

        case spi4_transfer_done:

            break;

        case real_time_clock:

            break;

        case flash_control_event:

            break;

        case prefetch_module_sec_event:

            break;

        case sqi1_event:

            break;

        case uart4_fault:

            break;

        case uart4_receive_done:

            break;

        case uart4_transfer_done:

            break;

        case i2c4_bus_collision_event:

            break;

        case i2c4_client_event:

            break;

        case i2c4_host_event:

            break;

        case spi5_fault:

            break;

        case spi5_receive_done:

            break;

        case spi5_transfer_done:

            break;

        case uart5_fault:

            break;

        case uart5_receive_done:

            break;

        case uart5_transfer_done:

            break;

        case i2c5_bus_collision_event:

            break;

        case i2c5_client_event:

            break;

        case i2c5_host_event:

            break;

        case spi6_fault:

            break;

        case spi6_receive_done:

            break;

        case spi6_transfer_done:

            break;

        case uart6_fault:

            break;

        case uart6_receive_done:

            break;

        case uart6_transfer_done:

            break;

        case sdhc_interrupt:

            break;

        case glcd_interrupt:

            break;

        case gpu_interrupt:

            break;

        case ctmu_interrupt:

            break;

        case adc_end_of_scan:

            break;

        case adc_analog_circuit_ready:

            break;

        case adc_update_ready:

            break;

        case adc0_early_interrupt:

            break;

        case adc1_early_interrupt:

            break;

        case adc2_early_interrupt:

            break;

        case adc3_early_interrupt:

            break;

        case adc4_early_interrupt:

            break;

        case adc_group_early_interrupt_request:

            break;

        case adc7_early_interrupt:

            break;

        case adc0_warm_interrupt:

            break;

        case adc1_warm_interrupt:

            break;

        case adc2_warm_interrupt:

            break;

        case adc3_warm_interrupt:

            break;

        case adc4_warm_interrupt:

            break;

        case adc7_warm_interrupt:

            break;

        case mpll_fault_interrupt:

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

            break;

        case core_software_interrupt_0:

            break;

        case core_software_interrupt_1:

            break;

        case external_interrupt_0:

            break;

        case timer1:

            break;

        case input_capture_1_error:

            break;

        case input_capture_1:

            break;

        case output_compare_1:

            break;

        case external_interrupt_1:

            break;

        case timer2:

            break;

        case input_capture_2_error:

            break;

        case input_capture_2:

            break;

        case output_compare_2:

            break;

        case external_interrupt_2:

            break;

        case timer3:

            break;

        case input_capture_3_error:

            break;

        case input_capture_3:

            break;

        case output_compare_3:

            break;

        case external_interrupt_3:

            break;

        case timer4:

            break;

        case input_capture_4_error:

            break;

        case input_capture_4:

            break;

        case output_compare_4:

            break;

        case external_interrupt_4:

            break;

        case timer5:

            break;

        case input_capture_5_error:

            break;

        case input_capture_5:

            break;

        case output_compare_5:

            break;

        case timer6:

            break;

        case input_capture_6_error:

            break;

        case input_capture_6:

            break;

        case output_compare_6:

            break;

        case timer7:

            break;

        case input_capture_7_error:

            break;

        case input_capture_7:

            break;

        case output_compare_7:

            break;

        case timer8:

            break;

        case input_capture_8_error:

            break;

        case input_capture_8:

            break;

        case output_compare_8:

            break;

        case timer9:

            break;

        case input_capture_9_error:

            break;

        case input_capture_9:

            break;

        case output_compare_9:

            break;

        case adc_global_interrupt:

            break;

        case adc_fifo_interrupt:

            break;

        case adc_digital_comparator_1:

            break;

        case adc_digital_comparator_2:

            break;

        case adc_digital_comparator_3:

            break;

        case adc_digital_comparator_4:

            break;

        case adc_digital_comparator_5:

            break;

        case adc_digital_comparator_6:

            break;

        case adc_digital_filter_1:

            break;

        case adc_digital_filter_2:

            break;

        case adc_digital_filter_3:

            break;

        case adc_digital_filter_4:

            break;

        case adc_digital_filter_5:

            break;

        case adc_digital_filter_6:

            break;

        case adc_fault:

            break;

        case adc_data_0:

            break;

        case adc_data_1:

            break;

        case adc_data_2:

            break;

        case adc_data_3:

            break;

        case adc_data_4:

            break;

        case adc_data_5:

            break;

        case adc_data_6:

            break;

        case adc_data_7:

            break;

        case adc_data_8:

            break;

        case adc_data_9:

            break;

        case adc_data_10:

            break;

        case adc_data_11:

            break;

        case adc_data_12:

            break;

        case adc_data_13:

            break;

        case adc_data_14:

            break;

        case adc_data_15:

            break;

        case adc_data_16:

            break;

        case adc_data_17:

            break;

        case adc_data_18:

            break;

        case adc_data_19:

            break;

        case adc_data_20:

            break;

        case adc_data_21:

            break;

        case adc_data_22:

            break;

        case adc_data_23:

            break;

        case adc_data_24:

            break;

        case adc_data_25:

            break;

        case adc_data_26:

            break;

        case adc_data_27:

            break;

        case adc_data_28:

            break;

        case adc_data_29:

            break;

        case adc_data_30:

            break;

        case adc_data_31:

            break;

        case adc_data_32:

            break;

        case adc_data_33:

            break;

        case adc_data_34:

            break;

        case adc_data_35:

            break;

        case adc_data_36:

            break;

        case adc_data_37:

            break;

        case adc_data_38:

            break;

        case adc_data_39:

            break;

        case adc_data_40:

            break;

        case adc_data_41:

            break;

        case adc_data_42:

            break;

        case adc_data_43:

            break;

        case usb_suspend_resume_event:

            break;

        case core_performance_counter_interrupt:

            break;

        case core_fast_debug_channel_interrupt:

            break;

        case system_bus_protection_violation:

            break;

        case crypto_engine_event:

            break;

        case spi1_fault:

            break;

        case spi1_receive_done:

            break;

        case spi1_transfer_done:

            break;

        case uart1_fault:

            break;

        case uart1_receive_done:

            break;

        case uart1_transfer_done:

            break;

        case i2c1_bus_collision_event:

            break;

        case i2c1_client_event:

            break;

        case i2c1_host_event:

            break;

        case porta_input_change_interrupt:

            break;

        case portb_input_change_interrupt:

            break;

        case portc_input_change_interrupt:

            break;

        case portd_input_change_interrupt:

            break;

        case porte_input_change_interrupt:

            break;

        case portf_input_change_interrupt:

            break;

        case portg_input_change_interrupt:

            break;

        case porth_input_change_interrupt:

            break;

        case portj_input_change_interrupt:

            break;

        case portk_input_change_interrupt:

            break;

        case pmp:

            break;

        case pmp_error:

            break;

        case comparator_1_interrupt:

            break;

        case comparator_2_interrupt:

            break;

        case usb_general_event:

            break;

        case usb_dma_event:

            break;

        case dma_channel_0:

            break;

        case dma_channel_1:

            break;

        case dma_channel_2:

            break;

        case dma_channel_3:

            break;

        case dma_channel_4:

            break;

        case dma_channel_5:

            break;

        case dma_channel_6:

            break;

        case dma_channel_7:

            break;

        case spi2_fault:

            break;

        case spi2_receive_done:

            break;

        case spi2_transfer_done:

            break;

        case uart2_fault:

            break;

        case uart2_receive_done:

            break;

        case uart2_transfer_done:

            break;

        case i2c2_bus_collision_event:

            break;

        case i2c2_client_event:

            break;

        case i2c2_host_event:

            break;

        case control_area_network_1:

            break;

        case control_area_network_2:

            break;

        case ethernet_interrupt:

            break;

        case spi3_fault:

            break;

        case spi3_receive_done:

            break;

        case spi3_transfer_done:

            break;

        case uart3_fault:

            break;

        case uart3_receive_done:

            break;

        case uart3_transfer_done:

            break;

        case i2c3_bus_collision_event:

            break;

        case i2c3_client_event:

            break;

        case i2c3_host_event:

            break;

        case spi4_fault:

            break;

        case spi4_receive_done:

            break;

        case spi4_transfer_done:

            break;

        case real_time_clock:

            break;

        case flash_control_event:

            break;

        case prefetch_module_sec_event:

            break;

        case sqi1_event:

            break;

        case uart4_fault:

            break;

        case uart4_receive_done:

            break;

        case uart4_transfer_done:

            break;

        case i2c4_bus_collision_event:

            break;

        case i2c4_client_event:

            break;

        case i2c4_host_event:

            break;

        case spi5_fault:

            break;

        case spi5_receive_done:

            break;

        case spi5_transfer_done:

            break;

        case uart5_fault:

            break;

        case uart5_receive_done:

            break;

        case uart5_transfer_done:

            break;

        case i2c5_bus_collision_event:

            break;

        case i2c5_client_event:

            break;

        case i2c5_host_event:

            break;

        case spi6_fault:

            break;

        case spi6_receive_done:

            break;

        case spi6_transfer_done:

            break;

        case uart6_fault:

            break;

        case uart6_receive_done:

            break;

        case uart6_transfer_done:

            break;

        case sdhc_interrupt:

            break;

        case glcd_interrupt:

            break;

        case gpu_interrupt:

            break;

        case ctmu_interrupt:

            break;

        case adc_end_of_scan:

            break;

        case adc_analog_circuit_ready:

            break;

        case adc_update_ready:

            break;

        case adc0_early_interrupt:

            break;

        case adc1_early_interrupt:

            break;

        case adc2_early_interrupt:

            break;

        case adc3_early_interrupt:

            break;

        case adc4_early_interrupt:

            break;

        case adc_group_early_interrupt_request:

            break;

        case adc7_early_interrupt:

            break;

        case adc0_warm_interrupt:

            break;

        case adc1_warm_interrupt:

            break;

        case adc2_warm_interrupt:

            break;

        case adc3_warm_interrupt:

            break;

        case adc4_warm_interrupt:

            break;

        case adc7_warm_interrupt:

            break;

        case mpll_fault_interrupt:

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

            break;

        case core_software_interrupt_0:

            break;

        case core_software_interrupt_1:

            break;

        case external_interrupt_0:

            break;

        case timer1:

            break;

        case input_capture_1_error:

            break;

        case input_capture_1:

            break;

        case output_compare_1:

            break;

        case external_interrupt_1:

            break;

        case timer2:

            break;

        case input_capture_2_error:

            break;

        case input_capture_2:

            break;

        case output_compare_2:

            break;

        case external_interrupt_2:

            break;

        case timer3:

            break;

        case input_capture_3_error:

            break;

        case input_capture_3:

            break;

        case output_compare_3:

            break;

        case external_interrupt_3:

            break;

        case timer4:

            break;

        case input_capture_4_error:

            break;

        case input_capture_4:

            break;

        case output_compare_4:

            break;

        case external_interrupt_4:

            break;

        case timer5:

            break;

        case input_capture_5_error:

            break;

        case input_capture_5:

            break;

        case output_compare_5:

            break;

        case timer6:

            break;

        case input_capture_6_error:

            break;

        case input_capture_6:

            break;

        case output_compare_6:

            break;

        case timer7:

            break;

        case input_capture_7_error:

            break;

        case input_capture_7:

            break;

        case output_compare_7:

            break;

        case timer8:

            break;

        case input_capture_8_error:

            break;

        case input_capture_8:

            break;

        case output_compare_8:

            break;

        case timer9:

            break;

        case input_capture_9_error:

            break;

        case input_capture_9:

            break;

        case output_compare_9:

            break;

        case adc_global_interrupt:

            break;

        case adc_fifo_interrupt:

            break;

        case adc_digital_comparator_1:

            break;

        case adc_digital_comparator_2:

            break;

        case adc_digital_comparator_3:

            break;

        case adc_digital_comparator_4:

            break;

        case adc_digital_comparator_5:

            break;

        case adc_digital_comparator_6:

            break;

        case adc_digital_filter_1:

            break;

        case adc_digital_filter_2:

            break;

        case adc_digital_filter_3:

            break;

        case adc_digital_filter_4:

            break;

        case adc_digital_filter_5:

            break;

        case adc_digital_filter_6:

            break;

        case adc_fault:

            break;

        case adc_data_0:

            break;

        case adc_data_1:

            break;

        case adc_data_2:

            break;

        case adc_data_3:

            break;

        case adc_data_4:

            break;

        case adc_data_5:

            break;

        case adc_data_6:

            break;

        case adc_data_7:

            break;

        case adc_data_8:

            break;

        case adc_data_9:

            break;

        case adc_data_10:

            break;

        case adc_data_11:

            break;

        case adc_data_12:

            break;

        case adc_data_13:

            break;

        case adc_data_14:

            break;

        case adc_data_15:

            break;

        case adc_data_16:

            break;

        case adc_data_17:

            break;

        case adc_data_18:

            break;

        case adc_data_19:

            break;

        case adc_data_20:

            break;

        case adc_data_21:

            break;

        case adc_data_22:

            break;

        case adc_data_23:

            break;

        case adc_data_24:

            break;

        case adc_data_25:

            break;

        case adc_data_26:

            break;

        case adc_data_27:

            break;

        case adc_data_28:

            break;

        case adc_data_29:

            break;

        case adc_data_30:

            break;

        case adc_data_31:

            break;

        case adc_data_32:

            break;

        case adc_data_33:

            break;

        case adc_data_34:

            break;

        case adc_data_35:

            break;

        case adc_data_36:

            break;

        case adc_data_37:

            break;

        case adc_data_38:

            break;

        case adc_data_39:

            break;

        case adc_data_40:

            break;

        case adc_data_41:

            break;

        case adc_data_42:

            break;

        case adc_data_43:

            break;

        case usb_suspend_resume_event:

            break;

        case core_performance_counter_interrupt:

            break;

        case core_fast_debug_channel_interrupt:

            break;

        case system_bus_protection_violation:

            break;

        case crypto_engine_event:

            break;

        case spi1_fault:

            break;

        case spi1_receive_done:

            break;

        case spi1_transfer_done:

            break;

        case uart1_fault:

            break;

        case uart1_receive_done:

            break;

        case uart1_transfer_done:

            break;

        case i2c1_bus_collision_event:

            break;

        case i2c1_client_event:

            break;

        case i2c1_host_event:

            break;

        case porta_input_change_interrupt:

            break;

        case portb_input_change_interrupt:

            break;

        case portc_input_change_interrupt:

            break;

        case portd_input_change_interrupt:

            break;

        case porte_input_change_interrupt:

            break;

        case portf_input_change_interrupt:

            break;

        case portg_input_change_interrupt:

            break;

        case porth_input_change_interrupt:

            break;

        case portj_input_change_interrupt:

            break;

        case portk_input_change_interrupt:

            break;

        case pmp:

            break;

        case pmp_error:

            break;

        case comparator_1_interrupt:

            break;

        case comparator_2_interrupt:

            break;

        case usb_general_event:

            break;

        case usb_dma_event:

            break;

        case dma_channel_0:

            break;

        case dma_channel_1:

            break;

        case dma_channel_2:

            break;

        case dma_channel_3:

            break;

        case dma_channel_4:

            break;

        case dma_channel_5:

            break;

        case dma_channel_6:

            break;

        case dma_channel_7:

            break;

        case spi2_fault:

            break;

        case spi2_receive_done:

            break;

        case spi2_transfer_done:

            break;

        case uart2_fault:

            break;

        case uart2_receive_done:

            break;

        case uart2_transfer_done:

            break;

        case i2c2_bus_collision_event:

            break;

        case i2c2_client_event:

            break;

        case i2c2_host_event:

            break;

        case control_area_network_1:

            break;

        case control_area_network_2:

            break;

        case ethernet_interrupt:

            break;

        case spi3_fault:

            break;

        case spi3_receive_done:

            break;

        case spi3_transfer_done:

            break;

        case uart3_fault:

            break;

        case uart3_receive_done:

            break;

        case uart3_transfer_done:

            break;

        case i2c3_bus_collision_event:

            break;

        case i2c3_client_event:

            break;

        case i2c3_host_event:

            break;

        case spi4_fault:

            break;

        case spi4_receive_done:

            break;

        case spi4_transfer_done:

            break;

        case real_time_clock:

            break;

        case flash_control_event:

            break;

        case prefetch_module_sec_event:

            break;

        case sqi1_event:

            break;

        case uart4_fault:

            break;

        case uart4_receive_done:

            break;

        case uart4_transfer_done:

            break;

        case i2c4_bus_collision_event:

            break;

        case i2c4_client_event:

            break;

        case i2c4_host_event:

            break;

        case spi5_fault:

            break;

        case spi5_receive_done:

            break;

        case spi5_transfer_done:

            break;

        case uart5_fault:

            break;

        case uart5_receive_done:

            break;

        case uart5_transfer_done:

            break;

        case i2c5_bus_collision_event:

            break;

        case i2c5_client_event:

            break;

        case i2c5_host_event:

            break;

        case spi6_fault:

            break;

        case spi6_receive_done:

            break;

        case spi6_transfer_done:

            break;

        case uart6_fault:

            break;

        case uart6_receive_done:

            break;

        case uart6_transfer_done:

            break;

        case sdhc_interrupt:

            break;

        case glcd_interrupt:

            break;

        case gpu_interrupt:

            break;

        case ctmu_interrupt:

            break;

        case adc_end_of_scan:

            break;

        case adc_analog_circuit_ready:

            break;

        case adc_update_ready:

            break;

        case adc0_early_interrupt:

            break;

        case adc1_early_interrupt:

            break;

        case adc2_early_interrupt:

            break;

        case adc3_early_interrupt:

            break;

        case adc4_early_interrupt:

            break;

        case adc_group_early_interrupt_request:

            break;

        case adc7_early_interrupt:

            break;

        case adc0_warm_interrupt:

            break;

        case adc1_warm_interrupt:

            break;

        case adc2_warm_interrupt:

            break;

        case adc3_warm_interrupt:

            break;

        case adc4_warm_interrupt:

            break;

        case adc7_warm_interrupt:

            break;

        case mpll_fault_interrupt:

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

            break;

        case core_software_interrupt_0:

            break;

        case core_software_interrupt_1:

            break;

        case external_interrupt_0:

            break;

        case timer1:

            break;

        case input_capture_1_error:

            break;

        case input_capture_1:

            break;

        case output_compare_1:

            break;

        case external_interrupt_1:

            break;

        case timer2:

            break;

        case input_capture_2_error:

            break;

        case input_capture_2:

            break;

        case output_compare_2:

            break;

        case external_interrupt_2:

            break;

        case timer3:

            break;

        case input_capture_3_error:

            break;

        case input_capture_3:

            break;

        case output_compare_3:

            break;

        case external_interrupt_3:

            break;

        case timer4:

            break;

        case input_capture_4_error:

            break;

        case input_capture_4:

            break;

        case output_compare_4:

            break;

        case external_interrupt_4:

            break;

        case timer5:

            break;

        case input_capture_5_error:

            break;

        case input_capture_5:

            break;

        case output_compare_5:

            break;

        case timer6:

            break;

        case input_capture_6_error:

            break;

        case input_capture_6:

            break;

        case output_compare_6:

            break;

        case timer7:

            break;

        case input_capture_7_error:

            break;

        case input_capture_7:

            break;

        case output_compare_7:

            break;

        case timer8:

            break;

        case input_capture_8_error:

            break;

        case input_capture_8:

            break;

        case output_compare_8:

            break;

        case timer9:

            break;

        case input_capture_9_error:

            break;

        case input_capture_9:

            break;

        case output_compare_9:

            break;

        case adc_global_interrupt:

            break;

        case adc_fifo_interrupt:

            break;

        case adc_digital_comparator_1:

            break;

        case adc_digital_comparator_2:

            break;

        case adc_digital_comparator_3:

            break;

        case adc_digital_comparator_4:

            break;

        case adc_digital_comparator_5:

            break;

        case adc_digital_comparator_6:

            break;

        case adc_digital_filter_1:

            break;

        case adc_digital_filter_2:

            break;

        case adc_digital_filter_3:

            break;

        case adc_digital_filter_4:

            break;

        case adc_digital_filter_5:

            break;

        case adc_digital_filter_6:

            break;

        case adc_fault:

            break;

        case adc_data_0:

            break;

        case adc_data_1:

            break;

        case adc_data_2:

            break;

        case adc_data_3:

            break;

        case adc_data_4:

            break;

        case adc_data_5:

            break;

        case adc_data_6:

            break;

        case adc_data_7:

            break;

        case adc_data_8:

            break;

        case adc_data_9:

            break;

        case adc_data_10:

            break;

        case adc_data_11:

            break;

        case adc_data_12:

            break;

        case adc_data_13:

            break;

        case adc_data_14:

            break;

        case adc_data_15:

            break;

        case adc_data_16:

            break;

        case adc_data_17:

            break;

        case adc_data_18:

            break;

        case adc_data_19:

            break;

        case adc_data_20:

            break;

        case adc_data_21:

            break;

        case adc_data_22:

            break;

        case adc_data_23:

            break;

        case adc_data_24:

            break;

        case adc_data_25:

            break;

        case adc_data_26:

            break;

        case adc_data_27:

            break;

        case adc_data_28:

            break;

        case adc_data_29:

            break;

        case adc_data_30:

            break;

        case adc_data_31:

            break;

        case adc_data_32:

            break;

        case adc_data_33:

            break;

        case adc_data_34:

            break;

        case adc_data_35:

            break;

        case adc_data_36:

            break;

        case adc_data_37:

            break;

        case adc_data_38:

            break;

        case adc_data_39:

            break;

        case adc_data_40:

            break;

        case adc_data_41:

            break;

        case adc_data_42:

            break;

        case adc_data_43:

            break;

        case usb_suspend_resume_event:

            break;

        case core_performance_counter_interrupt:

            break;

        case core_fast_debug_channel_interrupt:

            break;

        case system_bus_protection_violation:

            break;

        case crypto_engine_event:

            break;

        case spi1_fault:

            break;

        case spi1_receive_done:

            break;

        case spi1_transfer_done:

            break;

        case uart1_fault:

            break;

        case uart1_receive_done:

            break;

        case uart1_transfer_done:

            break;

        case i2c1_bus_collision_event:

            break;

        case i2c1_client_event:

            break;

        case i2c1_host_event:

            break;

        case porta_input_change_interrupt:

            break;

        case portb_input_change_interrupt:

            break;

        case portc_input_change_interrupt:

            break;

        case portd_input_change_interrupt:

            break;

        case porte_input_change_interrupt:

            break;

        case portf_input_change_interrupt:

            break;

        case portg_input_change_interrupt:

            break;

        case porth_input_change_interrupt:

            break;

        case portj_input_change_interrupt:

            break;

        case portk_input_change_interrupt:

            break;

        case pmp:

            break;

        case pmp_error:

            break;

        case comparator_1_interrupt:

            break;

        case comparator_2_interrupt:

            break;

        case usb_general_event:

            break;

        case usb_dma_event:

            break;

        case dma_channel_0:

            break;

        case dma_channel_1:

            break;

        case dma_channel_2:

            break;

        case dma_channel_3:

            break;

        case dma_channel_4:

            break;

        case dma_channel_5:

            break;

        case dma_channel_6:

            break;

        case dma_channel_7:

            break;

        case spi2_fault:

            break;

        case spi2_receive_done:

            break;

        case spi2_transfer_done:

            break;

        case uart2_fault:

            break;

        case uart2_receive_done:

            break;

        case uart2_transfer_done:

            break;

        case i2c2_bus_collision_event:

            break;

        case i2c2_client_event:

            break;

        case i2c2_host_event:

            break;

        case control_area_network_1:

            break;

        case control_area_network_2:

            break;

        case ethernet_interrupt:

            break;

        case spi3_fault:

            break;

        case spi3_receive_done:

            break;

        case spi3_transfer_done:

            break;

        case uart3_fault:

            break;

        case uart3_receive_done:

            break;

        case uart3_transfer_done:

            break;

        case i2c3_bus_collision_event:

            break;

        case i2c3_client_event:

            break;

        case i2c3_host_event:

            break;

        case spi4_fault:

            break;

        case spi4_receive_done:

            break;

        case spi4_transfer_done:

            break;

        case real_time_clock:

            break;

        case flash_control_event:

            break;

        case prefetch_module_sec_event:

            break;

        case sqi1_event:

            break;

        case uart4_fault:

            break;

        case uart4_receive_done:

            break;

        case uart4_transfer_done:

            break;

        case i2c4_bus_collision_event:

            break;

        case i2c4_client_event:

            break;

        case i2c4_host_event:

            break;

        case spi5_fault:

            break;

        case spi5_receive_done:

            break;

        case spi5_transfer_done:

            break;

        case uart5_fault:

            break;

        case uart5_receive_done:

            break;

        case uart5_transfer_done:

            break;

        case i2c5_bus_collision_event:

            break;

        case i2c5_client_event:

            break;

        case i2c5_host_event:

            break;

        case spi6_fault:

            break;

        case spi6_receive_done:

            break;

        case spi6_transfer_done:

            break;

        case uart6_fault:

            break;

        case uart6_receive_done:

            break;

        case uart6_transfer_done:

            break;

        case sdhc_interrupt:

            break;

        case glcd_interrupt:

            break;

        case gpu_interrupt:

            break;

        case ctmu_interrupt:

            break;

        case adc_end_of_scan:

            break;

        case adc_analog_circuit_ready:

            break;

        case adc_update_ready:

            break;

        case adc0_early_interrupt:

            break;

        case adc1_early_interrupt:

            break;

        case adc2_early_interrupt:

            break;

        case adc3_early_interrupt:

            break;

        case adc4_early_interrupt:

            break;

        case adc_group_early_interrupt_request:

            break;

        case adc7_early_interrupt:

            break;

        case adc0_warm_interrupt:

            break;

        case adc1_warm_interrupt:

            break;

        case adc2_warm_interrupt:

            break;

        case adc3_warm_interrupt:

            break;

        case adc4_warm_interrupt:

            break;

        case adc7_warm_interrupt:

            break;

        case mpll_fault_interrupt:

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

            break;

        case core_software_interrupt_0:

            break;

        case core_software_interrupt_1:

            break;

        case external_interrupt_0:

            break;

        case timer1:

            break;

        case input_capture_1_error:

            break;

        case input_capture_1:

            break;

        case output_compare_1:

            break;

        case external_interrupt_1:

            break;

        case timer2:

            break;

        case input_capture_2_error:

            break;

        case input_capture_2:

            break;

        case output_compare_2:

            break;

        case external_interrupt_2:

            break;

        case timer3:

            break;

        case input_capture_3_error:

            break;

        case input_capture_3:

            break;

        case output_compare_3:

            break;

        case external_interrupt_3:

            break;

        case timer4:

            break;

        case input_capture_4_error:

            break;

        case input_capture_4:

            break;

        case output_compare_4:

            break;

        case external_interrupt_4:

            break;

        case timer5:

            break;

        case input_capture_5_error:

            break;

        case input_capture_5:

            break;

        case output_compare_5:

            break;

        case timer6:

            break;

        case input_capture_6_error:

            break;

        case input_capture_6:

            break;

        case output_compare_6:

            break;

        case timer7:

            break;

        case input_capture_7_error:

            break;

        case input_capture_7:

            break;

        case output_compare_7:

            break;

        case timer8:

            break;

        case input_capture_8_error:

            break;

        case input_capture_8:

            break;

        case output_compare_8:

            break;

        case timer9:

            break;

        case input_capture_9_error:

            break;

        case input_capture_9:

            break;

        case output_compare_9:

            break;

        case adc_global_interrupt:

            break;

        case adc_fifo_interrupt:

            break;

        case adc_digital_comparator_1:

            break;

        case adc_digital_comparator_2:

            break;

        case adc_digital_comparator_3:

            break;

        case adc_digital_comparator_4:

            break;

        case adc_digital_comparator_5:

            break;

        case adc_digital_comparator_6:

            break;

        case adc_digital_filter_1:

            break;

        case adc_digital_filter_2:

            break;

        case adc_digital_filter_3:

            break;

        case adc_digital_filter_4:

            break;

        case adc_digital_filter_5:

            break;

        case adc_digital_filter_6:

            break;

        case adc_fault:

            break;

        case adc_data_0:

            break;

        case adc_data_1:

            break;

        case adc_data_2:

            break;

        case adc_data_3:

            break;

        case adc_data_4:

            break;

        case adc_data_5:

            break;

        case adc_data_6:

            break;

        case adc_data_7:

            break;

        case adc_data_8:

            break;

        case adc_data_9:

            break;

        case adc_data_10:

            break;

        case adc_data_11:

            break;

        case adc_data_12:

            break;

        case adc_data_13:

            break;

        case adc_data_14:

            break;

        case adc_data_15:

            break;

        case adc_data_16:

            break;

        case adc_data_17:

            break;

        case adc_data_18:

            break;

        case adc_data_19:

            break;

        case adc_data_20:

            break;

        case adc_data_21:

            break;

        case adc_data_22:

            break;

        case adc_data_23:

            break;

        case adc_data_24:

            break;

        case adc_data_25:

            break;

        case adc_data_26:

            break;

        case adc_data_27:

            break;

        case adc_data_28:

            break;

        case adc_data_29:

            break;

        case adc_data_30:

            break;

        case adc_data_31:

            break;

        case adc_data_32:

            break;

        case adc_data_33:

            break;

        case adc_data_34:

            break;

        case adc_data_35:

            break;

        case adc_data_36:

            break;

        case adc_data_37:

            break;

        case adc_data_38:

            break;

        case adc_data_39:

            break;

        case adc_data_40:

            break;

        case adc_data_41:

            break;

        case adc_data_42:

            break;

        case adc_data_43:

            break;

        case usb_suspend_resume_event:

            break;

        case core_performance_counter_interrupt:

            break;

        case core_fast_debug_channel_interrupt:

            break;

        case system_bus_protection_violation:

            break;

        case crypto_engine_event:

            break;

        case spi1_fault:

            break;

        case spi1_receive_done:

            break;

        case spi1_transfer_done:

            break;

        case uart1_fault:

            break;

        case uart1_receive_done:

            break;

        case uart1_transfer_done:

            break;

        case i2c1_bus_collision_event:

            break;

        case i2c1_client_event:

            break;

        case i2c1_host_event:

            break;

        case porta_input_change_interrupt:

            break;

        case portb_input_change_interrupt:

            break;

        case portc_input_change_interrupt:

            break;

        case portd_input_change_interrupt:

            break;

        case porte_input_change_interrupt:

            break;

        case portf_input_change_interrupt:

            break;

        case portg_input_change_interrupt:

            break;

        case porth_input_change_interrupt:

            break;

        case portj_input_change_interrupt:

            break;

        case portk_input_change_interrupt:

            break;

        case pmp:

            break;

        case pmp_error:

            break;

        case comparator_1_interrupt:

            break;

        case comparator_2_interrupt:

            break;

        case usb_general_event:

            break;

        case usb_dma_event:

            break;

        case dma_channel_0:

            break;

        case dma_channel_1:

            break;

        case dma_channel_2:

            break;

        case dma_channel_3:

            break;

        case dma_channel_4:

            break;

        case dma_channel_5:

            break;

        case dma_channel_6:

            break;

        case dma_channel_7:

            break;

        case spi2_fault:

            break;

        case spi2_receive_done:

            break;

        case spi2_transfer_done:

            break;

        case uart2_fault:

            break;

        case uart2_receive_done:

            break;

        case uart2_transfer_done:

            break;

        case i2c2_bus_collision_event:

            break;

        case i2c2_client_event:

            break;

        case i2c2_host_event:

            break;

        case control_area_network_1:

            break;

        case control_area_network_2:

            break;

        case ethernet_interrupt:

            break;

        case spi3_fault:

            break;

        case spi3_receive_done:

            break;

        case spi3_transfer_done:

            break;

        case uart3_fault:

            break;

        case uart3_receive_done:

            break;

        case uart3_transfer_done:

            break;

        case i2c3_bus_collision_event:

            break;

        case i2c3_client_event:

            break;

        case i2c3_host_event:

            break;

        case spi4_fault:

            break;

        case spi4_receive_done:

            break;

        case spi4_transfer_done:

            break;

        case real_time_clock:

            break;

        case flash_control_event:

            break;

        case prefetch_module_sec_event:

            break;

        case sqi1_event:

            break;

        case uart4_fault:

            break;

        case uart4_receive_done:

            break;

        case uart4_transfer_done:

            break;

        case i2c4_bus_collision_event:

            break;

        case i2c4_client_event:

            break;

        case i2c4_host_event:

            break;

        case spi5_fault:

            break;

        case spi5_receive_done:

            break;

        case spi5_transfer_done:

            break;

        case uart5_fault:

            break;

        case uart5_receive_done:

            break;

        case uart5_transfer_done:

            break;

        case i2c5_bus_collision_event:

            break;

        case i2c5_client_event:

            break;

        case i2c5_host_event:

            break;

        case spi6_fault:

            break;

        case spi6_receive_done:

            break;

        case spi6_transfer_done:

            break;

        case uart6_fault:

            break;

        case uart6_receive_done:

            break;

        case uart6_transfer_done:

            break;

        case sdhc_interrupt:

            break;

        case glcd_interrupt:

            break;

        case gpu_interrupt:

            break;

        case ctmu_interrupt:

            break;

        case adc_end_of_scan:

            break;

        case adc_analog_circuit_ready:

            break;

        case adc_update_ready:

            break;

        case adc0_early_interrupt:

            break;

        case adc1_early_interrupt:

            break;

        case adc2_early_interrupt:

            break;

        case adc3_early_interrupt:

            break;

        case adc4_early_interrupt:

            break;

        case adc_group_early_interrupt_request:

            break;

        case adc7_early_interrupt:

            break;

        case adc0_warm_interrupt:

            break;

        case adc1_warm_interrupt:

            break;

        case adc2_warm_interrupt:

            break;

        case adc3_warm_interrupt:

            break;

        case adc4_warm_interrupt:

            break;

        case adc7_warm_interrupt:

            break;

        case mpll_fault_interrupt:

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
        "ctmu_interrupt                     ",
        "adc_end_of_scan                    ",
        "adc_analog_circuit_ready           ",
        "adc_update_ready                   ",
        "adc0_early_interrupt               ",
        "adc1_early_interrupt               ",
        "adc2_early_interrupt               ",
        "adc3_early_interrupt               ",
        "adc4_early_interrupt               ",
        "adc_group_early_interrupt_request  ",
        "adc7_early_interrupt               ",
        "adc0_warm_interrupt                ",
        "adc1_warm_interrupt                ",
        "adc2_warm_interrupt                ",
        "adc3_warm_interrupt                ",
        "adc4_warm_interrupt                ",
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