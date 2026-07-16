include("${CMAKE_CURRENT_LIST_DIR}/rule.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/file.cmake")

set(Thermal_Camera_default_library_list )

# Handle files with suffix s, for group default-XC32
if(Thermal_Camera_default_default_XC32_FILE_TYPE_assemble)
add_library(Thermal_Camera_default_default_XC32_assemble OBJECT ${Thermal_Camera_default_default_XC32_FILE_TYPE_assemble})
    Thermal_Camera_default_default_XC32_assemble_rule(Thermal_Camera_default_default_XC32_assemble)
    list(APPEND Thermal_Camera_default_library_list "$<TARGET_OBJECTS:Thermal_Camera_default_default_XC32_assemble>")

endif()

# Handle files with suffix S, for group default-XC32
if(Thermal_Camera_default_default_XC32_FILE_TYPE_assembleWithPreprocess)
add_library(Thermal_Camera_default_default_XC32_assembleWithPreprocess OBJECT ${Thermal_Camera_default_default_XC32_FILE_TYPE_assembleWithPreprocess})
    Thermal_Camera_default_default_XC32_assembleWithPreprocess_rule(Thermal_Camera_default_default_XC32_assembleWithPreprocess)
    list(APPEND Thermal_Camera_default_library_list "$<TARGET_OBJECTS:Thermal_Camera_default_default_XC32_assembleWithPreprocess>")

endif()

# Handle files with suffix [cC], for group default-XC32
if(Thermal_Camera_default_default_XC32_FILE_TYPE_compile)
add_library(Thermal_Camera_default_default_XC32_compile OBJECT ${Thermal_Camera_default_default_XC32_FILE_TYPE_compile})
    Thermal_Camera_default_default_XC32_compile_rule(Thermal_Camera_default_default_XC32_compile)
    list(APPEND Thermal_Camera_default_library_list "$<TARGET_OBJECTS:Thermal_Camera_default_default_XC32_compile>")

endif()

# Handle files with suffix cpp, for group default-XC32
if(Thermal_Camera_default_default_XC32_FILE_TYPE_compile_cpp)
add_library(Thermal_Camera_default_default_XC32_compile_cpp OBJECT ${Thermal_Camera_default_default_XC32_FILE_TYPE_compile_cpp})
    Thermal_Camera_default_default_XC32_compile_cpp_rule(Thermal_Camera_default_default_XC32_compile_cpp)
    list(APPEND Thermal_Camera_default_library_list "$<TARGET_OBJECTS:Thermal_Camera_default_default_XC32_compile_cpp>")

endif()

# Handle files with suffix [cC], for group default-XC32
if(Thermal_Camera_default_default_XC32_FILE_TYPE_dependentObject)
add_library(Thermal_Camera_default_default_XC32_dependentObject OBJECT ${Thermal_Camera_default_default_XC32_FILE_TYPE_dependentObject})
    Thermal_Camera_default_default_XC32_dependentObject_rule(Thermal_Camera_default_default_XC32_dependentObject)
    list(APPEND Thermal_Camera_default_library_list "$<TARGET_OBJECTS:Thermal_Camera_default_default_XC32_dependentObject>")

endif()

# Handle files with suffix elf, for group default-XC32
if(Thermal_Camera_default_default_XC32_FILE_TYPE_bin2hex)
add_library(Thermal_Camera_default_default_XC32_bin2hex OBJECT ${Thermal_Camera_default_default_XC32_FILE_TYPE_bin2hex})
    Thermal_Camera_default_default_XC32_bin2hex_rule(Thermal_Camera_default_default_XC32_bin2hex)
    list(APPEND Thermal_Camera_default_library_list "$<TARGET_OBJECTS:Thermal_Camera_default_default_XC32_bin2hex>")

endif()


# Main target for this project
add_executable(Thermal_Camera_default_image_sTVxgZYV ${Thermal_Camera_default_library_list})

if(NOT CMAKE_HOST_WIN32)
    set_target_properties(Thermal_Camera_default_image_sTVxgZYV PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${Thermal_Camera_default_output_dir}")
endif()
set_target_properties(Thermal_Camera_default_image_sTVxgZYV PROPERTIES
    OUTPUT_NAME "default"
    SUFFIX ".elf")
target_link_libraries(Thermal_Camera_default_image_sTVxgZYV PRIVATE ${Thermal_Camera_default_default_XC32_FILE_TYPE_link})

# Add the link options from the rule file.
Thermal_Camera_default_link_rule( Thermal_Camera_default_image_sTVxgZYV)

# Call bin2hex function from the rule file
Thermal_Camera_default_bin2hex_rule(Thermal_Camera_default_image_sTVxgZYV)
if(CMAKE_HOST_WIN32)
    add_custom_command(
        TARGET Thermal_Camera_default_image_sTVxgZYV
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${Thermal_Camera_default_output_dir}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:Thermal_Camera_default_image_sTVxgZYV> ${Thermal_Camera_default_output_dir}/${Thermal_Camera_default_original_image_name}
        BYPRODUCTS ${Thermal_Camera_default_output_dir}/${Thermal_Camera_default_original_image_name}
        COMMENT "Copying elf to out location")
    set_property(
        TARGET Thermal_Camera_default_image_sTVxgZYV
        APPEND PROPERTY ADDITIONAL_CLEAN_FILES
        ${Thermal_Camera_default_output_dir}/${Thermal_Camera_default_original_image_name})
endif()

