cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    else()
        message(FATAL_ERROR "VITASDK is not set. source /usr/local/vitasdk/bin/vitasdk_env.sh or export VITASDK.")
    endif()
endif()

project(opensonic-vita C)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(VITA_APP_NAME "Open Sonic")
set(VITA_TITLEID  "OSNC00001")
set(VITA_VERSION  "01.00")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
add_compile_options(
    -Wall
    -Wno-unused-function
    -fno-strict-aliasing
    -ffast-math
)

add_definitions(
    -D__VITA__
)

include_directories(
    src
    src/core
)

file(GLOB_RECURSE OPENSONIC_SOURCES
    "src/core/*.c"
    "src/core/nanoparser/*.c"
    "src/entities/*.c"
    "src/entities/object_decorators/*.c"
    "src/entities/object_decorators/base/*.c"
    "src/entities/items/*.c"
    "src/entities/items/util/*.c"
    "src/scenes/*.c"
    "src/scenes/util/*.c"
    "src/main.c"
)

# desktop-only 2xSaI scaler and the Windows/Linux icon glue are not part
# of the Vita build; see video.c and vita/README.md for why.
list(FILTER OPENSONIC_SOURCES EXCLUDE REGEX ".*/2xsai/.*")
list(FILTER OPENSONIC_SOURCES EXCLUDE REGEX ".*/misc/.*")

add_executable(${PROJECT_NAME} ${OPENSONIC_SOURCES})

target_link_libraries(${PROJECT_NAME}
    vita2d
    png
    z
    m
    vorbisfile
    vorbis
    ogg
    SceDisplay_stub
    SceCtrl_stub
    SceTouch_stub
    SceAudio_stub
    SceAudiodec_stub
    SceSysmodule_stub
    SceGxm_stub
    SceCommonDialog_stub
    SceAppMgr_stub
    SceIofilemgr_stub
    ScePower_stub
    SceRtc_stub
    SceLibKernel_stub
    m
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE ${CMAKE_SOURCE_DIR}/vita/sce_sys sce_sys
         ${CMAKE_SOURCE_DIR}/data data
)
