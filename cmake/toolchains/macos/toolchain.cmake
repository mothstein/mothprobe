# macOS Cross-Compilation Toolchain File
# Target: arm64 (Apple Silicon) or x86_64 (Intel Mac)

set(CMAKE_SYSTEM_NAME Darwin)

# Default to arm64 (Apple Silicon) if not specified
if(NOT DEFINED CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR arm64)
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(MACOS_TARGET_TRIPLE arm64-apple-darwin)
else()
    set(MACOS_TARGET_TRIPLE x86_64-apple-darwin)
endif()

message(STATUS "[macOS Toolchain] Target Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "[macOS Toolchain] Target Triple: ${MACOS_TARGET_TRIPLE}")

# Clang natively supports macOS cross compilation
find_program(CLANG_COMPILER clang)
find_program(CLANGXX_COMPILER clang++)

if(CLANG_COMPILER AND CLANGXX_COMPILER)
    set(CMAKE_C_COMPILER ${CLANG_COMPILER})
    set(CMAKE_C_COMPILER_TARGET ${MACOS_TARGET_TRIPLE})
    set(CMAKE_CXX_COMPILER ${CLANGXX_COMPILER})
    set(CMAKE_CXX_COMPILER_TARGET ${MACOS_TARGET_TRIPLE})
    message(STATUS "[macOS Toolchain] Using Clang cross-compiler")

    # Find matching LLVM utilities next to clang
    get_filename_component(COMPILER_DIR "${CLANG_COMPILER}" DIRECTORY)
    find_program(LLVM_AR NAMES llvm-ar llvm-ar.exe HINTS "${COMPILER_DIR}")
    if(LLVM_AR)
        set(CMAKE_AR ${LLVM_AR} CACHE FILEPATH "Archiver")
        message(STATUS "[macOS Toolchain] Found archiver: ${CMAKE_AR}")
    endif()
    find_program(LLVM_RANLIB NAMES llvm-ranlib llvm-ranlib.exe HINTS "${COMPILER_DIR}")
    if(LLVM_RANLIB)
        set(CMAKE_RANLIB ${LLVM_RANLIB} CACHE FILEPATH "Ranlib")
        message(STATUS "[macOS Toolchain] Found ranlib: ${CMAKE_RANLIB}")
    endif()
else()
    message(WARNING "[macOS Toolchain] Clang compilers not found in PATH.")
endif()

# Path to macOS SDK (Required for cross-compiling from non-macOS hosts)
if(DEFINED ENV{MACOS_SDK_PATH})
    set(CMAKE_OSX_SYSROOT $ENV{MACOS_SDK_PATH})
    set(CMAKE_SYSROOT $ENV{MACOS_SDK_PATH})
    message(STATUS "[macOS Toolchain] Using SDK from: ${CMAKE_SYSROOT}")
else()
    message(WARNING "[macOS Toolchain] MACOS_SDK_PATH environment variable not set. Compilation may fail without SDK.")
endif()

set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Bypass linker check since we don't have a macOS SDK on the host machine
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
