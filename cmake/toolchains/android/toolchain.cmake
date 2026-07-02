# Android Cross-Compilation Toolchain File
# This file sets up Android configuration. If ANDROID_NDK is defined in environment or CMake cache,
# it delegates to the official Android NDK CMake toolchain.

set(CMAKE_SYSTEM_NAME Android)

# Default Android API level if not specified
if(NOT DEFINED CMAKE_SYSTEM_VERSION)
    set(CMAKE_SYSTEM_VERSION 21)
endif()

# Search for Android NDK
if(DEFINED ENV{ANDROID_NDK_ROOT})
    set(ANDROID_NDK $ENV{ANDROID_NDK_ROOT} CACHE PATH "Path to Android NDK")
elseif(DEFINED ENV{ANDROID_NDK})
    set(ANDROID_NDK $ENV{ANDROID_NDK} CACHE PATH "Path to Android NDK")
endif()

if(ANDROID_NDK)
    message(STATUS "[Android Toolchain] Using NDK from: ${ANDROID_NDK}")
    # Forward directly to official NDK toolchain
    include("${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
else()
    # Basic fallbacks in case NDK is not installed yet (useful for initial generation tests)
    message(WARNING "[Android Toolchain] ANDROID_NDK not found. Setting basic CMake defaults.")
    
    if(NOT DEFINED CMAKE_ANDROID_ARCH_ABI)
        set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
    endif()
    
    set(CMAKE_ANDROID_API ${CMAKE_SYSTEM_VERSION})
    set(CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION clang)
    
    # Avoid compiler test failures when NDK is missing
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()
