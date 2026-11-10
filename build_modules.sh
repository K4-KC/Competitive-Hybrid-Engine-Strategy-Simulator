#!/bin/bash

# ==============================================================================
# Godot C++ Module Multi-Target Cross-Compilation Script
# Designed for use in WSL (Ubuntu/Linux) to build Windows, Linux, and macOS binaries.
#
# Interactive mode allows selecting specific platforms and build types.
#
# NOTES:
# - Windows builds require MinGW-w64: 'sudo apt install mingw-w64'
# - macOS builds require OSXCross toolchain (optional, will skip if not found)
# ==============================================================================

# Determine the number of available CPU cores for parallel compilation (speed boost)
CORES=$(nproc)

# Interactive platform selection
echo "===================================================================="
echo "Godot C++ Module Multi-Platform Builder"
echo "===================================================================="
echo ""
echo "Select platforms to build (press Enter for all):"
echo "  1 = Windows"
echo "  2 = Linux"
echo "  3 = macOS"
echo ""
read -p "Your choice (can be combined): " PLATFORM_CHOICE

# Default to all platforms if empty
if [ -z "$PLATFORM_CHOICE" ]; then
    PLATFORM_CHOICE="123"
fi

# Parse platform selection
BUILD_WINDOWS=false
BUILD_LINUX=false
BUILD_MACOS=false

if [[ $PLATFORM_CHOICE == *"1"* ]]; then
    BUILD_WINDOWS=true
fi

if [[ $PLATFORM_CHOICE == *"2"* ]]; then
    BUILD_LINUX=true
fi

if [[ $PLATFORM_CHOICE == *"3"* ]]; then
    BUILD_MACOS=true
fi

# Interactive build type selection
echo ""
echo "Select build type (press Enter for all):"
echo "  1 = Debug"
echo "  2 = Release"
echo ""
read -p "Your choice (can be combined): " BUILD_TYPE_CHOICE

# Default to both if empty
if [ -z "$BUILD_TYPE_CHOICE" ]; then
    BUILD_TYPE_CHOICE="12"
fi

# Parse build type selection
BUILD_DEBUG=false
BUILD_RELEASE=false

if [[ $BUILD_TYPE_CHOICE == *"1"* ]]; then
    BUILD_DEBUG=true
fi

if [[ $BUILD_TYPE_CHOICE == *"2"* ]]; then
    BUILD_RELEASE=true
fi

# Check if OSXCross is available
OSXCROSS_AVAILABLE=false
if [ -z "$OSXCROSS_ROOT" ]; then
    export OSXCROSS_ROOT="/opt/osxcross"
fi

if [ -f "$OSXCROSS_ROOT/target/bin/x86_64-apple-darwin25.1-clang++" ] || \
   [ -f "$OSXCROSS_ROOT/target/bin/arm64-apple-darwin25.1-clang++" ]; then
    OSXCROSS_AVAILABLE=true
fi

# Disable macOS build if OSXCross is not available
if [ "$BUILD_MACOS" = true ] && [ "$OSXCROSS_AVAILABLE" = false ]; then
    echo ""
    echo "Warning: macOS build selected but OSXCross not found. Skipping macOS builds."
    BUILD_MACOS=false
fi

# Calculate total build count
BUILD_COUNT=0
if [ "$BUILD_WINDOWS" = true ]; then
    if [ "$BUILD_DEBUG" = true ]; then ((BUILD_COUNT++)); fi
    if [ "$BUILD_RELEASE" = true ]; then ((BUILD_COUNT++)); fi
fi
if [ "$BUILD_LINUX" = true ]; then
    if [ "$BUILD_DEBUG" = true ]; then ((BUILD_COUNT++)); fi
    if [ "$BUILD_RELEASE" = true ]; then ((BUILD_COUNT++)); fi
fi
if [ "$BUILD_MACOS" = true ]; then
    if [ "$BUILD_DEBUG" = true ]; then ((BUILD_COUNT++)); fi
    if [ "$BUILD_RELEASE" = true ]; then ((BUILD_COUNT++)); fi
fi

# Display build summary
echo ""
echo "===================================================================="
echo "Build Configuration Summary"
echo "===================================================================="
echo "Platforms: $([ "$BUILD_WINDOWS" = true ] && echo -n "Windows ")$([ "$BUILD_LINUX" = true ] && echo -n "Linux ")$([ "$BUILD_MACOS" = true ] && echo -n "macOS ")"
echo "Build Types: $([ "$BUILD_DEBUG" = true ] && echo -n "Debug ")$([ "$BUILD_RELEASE" = true ] && echo -n "Release ")"
echo "Total artifacts: ${BUILD_COUNT}"
echo "CPU cores: ${CORES}"
echo "===================================================================="
echo ""

