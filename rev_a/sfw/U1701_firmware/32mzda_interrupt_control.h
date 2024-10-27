/* ************************************************************************** */
/** Interrupt Control APIs

  @Company
    Marquette Senior Design E44

  @File Name
    32mz_interrupt_control.h

  @Summary
    Easier interface for enabling and disabling interrupts, checking interrupt flags, etc
    on the PIC32MZ family
  
  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _32MZDA_INTERRUPT_CONTROL_H    /* Guard against multiple inclusion */
#define _32MZDA_INTERRUPT_CONTROL_H

#include <xc.h>

#include "sys/attribs.h"

// This typdef describes all of the interrupt sources available on the 32MZ EF family
typedef enum {

  core_timer_interrupt                  = 0,
  core_software_interrupt_0             = 1,
  core_software_interrupt_1             = 2,
  external_interrupt_0                  = 3,
  timer1                                = 4,
  input_capture_1_error                 = 5,
  input_capture_1                       = 6,
  output_compare_1                      = 7,
  external_interrupt_1                  = 8,
  timer2                                = 9,
  input_capture_2_error                 = 10,
  input_capture_2                       = 11,
  output_compare_2                      = 12,
  external_interrupt_2                  = 13,
  timer3                                = 14,
  input_capture_3_error                 = 15,
  input_capture_3                       = 16,
  output_compare_3                      = 17,
  external_interrupt_3                  = 18,
  timer4                                = 19,
  input_capture_4_error                 = 20,
  input_capture_4                       = 21,
  output_compare_4                      = 22,
  external_interrupt_4                  = 23,
  timer5                                = 24,
  input_capture_5_error                 = 25,
  input_capture_5                       = 26,
  output_compare_5                      = 27,
  timer6                                = 28,
  input_capture_6_error                 = 29,
  input_capture_6                       = 30,
  output_compare_6                      = 31,
  timer7                                = 32,
  input_capture_7_error                 = 33,
  input_capture_7                       = 34,
  output_compare_7                      = 35,
  timer8                                = 36,
  input_capture_8_error                 = 37,
  input_capture_8                       = 38,
  output_compare_8                      = 39,
  timer9                                = 40,
  input_capture_9_error                 = 41,
  input_capture_9                       = 42,
  output_compare_9                      = 43,
  adc_global_interrupt                  = 44,
  adc_fifo_interrupt                    = 45,
  adc_digital_comparator_1              = 46,
  adc_digital_comparator_2              = 47,
  adc_digital_comparator_3              = 48,
  adc_digital_comparator_4              = 49,
  adc_digital_comparator_5              = 50,
  adc_digital_comparator_6              = 51,
  adc_digital_filter_1                  = 52,
  adc_digital_filter_2                  = 53,
  adc_digital_filter_3                  = 54,
  adc_digital_filter_4                  = 55,
  adc_digital_filter_5                  = 56,
  adc_digital_filter_6                  = 57,
  adc_fault                             = 58,
  adc_data_0                            = 59,
  adc_data_1                            = 60,
  adc_data_2                            = 61,
  adc_data_3                            = 62,
  adc_data_4                            = 63,
  adc_data_5                            = 64,
  adc_data_6                            = 65,
  adc_data_7                            = 66,
  adc_data_8                            = 67,
  adc_data_9                            = 68,
  adc_data_10                           = 69,
  adc_data_11                           = 70,
  adc_data_12                           = 71,
  adc_data_13                           = 72,
  adc_data_14                           = 73,
  adc_data_15                           = 74,
  adc_data_16                           = 75,
  adc_data_17                           = 76,
  adc_data_18                           = 77,
  adc_data_19                           = 78,
  adc_data_20                           = 79,
  adc_data_21                           = 80,
  adc_data_22                           = 81,
  adc_data_23                           = 82,
  adc_data_24                           = 83,
  adc_data_25                           = 84,
  adc_data_26                           = 85,
  adc_data_27                           = 86,
  adc_data_28                           = 87,
  adc_data_29                           = 88,
  adc_data_30                           = 89,
  adc_data_31                           = 90,
  adc_data_32                           = 91,
  adc_data_33                           = 92,
  adc_data_34                           = 93,
  adc_data_35                           = 94,
  adc_data_36                           = 95,
  adc_data_37                           = 96,
  adc_data_38                           = 97,
  adc_data_39                           = 98,
  adc_data_40                           = 99,
  adc_data_41                           = 100,
  adc_data_42                           = 101,
  adc_data_43                           = 102,
  usb_suspend_resume_event              = 103,
  core_performance_counter_interrupt    = 104,
  core_fast_debug_channel_interrupt     = 105,
  system_bus_protection_violation       = 106,
  crypto_engine_event                   = 107,
  spi1_fault                            = 109,
  spi1_receive_done                     = 110,
  spi1_transfer_done                    = 111,
  uart1_fault                           = 112,
  uart1_receive_done                    = 113,
  uart1_transfer_done                   = 114,
  i2c1_bus_collision_event              = 115,
  i2c1_client_event                     = 116,
  i2c1_host_event                       = 117,
  porta_input_change_interrupt          = 118,
  portb_input_change_interrupt          = 119,
  portc_input_change_interrupt          = 120,
  portd_input_change_interrupt          = 121,
  porte_input_change_interrupt          = 122,
  portf_input_change_interrupt          = 123,
  portg_input_change_interrupt          = 124,
  porth_input_change_interrupt          = 125,
  portj_input_change_interrupt          = 126,
  portk_input_change_interrupt          = 127,
  pmp                                   = 128,
  pmp_error                             = 129,
  comparator_1_interrupt                = 130,
  comparator_2_interrupt                = 131,
  usb_general_event                     = 132,
  usb_dma_event                         = 133,
  dma_channel_0                         = 134,
  dma_channel_1                         = 135,
  dma_channel_2                         = 136,
  dma_channel_3                         = 137,
  dma_channel_4                         = 138,
  dma_channel_5                         = 139,
  dma_channel_6                         = 140,
  dma_channel_7                         = 141,
  spi2_fault                            = 142,
  spi2_receive_done                     = 143,
  spi2_transfer_done                    = 144,
  uart2_fault                           = 145,
  uart2_receive_done                    = 146,
  uart2_transfer_done                   = 147,
  i2c2_bus_collision_event              = 148,
  i2c2_client_event                     = 149,
  i2c2_host_event                       = 150,
  control_area_network_1                = 151,
  control_area_network_2                = 152,
  ethernet_interrupt                    = 153,
  spi3_fault                            = 154,
  spi3_receive_done                     = 155,
  spi3_transfer_done                    = 156,
  uart3_fault                           = 157,
  uart3_receive_done                    = 158,
  uart3_transfer_done                   = 159,
  i2c3_bus_collision_event              = 160,
  i2c3_client_event                     = 161,
  i2c3_host_event                       = 162,
  spi4_fault                            = 163,
  spi4_receive_done                     = 164,
  spi4_transfer_done                    = 165,
  real_time_clock                       = 166,
  flash_control_event                   = 167,
  prefetch_module_sec_event             = 168,
  sqi1_event                            = 169,
  uart4_fault                           = 170,
  uart4_receive_done                    = 171,
  uart4_transfer_done                   = 172,
  i2c4_bus_collision_event              = 173,
  i2c4_client_event                     = 174,
  i2c4_host_event                       = 175,
  spi5_fault                            = 176,
  spi5_receive_done                     = 177,
  spi5_transfer_done                    = 178,
  uart5_fault                           = 179,
  uart5_receive_done                    = 180,
  uart5_transfer_done                   = 181,
  i2c5_bus_collision_event              = 182,
  i2c5_client_event                     = 183,
  i2c5_host_event                       = 184,
  spi6_fault                            = 185,
  spi6_receive_done                     = 186,
  spi6_transfer_done                    = 187,
  uart6_fault                           = 188,
  uart6_receive_done                    = 189,
  uart6_transfer_done                   = 190,
  sdhc_interrupt                        = 191,
  glcd_interrupt                        = 192,
  gpu_interrupt                         = 193,
  ctmu_interrupt                        = 195,
  adc_end_of_scan                       = 196,
  adc_analog_circuit_ready              = 197,
  adc_update_ready                      = 198,
  adc0_early_interrupt                  = 199,
  adc1_early_interrupt                  = 200,
  adc2_early_interrupt                  = 201,
  adc3_early_interrupt                  = 202,
  adc4_early_interrupt                  = 203,
  adc_group_early_interrupt_request     = 205,
  adc7_early_interrupt                  = 206,
  adc0_warm_interrupt                   = 207,
  adc1_warm_interrupt                   = 208,
  adc2_warm_interrupt                   = 209,
  adc3_warm_interrupt                   = 210,
  adc4_warm_interrupt                   = 211,
  adc7_warm_interrupt                   = 214,
  mpll_fault_interrupt                  = 215

} interrupt_source_t;

