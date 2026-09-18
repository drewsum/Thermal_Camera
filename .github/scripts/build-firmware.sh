#!/usr/bin/env bash
#
# Standalone CI build of the Thermal_Camera firmware.
#
# Locally this project is built by the MPLAB VS Code extension with CMake +
# Ninja, but that build cannot be reproduced on a runner: the extension
# regenerates cmake/Thermal_Camera/<conf>/.generated/ by scanning the machine
# it runs on, so those fragments hard-code "c:\Program Files\Microchip\xc32\..."
# toolchain paths and a Windows pack repository. They are (correctly) gitignored
# and so are not in a checkout at all.
#
# What *is* tracked, and is therefore what this script builds from:
#   .vscode/Thermal_Camera.mplab.json    device, DFP + XC32 versions, file set
#   cmake/Thermal_Camera/default/user.cmake
#                                        include dirs, compile definitions,
#                                        and the two extra link options
#
# Anything below that duplicates user.cmake is marked "mirrors user.cmake"; keep
# the two in step when that file changes.
#
# Required environment:
#   XC32_BIN   directory holding xc32-gcc / xc32-bin2hex
#   DFP_DIR    root of the unpacked PIC32MZ-DA device family pack
# Optional:
#   CONFIG     MPLAB configuration to build (default: "default")
#   BUILD_DIR  object directory      (default: _build/ci)
#   JOBS       parallel compile jobs (default: nproc)
#
# Run with the project directory (the one containing main.c) as the cwd.

set -euo pipefail

PROJECT_JSON=".vscode/Thermal_Camera.mplab.json"
USER_CMAKE_TEMPLATE="cmake/Thermal_Camera/%s/user.cmake"
CONFIG="${CONFIG:-default}"
BUILD_DIR="${BUILD_DIR:-_build/ci}"
JOBS="${JOBS:-$(nproc)}"

: "${XC32_BIN:?must point at the xc32 bin directory}"
: "${DFP_DIR:?must point at the unpacked device family pack}"

if [ ! -f "$PROJECT_JSON" ]; then
    echo "error: $PROJECT_JSON not found -- run this from the project directory" >&2
    exit 1
fi

# --- project description -----------------------------------------------------

conf_field() {
    jq -er --arg c "$CONFIG" \
        '.configurations[] | select(.name==$c) | .'"$1" "$PROJECT_JSON"
}

device=$(conf_field device)                 # e.g. PIC32MZ2064DAR176
image_path=$(conf_field imagePath)          # e.g. ./out/Thermal_Camera/default.elf

# xc32-gcc wants the part number without the "PIC" prefix, the way
# cmake/.../rule.cmake passes it: -mprocessor=32MZ2064DAR176
processor="${device#PIC}"

out_dir=$(dirname "${image_path#./}")
image_base=$(basename "$image_path" .elf)

# The file set carries a handful of duplicate entries (MPLAB tolerates them;
# compiling the same translation unit twice would not link), so de-duplicate
# while keeping project order.
mapfile -t sources < <(
    jq -er --arg c "$CONFIG" '
        (.configurations[] | select(.name==$c) | .fileSet) as $fs
        | .fileSets[] | select(.name==$fs) | .files[]
        | select(endswith(".c"))
    ' "$PROJECT_JSON" | awk '!seen[$0]++'
)

if [ "${#sources[@]}" -eq 0 ]; then
    echo "error: configuration '$CONFIG' lists no .c sources" >&2
    exit 1
fi

missing=0
for src in "${sources[@]}"; do
    if [ ! -f "$src" ]; then
        echo "error: $PROJECT_JSON lists '$src', which is not in the checkout" >&2
        missing=1
    fi
done
[ "$missing" -eq 0 ] || exit 1

# The heap size lives in user.cmake as a linker --defsym; read it back rather
# than keeping a second copy here that could silently drift.
user_cmake=$(printf "$USER_CMAKE_TEMPLATE" "$CONFIG")
heap_size=$(sed -n 's/.*_min_heap_size=\([0-9]\+\).*/\1/p' "$user_cmake" 2>/dev/null | tail -n1)
if [ -z "$heap_size" ]; then
    heap_size=115200
    echo "notice: no _min_heap_size found in $user_cmake, defaulting to $heap_size"
