project(PhononGStreamer)
cmake_minimum_required(VERSION 2.6.2 FATAL_ERROR)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Phonon REQUIRED)
add_definitions(${QT_DEFINITIONS})
include_directories(${QT_INCLUDES} ${PHONON_INCLUDES})

if (Q_WS_X11)
    add_subdirectory(gstreamer)
endif (Q_WS_X11)

macro_display_feature_log()
