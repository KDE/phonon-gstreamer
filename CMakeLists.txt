project(PhononGStreamer)
cmake_minimum_required(VERSION 2.6.2 FATAL_ERROR)

option(USE_INSTALL_PLUGIN "Use GStreamer codec installation API" TRUE)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Phonon REQUIRED)
add_definitions(${QT_DEFINITIONS})
include_directories(${QT_INCLUDES} ${PHONON_INCLUDES})

set(PHONON_GST_MAJOR_VERSION "4")
set(PHONON_GST_MINOR_VERSION "4")
set(PHONON_GST_PATCH_VERSION "4")
set(PHONON_GST_VERSION "${PHONON_GST_MAJOR_VERSION}.${PHONON_GST_MINOR_VERSION}.${PHONON_GST_PATCH_VERSION}")
add_definitions(-DPHONON_GST_VERSION="${PHONON_GST_VERSION}")

if (Q_WS_X11)
    add_subdirectory(gstreamer)
endif (Q_WS_X11)

macro_display_feature_log()
