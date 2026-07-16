# The following variables contains the files used by the different stages of the build process.
set(Thermal_Camera_default_default_XC32_FILE_TYPE_assemble)
set_source_files_properties(${Thermal_Camera_default_default_XC32_FILE_TYPE_assemble} PROPERTIES LANGUAGE ASM)

# For assembly files, add "." to the include path for each file so that .include with a relative path works
foreach(source_file ${Thermal_Camera_default_default_XC32_FILE_TYPE_assemble})
        set_source_files_properties(${source_file} PROPERTIES INCLUDE_DIRECTORIES "$<PATH:NORMAL_PATH,$<PATH:REMOVE_FILENAME,${source_file}>>")
endforeach()

set(Thermal_Camera_default_default_XC32_FILE_TYPE_assembleWithPreprocess)
set_source_files_properties(${Thermal_Camera_default_default_XC32_FILE_TYPE_assembleWithPreprocess} PROPERTIES LANGUAGE ASM)

# For assembly files, add "." to the include path for each file so that .include with a relative path works
foreach(source_file ${Thermal_Camera_default_default_XC32_FILE_TYPE_assembleWithPreprocess})
        set_source_files_properties(${source_file} PROPERTIES INCLUDE_DIRECTORIES "$<PATH:NORMAL_PATH,$<PATH:REMOVE_FILENAME,${source_file}>>")
endforeach()

set(Thermal_Camera_default_default_XC32_FILE_TYPE_compile
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../adc/adc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../application/adc_channels.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../application/error_handler.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../application/heartbeat_services.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../application/pgood_monitor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../application/power_saving.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../application/pushbuttons.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../application/telemetry.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/32mzda_interrupt_control.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/cause_of_reset.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/ddr2.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/device_control.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/heartbeat_timer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/hlvd.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/prefetch.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/rtcc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/watchdog_timer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../gpio/pic32mzda_gpio_setup.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../i2c/device_driver/ds1683.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../i2c/device_driver/ina231a.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../i2c/device_driver/mcp9804.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../i2c/i2c_devices.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../i2c/i2c_master.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdhc/device_driver/sd_card.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdhc/fatfs/diskio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdhc/fatfs/ff.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdhc/sd_fileio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdhc/sdhc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../spi/device_driver/sst25vf080b.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../spi/spi3.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../usb_uart/terminal_control.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../usb_uart/usb_uart.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../usb_uart/usb_uart_rx_lookup_table.c")
set_source_files_properties(${Thermal_Camera_default_default_XC32_FILE_TYPE_compile} PROPERTIES LANGUAGE C)
set(Thermal_Camera_default_default_XC32_FILE_TYPE_compile_cpp)
set_source_files_properties(${Thermal_Camera_default_default_XC32_FILE_TYPE_compile_cpp} PROPERTIES LANGUAGE CXX)
set(Thermal_Camera_default_default_XC32_FILE_TYPE_link)
set(Thermal_Camera_default_default_XC32_FILE_TYPE_bin2hex)
set(Thermal_Camera_default_image_name "default.elf")
set(Thermal_Camera_default_image_base_name "default")

# The output directory of the final image.
set(Thermal_Camera_default_output_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../out/Thermal_Camera")

# The full path to the final image.
set(Thermal_Camera_default_full_path_to_image ${Thermal_Camera_default_output_dir}/${Thermal_Camera_default_image_name})
