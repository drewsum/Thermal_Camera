# Include paths:
#   <root>        project sources use root-relative includes (e.g.
#                 "application/foo.h", "gui/lvgl/lvgl.h")
#   <root>/gui    where lv_conf.h lives; LVGL finds it via
#                 LV_CONF_INCLUDE_SIMPLE below
#   <root>/gui/lvgl  LVGL's own font sources #include "lvgl.h" expecting the
#                 library root on the path
#
# NOTE: this file must NOT list source files. The MPLAB VS Code extension
# regenerates cmake/Thermal_Camera/default/.generated/file.cmake by scanning
# the source tree, so it picks up gui/ and gui/lvgl/ on its own; adding them
# here as well would compile every LVGL translation unit twice and fail the
# link with duplicate symbols. The authoritative, git-tracked file list for
# this project's own sources is .vscode/Thermal_Camera.mplab.json.
set(THERMAL_CAMERA_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../gui/lvgl"
)

# LVGL configuration, applied to every translation unit because LVGL headers
# are reachable from application code (application/image_loader.c and
# application/image_saver.c both use the lodepng that ships inside LVGL):
#   LV_CONF_INCLUDE_SIMPLE       find lv_conf.h as <lv_conf.h> on the include
#                                path instead of at a fixed relative location
#   LODEPNG_NO_COMPILE_CPP       the C++ wrapper, on a C-only MIPS build
# LODEPNG_NO_COMPILE_ENCODER used to be set here -- program flash is only 2MB
# and nothing wrote PNGs. It came off when application/image_saver.c landed:
# the shutter button saves the captured thermal frame to the SD card as a PNG,
# so the deflate/encode half now has to be built.
# LODEPNG_NO_COMPILE_DISK is deliberately NOT set even though nothing here
# loads or stores a PNG through lodepng's own fopen() helpers (files come from
# FatFs via image_loader.c/image_saver.c): LVGL's lv_lodepng.c calls
# lodepng_load_file() unconditionally in its LV_IMAGE_SRC_FILE path, so
# disabling it would mean patching upstream to link.
# These are honoured by gui/lvgl/src/libs/lodepng/lodepng.h's own
# LODEPNG_NO_COMPILE_* guards.
set(THERMAL_CAMERA_COMPILE_DEFS
    "LV_CONF_INCLUDE_SIMPLE"
    "LODEPNG_NO_COMPILE_CPP"
)

target_include_directories(Thermal_Camera_default_default_XC32_compile PRIVATE ${THERMAL_CAMERA_INCLUDE_DIRS})
target_compile_definitions(Thermal_Camera_default_default_XC32_compile PRIVATE ${THERMAL_CAMERA_COMPILE_DEFS})
if (TARGET Thermal_Camera_default_default_XC32_compile_cpp)
    target_include_directories(Thermal_Camera_default_default_XC32_compile_cpp PRIVATE ${THERMAL_CAMERA_INCLUDE_DIRS})
    target_compile_definitions(Thermal_Camera_default_default_XC32_compile_cpp PRIVATE ${THERMAL_CAMERA_COMPILE_DEFS})
endif()

# Several headers define global storage (and one ISR prototype) directly
# rather than as an extern declaration, so multiple translation units emit
# the same strong symbol. The duplicates are identical copies of the same
# header text, so telling the linker to keep the first and ignore the rest
# is safe.
target_link_options(Thermal_Camera_default_image_sTVxgZYV PRIVATE "-Wl,--allow-multiple-definition")

# Heap size (bytes). Matches the old Thermal_Camera.X project's "heap-size" setting.
target_link_options(Thermal_Camera_default_image_sTVxgZYV PRIVATE "-Wl,--defsym=_min_heap_size=115200")
