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