fi

echo "configuration : $CONFIG"
echo "device        : $device (-mprocessor=$processor)"
echo "sources       : ${#sources[@]} C files"
echo "heap size     : $heap_size bytes"
echo "image         : $out_dir/$image_base.elf"

# --- flags -------------------------------------------------------------------

# Include paths and compile definitions mirror user.cmake. The paths there are
# relative to cmake/Thermal_Camera/<conf>/; here the cwd is the project root, so
# "${CMAKE_CURRENT_SOURCE_DIR}/../../.." is simply ".".
includes=(-I. -Igui -Igui/lvgl)
defines=(-DLV_CONF_INCLUDE_SIMPLE -DLODEPNG_NO_COMPILE_CPP -DXPRJ_${CONFIG}=${CONFIG})

# CI builds the production variant of the flags MPLAB uses: same processor, DFP
# and code generation, minus the debugger support (-mdebugger, -mreserve, the
# __DEBUG/__MPLAB_DEBUGGER_PK4 defines) that only matters when a PICkit is
# attached. No source in this project tests __DEBUG, so the two are equivalent
# for the purpose of catching build breakage.
cflags=(-g -x c -c "-mprocessor=$processor" "-mdfp=$DFP_DIR" "${includes[@]}" "${defines[@]}")

ldflags=(
    -g
    "-mprocessor=$processor"
    "-mdfp=$DFP_DIR"
    # From the generated production link rule.
    "-Wl,--defsym=__MPLAB_BUILD=1,--no-code-in-dinit,--no-dinit-in-serial-mem,-Map=$out_dir/$image_base.map,--report-mem,--memorysummary,$out_dir/memoryfile.xml"
    # Both mirror user.cmake: several headers define global storage directly
    # rather than declaring it extern, so the same strong symbol is emitted by
    # more than one translation unit.
    "-Wl,--allow-multiple-definition"
    "-Wl,--defsym=_min_heap_size=$heap_size"
)

# --- compile -----------------------------------------------------------------

objects=()
for src in "${sources[@]}"; do
    objects+=("$BUILD_DIR/${src%.c}.o")
done

mkdir -p "$BUILD_DIR" "$out_dir"

# A generated makefile gets parallel compilation and first-error reporting for
# free, and leaves the exact failing command in the log.
makefile="$BUILD_DIR/ci.mk"
{
    echo "# Generated by .github/scripts/build-firmware.sh -- do not edit."
    printf 'CC := %q\n' "$XC32_BIN/xc32-gcc"
    printf 'CFLAGS :='
    printf ' %q' "${cflags[@]}"
    printf '\n'
    printf 'OBJS := %s\n\n' "${objects[*]}"
    echo ".PHONY: all"
    echo "all: \$(OBJS)"
    echo
    echo "$BUILD_DIR/%.o: %.c"
    printf '\t@mkdir -p "$(@D)"\n'
    printf '\t$(CC) $(CFLAGS) "$<" -o "$@"\n'
} > "$makefile"

echo "==> compiling ${#sources[@]} files with -j$JOBS"
make -f "$makefile" -j"$JOBS" all

# --- link --------------------------------------------------------------------

echo "==> linking $out_dir/$image_base.elf"
# Several hundred object paths on one command line gets close enough to the
# argument-length limit to be worth avoiding; xc32-gcc reads them from an
# @response file instead.
objects_rsp="$BUILD_DIR/objects.rsp"
printf '"%s"\n' "${objects[@]}" > "$objects_rsp"
"$XC32_BIN/xc32-gcc" "${ldflags[@]}" "@$objects_rsp" -o "$out_dir/$image_base.elf"

echo "==> generating $out_dir/$image_base.hex"
"$XC32_BIN/xc32-bin2hex" "$out_dir/$image_base.elf"

ls -l "$out_dir"
