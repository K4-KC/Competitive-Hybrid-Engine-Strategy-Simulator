#!/usr/bin/env python
import os
import sys

env = SConscript("godot-cpp/SConstruct")

# For reference:
# - CCFLAGS are compilation flags shared between C and C++
# - CFLAGS are for C-specific compilation flags
# - CXXFLAGS are for C++-specific compilation flags
# - CPPFLAGS are for pre-processor flags
# - CPPDEFINES are for pre-processor defines
# - LINKFLAGS are for linking flags

# Configure build directory to keep object files separate from source
build_dir = "build_cache/{}".format(env["platform"])
VariantDir(build_dir, "C.H.E.S.S/modules", duplicate=0)

# tweak this if you want to use different folders, or more folders, to store your source code in.
env.Append(CPPPATH=["C.H.E.S.S/modules/"])

# Automatically finds all .cpp files in src/ directory, but build in separate directory
sources = Glob("{}/*.cpp".format(build_dir))

# Changed library name from "NeuralNet" to "chess_ai" to reflect combined module
library = env.SharedLibrary(
    "C.H.E.S.S/bin/libkCC_modules{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
)

Default(library)