if [ "$BUILD_COUNT" -eq 0 ]; then
    echo "Error: No builds selected. Exiting."
    exit 1
fi

CURRENT_BUILD=0

# --- WINDOWS BUILDS ---
if [ "$BUILD_WINDOWS" = true ]; then
    if [ "$BUILD_DEBUG" = true ]; then
        ((CURRENT_BUILD++))
        echo ""
        echo "-> ${CURRENT_BUILD}/${BUILD_COUNT}: Building Windows | Target: template_debug"
        scons platform=windows target=template_debug use_mingw=yes -j"${CORES}"

        if [ $? -ne 0 ]; then
            echo "!! Error during Windows Debug build. Review output and correct errors."
            exit 1
        fi
    fi

    if [ "$BUILD_RELEASE" = true ]; then
        ((CURRENT_BUILD++))
        echo ""
        echo "-> ${CURRENT_BUILD}/${BUILD_COUNT}: Building Windows | Target: template_release"
        scons platform=windows target=template_release use_mingw=yes -j"${CORES}"

        if [ $? -ne 0 ]; then
            echo "!! Error during Windows Release build. Review output and correct errors."
            exit 1
        fi
    fi
fi

# --- LINUX BUILDS ---
if [ "$BUILD_LINUX" = true ]; then
    if [ "$BUILD_DEBUG" = true ]; then
        ((CURRENT_BUILD++))
        echo ""
        echo "-> ${CURRENT_BUILD}/${BUILD_COUNT}: Building Linux | Target: template_debug"
        scons platform=linux target=template_debug -j"${CORES}"

        if [ $? -ne 0 ]; then
            echo "!! Error during Linux Debug build. Review output and correct errors."
            exit 1
        fi
    fi

    if [ "$BUILD_RELEASE" = true ]; then
        ((CURRENT_BUILD++))
        echo ""
        echo "-> ${CURRENT_BUILD}/${BUILD_COUNT}: Building Linux | Target: template_release"
        scons platform=linux target=template_release -j"${CORES}"

        if [ $? -ne 0 ]; then
            echo "!! Error during Linux Release build. Review output and correct errors."
            exit 1
        fi
    fi
fi

# --- MACOS BUILDS ---
if [ "$BUILD_MACOS" = true ]; then
    if [ "$BUILD_DEBUG" = true ]; then
        ((CURRENT_BUILD++))
        echo ""
        echo "-> ${CURRENT_BUILD}/${BUILD_COUNT}: Building macOS | Target: template_debug"
        scons platform=macos target=template_debug osxcross_sdk=darwin25.1 -j"${CORES}"

        if [ $? -ne 0 ]; then
            echo "!! Error during macOS Debug build. Review output and correct errors."
            exit 1
        fi
    fi

    if [ "$BUILD_RELEASE" = true ]; then
        ((CURRENT_BUILD++))
        echo ""
        echo "-> ${CURRENT_BUILD}/${BUILD_COUNT}: Building macOS | Target: template_release"
        scons platform=macos target=template_release osxcross_sdk=darwin25.1 -j"${CORES}"

        if [ $? -ne 0 ]; then
            echo "!! Error during macOS Release build. Review output and correct errors."
            exit 1
        fi
    fi
fi

echo ""
echo "===================================================================="
echo "Build Complete!"
echo "===================================================================="
echo "Successfully compiled ${BUILD_COUNT} artifacts."
echo ""

# List built binaries
if [ "$BUILD_WINDOWS" = true ]; then
    echo "Windows binaries (.dll) compiled:"
    [ "$BUILD_DEBUG" = true ] && echo "  - Debug: C.H.E.S.S/bin/"
    [ "$BUILD_RELEASE" = true ] && echo "  - Release: C.H.E.S.S/bin/"
fi

if [ "$BUILD_LINUX" = true ]; then
    echo "Linux binaries (.so) compiled:"
    [ "$BUILD_DEBUG" = true ] && echo "  - Debug: C.H.E.S.S/bin/"
    [ "$BUILD_RELEASE" = true ] && echo "  - Release: C.H.E.S.S/bin/"
fi

if [ "$BUILD_MACOS" = true ]; then
    echo "macOS binaries (.dylib) compiled:"
    [ "$BUILD_DEBUG" = true ] && echo "  - Debug: C.H.E.S.S/bin/"
    [ "$BUILD_RELEASE" = true ] && echo "  - Release: C.H.E.S.S/bin/"
fi

echo ""
echo "All binaries are located in the 'C.H.E.S.S/bin/' directory."
echo "===================================================================="