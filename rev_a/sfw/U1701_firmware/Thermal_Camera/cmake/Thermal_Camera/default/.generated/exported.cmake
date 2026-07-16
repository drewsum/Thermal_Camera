set(DEPENDENT_MP_BIN2HEXThermal_Camera_default_sTVxgZYV "c:/Program Files/Microchip/xc32/v4.45/bin/xc32-bin2hex.exe")
set(DEPENDENT_DEPENDENT_TARGET_ELFThermal_Camera_default_sTVxgZYV "${CMAKE_CURRENT_LIST_DIR}/../../../../out/Thermal_Camera/default.elf")
set(DEPENDENT_TARGET_DIRThermal_Camera_default_sTVxgZYV "${CMAKE_CURRENT_LIST_DIR}/../../../../out/Thermal_Camera")
set(DEPENDENT_BYPRODUCTSThermal_Camera_default_sTVxgZYV ${DEPENDENT_TARGET_DIRThermal_Camera_default_sTVxgZYV}/${sourceFileNameThermal_Camera_default_sTVxgZYV}.c)
add_custom_command(
    OUTPUT ${DEPENDENT_TARGET_DIRThermal_Camera_default_sTVxgZYV}/${sourceFileNameThermal_Camera_default_sTVxgZYV}.c
    COMMAND ${DEPENDENT_MP_BIN2HEXThermal_Camera_default_sTVxgZYV} --image ${DEPENDENT_DEPENDENT_TARGET_ELFThermal_Camera_default_sTVxgZYV} --image-generated-c ${sourceFileNameThermal_Camera_default_sTVxgZYV}.c --image-generated-h ${sourceFileNameThermal_Camera_default_sTVxgZYV}.h --image-copy-mode ${modeThermal_Camera_default_sTVxgZYV} --image-offset ${addressThermal_Camera_default_sTVxgZYV} 
    WORKING_DIRECTORY ${DEPENDENT_TARGET_DIRThermal_Camera_default_sTVxgZYV}
    DEPENDS ${DEPENDENT_DEPENDENT_TARGET_ELFThermal_Camera_default_sTVxgZYV})
add_custom_target(
    dependent_produced_source_artifactThermal_Camera_default_sTVxgZYV 
    DEPENDS ${DEPENDENT_TARGET_DIRThermal_Camera_default_sTVxgZYV}/${sourceFileNameThermal_Camera_default_sTVxgZYV}.c
    )
