cmake_minimum_required(VERSION 3.15)

project(Mirrorball
    VERSION 0.1.0
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Universal build by default so the plugin loads in both native Apple Silicon
# and Intel builds of Resolume. Override with -DCMAKE_OSX_ARCHITECTURES=arm64
# for a faster single-arch dev build.
#
# This must sit immediately after project() and before any target is defined.
# CMake latches the architecture list when the first target is created, and
# setting it later is silently ignored -- you get an arm64-only binary that the
# build log calls a success. Verify with `lipo`, never with the log.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple deprecated the whole OpenGL API in 10.14 and Resolume's plugin interface
# is still OpenGL, so every SDK call warns. Silencing it is not ignoring a
# problem -- there is no alternative API to move to here.
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX)
endif()

option(MIRRORBALL_BUILD_TOOLS "Build the offline render and measurement harness" ON)

# ---------------------------------------------------------------------------
# The Resolume FFGL SDK, as a git submodule under external/.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
    message(FATAL_ERROR
        "FFGL SDK not found. Run: git submodule update --init --recursive")
endif()

set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Everything both plugins are, as an OBJECT library so both bundles and the
# offline harness are built from exactly the same code.
#
# OBJECT and not STATIC on purpose. CFFGLPluginInfo registers itself from a
# file-scope constructor and nothing ever references that object by name, so
# in an archive the linker is entitled to drop the whole translation unit --
# and the result is a bundle that loads, exports plugMain, and reports that it
# contains no plugins.
# ---------------------------------------------------------------------------
add_library(mirrorball_core OBJECT
    source/Diag.h
    source/Diag.cpp
    source/Controls.h
    source/Controls.cpp
    source/Ball.h
    source/Ball.cpp
    source/Motor.h
    source/Motor.cpp
    source/PassBuffer.h
    source/PassBuffer.cpp
    source/GLState.h
    source/Shaders.h
    source/Shaders.cpp
    source/Mirrorball.h
    source/Mirrorball.cpp
)

target_include_directories(mirrorball_core PUBLIC source)
target_link_libraries(mirrorball_core PUBLIC ffgl::sdk)
target_compile_definitions(mirrorball_core PUBLIC MIRRORBALL_VERSION="${PROJECT_VERSION}")
set_target_properties(mirrorball_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# On Windows/Linux the SDK headers we include pull in <GL/glew.h>. The SDK links
# GLEW privately, so consumers need it too for the include path (and for the
# GLEW_STATIC define the static vcpkg triplet propagates). macOS uses the system
# OpenGL framework and needs none of this.
if(NOT APPLE)
    find_package(GLEW REQUIRED)
    target_link_libraries(mirrorball_core PUBLIC GLEW::GLEW)
endif()

# ---------------------------------------------------------------------------
# The two plugins. FFGL resolves one plugMain per binary, so a source and an
# effect are two bundles -- not one bundle with two entries. SourcePlugin.cpp
# and EffectPlugin.cpp each carry their own CFFGLPluginInfo and are listed in
# their own MODULE target: in the shared library they would register both
# plugins into both bundles.
# ---------------------------------------------------------------------------
function(mirrorball_add_plugin target entry bundle_name identifier)
    add_library(${target} MODULE ${entry})
    target_link_libraries(${target} PRIVATE mirrorball_core)

    if(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "bundle"
            OUTPUT_NAME "${bundle_name}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${bundle_name}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${identifier}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        )
        # FFGL hosts resolve plugMain by symbol name, so keep default visibility.
        target_link_libraries(${target} PRIVATE
            "-framework OpenGL"
            "-framework Carbon"
            "-framework AppKit"
        )
    else()
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${bundle_name}")
    endif()
endfunction()

mirrorball_add_plugin(MirrorballSource source/SourcePlugin.cpp
    "Mirrorball" "com.stoatworks.ffgl.mirrorball")
mirrorball_add_plugin(MirrorballEffect source/EffectPlugin.cpp
    "Mirror Over" "com.stoatworks.ffgl.mirrorball.over")

# ---------------------------------------------------------------------------
# Offline harness. Whether a facet's spot lands where the law of reflection
# sends it, whether its size is the gate's or the pupil's cone, whether the
# flux it carries is conserved, and whether every ring turns at the motor's
# rate: measurements, not matters of taste -- so this is where the plugin is
# verified rather than merely previewed.
# ---------------------------------------------------------------------------
if(MIRRORBALL_BUILD_TOOLS AND APPLE)
    add_executable(mbtest tools/mbtest/main.cpp)
    # zlib ships with the OS, which is why the PNG writer is fifty lines here
    # rather than a vendored dependency.
    target_link_libraries(mbtest PRIVATE mirrorball_core z
        "-framework OpenGL"
        "-framework AppKit"
    )
endif()

# ---------------------------------------------------------------------------
# `cmake --install` drops the bundle straight into the user's Resolume plugin
# folder. Override with -DCMAKE_INSTALL_PREFIX=...
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # honour an explicit prefix
elseif(APPLE)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Documents/Resolume Arena/Extra Effects"
        CACHE PATH "" FORCE)
endif()

install(TARGETS MirrorballSource MirrorballEffect
    LIBRARY DESTINATION "."
    BUNDLE  DESTINATION "."
)
