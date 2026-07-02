# Linux Cross-Compilation Toolchain File
# Target: x86_64-unknown-linux-gnu or arm64-unknown-linux-gnu

set(CMAKE_SYSTEM_NAME Linux)

# Default to x86_64 if not specified
if(NOT DEFINED CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(LINUX_TARGET_TRIPLE aarch64-unknown-linux-gnu)
else()
    set(LINUX_TARGET_TRIPLE x86_64-unknown-linux-gnu)
endif()

message(STATUS "[Linux Toolchain] Target Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "[Linux Toolchain] Target Triple: ${LINUX_TARGET_TRIPLE}")

# Search for clang (since clang supports cross-compilation out of the box)
find_program(CLANG_COMPILER clang)
find_program(CLANGXX_COMPILER clang++)

if(CLANG_COMPILER AND CLANGXX_COMPILER)
    set(CMAKE_C_COMPILER ${CLANG_COMPILER})
    set(CMAKE_C_COMPILER_TARGET ${LINUX_TARGET_TRIPLE})
    set(CMAKE_CXX_COMPILER ${CLANGXX_COMPILER})
    set(CMAKE_CXX_COMPILER_TARGET ${LINUX_TARGET_TRIPLE})
    message(STATUS "[Linux Toolchain] Using Clang cross-compiler")

    # Find matching LLVM utilities next to clang
    get_filename_component(COMPILER_DIR "${CLANG_COMPILER}" DIRECTORY)
    find_program(LLVM_AR NAMES llvm-ar llvm-ar.exe HINTS "${COMPILER_DIR}")
    if(LLVM_AR)
        set(CMAKE_AR ${LLVM_AR} CACHE FILEPATH "Archiver")
        message(STATUS "[Linux Toolchain] Found archiver: ${CMAKE_AR}")
    endif()
    find_program(LLVM_RANLIB NAMES llvm-ranlib llvm-ranlib.exe HINTS "${COMPILER_DIR}")
    if(LLVM_RANLIB)
        set(CMAKE_RANLIB ${LLVM_RANLIB} CACHE FILEPATH "Ranlib")
        message(STATUS "[Linux Toolchain] Found ranlib: ${CMAKE_RANLIB}")
    endif()
else()
    # Fallback to GCC cross-compiler names
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
        set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
    else()
        set(CMAKE_C_COMPILER x86_64-linux-gnu-gcc)
        set(CMAKE_CXX_COMPILER x86_64-linux-gnu-g++)
    endif()
    message(STATUS "[Linux Toolchain] Clang not found, using GCC fallback: ${CMAKE_CXX_COMPILER}")
endif()

# Sysroot configuration (crucial for finding Linux headers/libs)
if(DEFINED ENV{LINUX_SYSROOT})
    set(CMAKE_SYSROOT $ENV{LINUX_SYSROOT})
    message(STATUS "[Linux Toolchain] Using sysroot: ${CMAKE_SYSROOT}")
else()
    message(WARNING "[Linux Toolchain] LINUX_SYSROOT environment variable not set. Header lookups might fail.")
endif()

set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Bypass linker check since we don't have a sysroot on the host machine
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
