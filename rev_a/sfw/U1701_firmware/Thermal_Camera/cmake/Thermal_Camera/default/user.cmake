# Project sources use root-relative includes (e.g. "application/foo.h"),
# so the project root must be on the include path.
target_include_directories(Thermal_Camera_default_default_XC32_compile PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
if (TARGET Thermal_Camera_default_default_XC32_compile_cpp)
    target_include_directories(Thermal_Camera_default_default_XC32_compile_cpp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
endif()

# Several headers define global storage (and one ISR prototype) directly
# rather than as an extern declaration, so multiple translation units emit
# the same strong symbol. The duplicates are identical copies of the same
# header text, so telling the linker to keep the first and ignore the rest
# is safe.
target_link_options(Thermal_Camera_default_image_sTVxgZYV PRIVATE "-Wl,--allow-multiple-definition")

# Heap size (bytes). Matches the old Thermal_Camera.X project's "heap-size" setting.
target_link_options(Thermal_Camera_default_image_sTVxgZYV PRIVATE "-Wl,--defsym=_min_heap_size=115200")

# ---------------------------------------------------------------------------
# GUI: vendored LVGL v9.3.0 + the application/gui port (see application/gui/).
#
# The LVGL and gui .c SOURCES are NOT listed here on purpose: the MPLAB
# tooling regenerates .generated/file.cmake by scanning the source tree, so it
# already picks up everything under application/gui automatically (adding them
# here too would double-compile and cause duplicate-symbol link errors). The
# vendored tree is kept clean of non-MIPS files (ARM .S SIMD, platform .cpp,
# GPU draw backends) precisely so that auto-scan only ever collects
# MIPS-compilable C.
#
# What DOES belong here (won't survive in file.cmake through a regeneration):
# the include paths LVGL needs and the lv_conf.h discovery define. These are
# applied to the C object library that file.cmake feeds
# (..._XC32_compile), so the auto-added LVGL sources compile with them.
# ---------------------------------------------------------------------------
set(GUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../application/gui")

# GUI_DIR so <lvgl/lvgl.h> and (via LV_CONF_INCLUDE_SIMPLE) "lv_conf.h" resolve;
# GUI_DIR/lvgl so any LVGL-internal <lvgl.h> resolves. (The project root is
# already added to this target near the top of this file, for glcd/glcd.h etc.)
target_include_directories(Thermal_Camera_default_default_XC32_compile PRIVATE
    "${GUI_DIR}"
    "${GUI_DIR}/lvgl")

# Tell LVGL to pull config via a plain #include "lv_conf.h" from the include
# path above, rather than its default ../../lv_conf.h relative lookup.
target_compile_definitions(Thermal_Camera_default_default_XC32_compile PRIVATE
    LV_CONF_INCLUDE_SIMPLE)
