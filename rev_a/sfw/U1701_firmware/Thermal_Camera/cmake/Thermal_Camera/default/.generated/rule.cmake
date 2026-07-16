# The following functions contains all the flags passed to the different build stages.

set(PACK_REPO_PATH "C:/Users/drewm/.mchp_packs" CACHE PATH "Path to the root of a pack repository.")

function(Thermal_Camera_default_default_XC32_assemble_rule target)
    set(options
        "-g"
        "${ASSEMBLER_PRE}"
        "-mprocessor=32MZ2064DAR176"
        "-Wa,--defsym=__MPLAB_BUILD=1${MP_EXTRA_AS_POST},--defsym=__MPLAB_DEBUG=1,--defsym=__DEBUG=1,--gdwarf-2,--defsym=__MPLAB_DEBUGGER_PK4=1"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MZ-DA_DFP/1.7.245")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__DEBUG=1"
        PRIVATE "__MPLAB_DEBUGGER_PK4=1")
endfunction()
function(Thermal_Camera_default_default_XC32_assembleWithPreprocess_rule target)
    set(options
        "-x"
        "assembler-with-cpp"
        "-g"
        "${MP_EXTRA_AS_PRE}"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MZ-DA_DFP/1.7.245"
        "-mprocessor=32MZ2064DAR176"
        "-Wa,--defsym=__MPLAB_BUILD=1${MP_EXTRA_AS_POST},--defsym=__MPLAB_DEBUG=1,--gdwarf-2,--defsym=__DEBUG=1,--defsym=__MPLAB_DEBUGGER_PK4=1")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__DEBUG=1"
        PRIVATE "__MPLAB_DEBUGGER_PK4=1"
        PRIVATE "XPRJ_default=default")
endfunction()
function(Thermal_Camera_default_default_XC32_compile_rule target)
    set(options
        "-g"
        "${CC_PRE}"
        "-x"
        "c"
        "-c"
        "-mprocessor=32MZ2064DAR176"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MZ-DA_DFP/1.7.245")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__DEBUG"
        PRIVATE "__MPLAB_DEBUGGER_PK4=1"
        PRIVATE "XPRJ_default=default")
endfunction()
function(Thermal_Camera_default_default_XC32_compile_cpp_rule target)
    set(options
        "-g"
        "${CC_PRE}"
        "-mprocessor=32MZ2064DAR176"
        "-frtti"
        "-fexceptions"
        "-fno-check-new"
        "-fenforce-eh-specs"
        "-fno-common"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MZ-DA_DFP/1.7.245")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__DEBUG"
        PRIVATE "__MPLAB_DEBUGGER_PK4=1"
        PRIVATE "XPRJ_default=default")
endfunction()
function(Thermal_Camera_default_dependentObject_rule target)
    set(options
        "-mprocessor=32MZ2064DAR176"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MZ-DA_DFP/1.7.245")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
endfunction()
function(Thermal_Camera_default_link_rule target)
    set(options
        "-g"
        "${MP_EXTRA_LD_PRE}"
        "-mdebugger"
        "-mprocessor=32MZ2064DAR176"
        "-mreserve=data@0x0:0x27f"
        "-Wl,--defsym=__MPLAB_BUILD=1${MP_EXTRA_LD_POST},--defsym=__MPLAB_DEBUG=1,--defsym=__DEBUG=1,--no-code-in-dinit,--no-dinit-in-serial-mem,-Map=mem.map,--report-mem,--memorysummary,memoryfile.xml"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MZ-DA_DFP/1.7.245")
    list(REMOVE_ITEM options "")
    target_link_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__MPLAB_DEBUGGER_PK4=1"
        PRIVATE "XPRJ_default=default")
endfunction()
function(Thermal_Camera_default_bin2hex_rule target)
    add_custom_target(
        Thermal_Camera_default_Bin2Hex ALL
        COMMAND ${MP_BIN2HEX} ${Thermal_Camera_default_image_name}
        WORKING_DIRECTORY ${Thermal_Camera_default_output_dir}
        BYPRODUCTS "${Thermal_Camera_default_output_dir}/${Thermal_Camera_default_image_base_name}.hex"
        COMMENT "Convert build file to .hex")
    add_dependencies(Thermal_Camera_default_Bin2Hex ${target})
endfunction()
