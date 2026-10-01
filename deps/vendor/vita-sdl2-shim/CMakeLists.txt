# HardRPG - SDL2 Vita shim build input
# Copyright (C) 2026 Asukate - GNU General Public License v3 (see LICENSES/GPL-3.0.txt at the repository root).
cmake_minimum_required(VERSION 3.10)
project(vita-sdl2 C)

set(VITASDK $ENV{VITASDK})
if(NOT VITASDK)
    message(FATAL_ERROR "Set VITASDK to the VitaSDK installation root")
endif()

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_C_COMPILER ${VITASDK}/bin/arm-vita-eabi-gcc)
    set(CMAKE_AR ${VITASDK}/bin/arm-vita-eabi-ar)
    set(CMAKE_RANLIB ${VITASDK}/bin/arm-vita-eabi-ranlib)
endif()

set(CMAKE_C_FLAGS "-marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -O2" CACHE STRING "")

set(SOURCES
    src/SDL_vita.c
    src/SDL_vita_stubs.c
)

add_library(SDL2 STATIC ${SOURCES})
target_include_directories(SDL2 PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SDL2
    ${VITA_DEP_PREFIX}/include
    ${VITA_DEP_PREFIX}/include/freetype2
    ${VITA_DEP_PREFIX}/include/libpng16
    ${VITASDK}/arm-vita-eabi/include
    ${VITASDK}/arm-vita-eabi/include/freetype2
    ${VITASDK}/arm-vita-eabi/include/libpng16
)
target_link_libraries(SDL2  
    ${VITASDK}/arm-vita-eabi/lib/libSceGxm_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceCtrl_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceAudio_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceSysmodule_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceDisplay_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceTouch_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceKernelThreadMgr_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceProcessmgr_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceAppMgr_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceCommonDialog_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceKernelDmacMgr_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceKernelModulemgr_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libSceShaccCg_stub.a
    ${VITASDK}/arm-vita-eabi/lib/libvitashark.a
    ${VITASDK}/arm-vita-eabi/lib/libvitaGL.a
    -lpthread -lm
)

# Install prefix for built artifacts. Standalone builds default to VitaSDK;
# the project bootstrap sets VITA_DEP_PREFIX to a workspace prefix.
set(VITA_DEP_PREFIX "${VITASDK}/arm-vita-eabi" CACHE PATH "install prefix for shim artifacts")

# Install
install(TARGETS SDL2 DESTINATION ${VITA_DEP_PREFIX}/lib)
install(DIRECTORY include/SDL2 DESTINATION ${VITA_DEP_PREFIX}/include)
install(DIRECTORY include/GLES2 DESTINATION ${VITA_DEP_PREFIX}/include)
install(DIRECTORY include/KHR DESTINATION ${VITA_DEP_PREFIX}/include)

# Generate pkg-config file
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/sdl2.pc "prefix=${VITA_DEP_PREFIX}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: sdl2
Description: SDL2 Vita Shim
Version: 2.30.0
Requires:
Libs: -L\${libdir} -lSDL2 -lvitaGL -lvitashark -lSceShaccCg_stub -lSceKernelModulemgr_stub -lSceKernelDmacMgr_stub -lSceCommonDialog_stub -lSceAppMgr_stub -lSceGxm_stub -lSceCtrl_stub -lSceAudio_stub -lSceSysmodule_stub -lSceDisplay_stub -lpthread -lm
Cflags: -I\${includedir} -I\${includedir}/SDL2
")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sdl2.pc DESTINATION ${VITA_DEP_PREFIX}/lib/pkgconfig)
