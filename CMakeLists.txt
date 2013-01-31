project(PhononGStreamer)
cmake_minimum_required(VERSION 2.6.2 FATAL_ERROR)

option(USE_INSTALL_PLUGIN "Use GStreamer codec installation API" TRUE)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Phonon REQUIRED)
add_definitions(${QT_DEFINITIONS})
include_directories(${PHONON_INCLUDES} ${QT_INCLUDES})

set(PHONON_GST_MAJOR_VERSION "4")
set(PHONON_GST_MINOR_VERSION "6")
set(PHONON_GST_PATCH_VERSION "3")
set(PHONON_GST_VERSION "${PHONON_GST_MAJOR_VERSION}.${PHONON_GST_MINOR_VERSION}.${PHONON_GST_PATCH_VERSION}")
add_definitions(-DPHONON_GST_VERSION="${PHONON_GST_VERSION}")

add_subdirectory(gstreamer)

macro_display_feature_log()

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/phonon-backend-gstreamer-${PHONON_GST_VERSION}.tar.xz
    COMMAND
        git archive --prefix=phonon-backend-gstreamer-${PHONON_GST_VERSION}/ HEAD | xz > ${CMAKE_BINARY_DIR}/phonon-backend-gstreamer-${PHONON_GST_VERSION}.tar.xz
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(
    dist
    DEPENDS ${CMAKE_BINARY_DIR}/phonon-backend-gstreamer-${PHONON_GST_VERSION}.tar.xz
)