// This function configures the system for multi-interrupt operation and
// assigns shadow registers sets to priority level ISRs
void interruptControllerInitialize(void);

// This function enables global interrupts
void enableGlobalInterrupts(void);

// This function disables global interrupts
void disableGlobalInterrupts(void);

// This function returns the state of global interrupt enable
// Returns 0 if global interrupts are disabled
// returns 1 if global interrupts are enabled
uint8_t getGlobalInterruptsState(void);

// This function explicitly sets the state of global interrupts
void setGlobalInterruptsState(uint8_t);

// This function allows for the setting/clearing of a given interrupt enable
// It manipulates the interrupt's 'Interrupt Enable' bit with IEC registers
void setInterruptEnable(interrupt_source_t input_interrupt, uint8_t input_state);

// This function allows for the reading of a given interrupt enable
// It reads the interrupt's 'Interrupt Enable' bit with IEC registers
// Returns the state of the given interrupt
uint8_t getInterruptEnable(interrupt_source_t input_interrupt);

// This function allows for the setting/clearing of a given interrupt flag
// It manipulates the interrupt's 'Interrupt Flag' bit
void setInterruptFlag(interrupt_source_t input_interrupt, uint8_t input_state);

// This function allows for the reading of a given interrupt flag
// It reads the interrupt's 'Interrupt Flag' bit
// Returns the state of the given interrupt flag
uint8_t getInterruptFlag(interrupt_source_t input_interrupt);

// This function sets the priority for a given interrupt
void setInterruptPriority(interrupt_source_t input_interrupt, uint8_t input_priority);

// This function sets the subpriority for a given interrupt
void setInterruptSubpriority(interrupt_source_t input_interrupt, uint8_t input_subpriority);

// This function returns the given interrupt priority
uint8_t getInterruptPriority(interrupt_source_t input_interrupt);

// This function returns the given interrupt subpriority
uint8_t getInterruptSubriority(interrupt_source_t input_interrupt);

// This function enables selected interrupt
void enableInterrupt(interrupt_source_t input_interrupt);

// This function disables selected interrupt
void disableInterrupt(interrupt_source_t input_interrupt);

// This function clears selected interrupt flag
void clearInterruptFlag(interrupt_source_t input_interrupt);

// This function returns a string of the given interrupt name
char * getInterruptNameStringPadded(interrupt_source_t input_interrupt);

// This function prints information on all interrupt settings
void printInterruptStatus(void);

#endif /* _32MZDA_INTERRUPT_CONTROL_H */

/* *****************************************************************************
 End of File
 */