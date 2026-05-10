#!/bin/bash

# ==============================================================================
# Godot C++ Module Native macOS Builder
# Run on macOS to build the GDExtension shared library for native macOS use.
#
# Output: C.H.E.S.S/bin/libkCC_modules.macos.template_{debug,release}.universal.so
#
# Requirements:
#   - scons         (brew install scons)
#   - Xcode CLT     (xcode-select --install)
#   - godot-cpp/    checked out at the repo root, matching your Godot version
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CORES=$(sysctl -n hw.ncpu)

# --- Preflight checks ---
if ! command -v scons >/dev/null 2>&1; then
    echo "Error: scons not found. Install with: brew install scons"
    exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Error: Xcode Command Line Tools not found. Install with: xcode-select --install"
    exit 1
fi

if [ ! -d "godot-cpp" ]; then
    echo "Error: godot-cpp/ directory not found at repo root."
    echo "Clone it (matching your Godot version, e.g. 4.5) with:"
    echo "  git clone -b 4.5 https://github.com/godotengine/godot-cpp.git"
    exit 1
fi

# --- Interactive build type selection ---
echo "===================================================================="
echo "Godot C++ Module Native macOS Builder"
echo "===================================================================="
echo ""
echo "Select build type (press Enter for both):"
echo "  1 = Debug"
echo "  2 = Release"
echo ""
read -p "Your choice (can be combined): " BUILD_TYPE_CHOICE

if [ -z "$BUILD_TYPE_CHOICE" ]; then
    BUILD_TYPE_CHOICE="12"
fi

BUILD_DEBUG=false
BUILD_RELEASE=false

[[ $BUILD_TYPE_CHOICE == *"1"* ]] && BUILD_DEBUG=true
[[ $BUILD_TYPE_CHOICE == *"2"* ]] && BUILD_RELEASE=true

BUILD_COUNT=0
[ "$BUILD_DEBUG"   = true ] && ((BUILD_COUNT++))
[ "$BUILD_RELEASE" = true ] && ((BUILD_COUNT++))

if [ "$BUILD_COUNT" -eq 0 ]; then
    echo "Error: No builds selected. Exiting."
    exit 1
fi

echo ""
echo "===================================================================="
echo "Build Configuration Summary"
echo "===================================================================="
echo "Platform:    macOS (native, universal)"
echo "Build types: $([ "$BUILD_DEBUG" = true ] && echo -n "Debug ")$([ "$BUILD_RELEASE" = true ] && echo -n "Release ")"
echo "Artifacts:   ${BUILD_COUNT}"
echo "CPU cores:   ${CORES}"
echo "===================================================================="
echo ""

CURRENT_BUILD=0

if [ "$BUILD_DEBUG" = true ]; then
    ((CURRENT_BUILD++))
    echo ""
    echo "-> ${CURRENT_BUILD}/${BUILD_COUNT}: Building macOS | Target: template_debug"
    scons platform=macos target=template_debug -j"${CORES}"
fi

if [ "$BUILD_RELEASE" = true ]; then
    ((CURRENT_BUILD++))
    echo ""
    echo "-> ${CURRENT_BUILD}/${BUILD_COUNT}: Building macOS | Target: template_release"
    scons platform=macos target=template_release -j"${CORES}"
fi

echo ""
echo "===================================================================="
echo "Build Complete!"
echo "===================================================================="
echo "Successfully compiled ${BUILD_COUNT} artifact(s)."
echo "Output: C.H.E.S.S/bin/"
ls -1 C.H.E.S.S/bin/libkCC_modules.macos.* 2>/dev/null || true
echo "===================================================================="
