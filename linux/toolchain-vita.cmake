# CMake toolchain file for PS Vita cross-compilation
# Used by mkxp-z dependency builds that use CMake

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(VITASDK "$ENV{VITASDK}" CACHE PATH "VitaSDK installation")
if(NOT VITASDK)
    message(FATAL_ERROR "Set VITASDK to the VitaSDK installation root")
endif()

set(CMAKE_C_COMPILER ${VITASDK}/bin/arm-vita-eabi-gcc)
set(CMAKE_CXX_COMPILER ${VITASDK}/bin/arm-vita-eabi-g++)
set(CMAKE_AR ${VITASDK}/bin/arm-vita-eabi-ar)
set(CMAKE_RANLIB ${VITASDK}/bin/arm-vita-eabi-ranlib)
set(CMAKE_STRIP ${VITASDK}/bin/arm-vita-eabi-strip)

set(CMAKE_C_FLAGS "-marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -O3" CACHE STRING "C flags")
set(CMAKE_CXX_FLAGS "-marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -O3" CACHE STRING "C++ flags")
set(CMAKE_FIND_ROOT_PATH ${VITASDK}/arm-vita-eabi)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
