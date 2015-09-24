cmake_minimum_required(VERSION 2.8.9)
project(PhononGStreamer)

option(USE_INSTALL_PLUGIN "Use GStreamer codec installation API" TRUE)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(PHONON_GST_MAJOR_VERSION "4")
set(PHONON_GST_MINOR_VERSION "8")
set(PHONON_GST_PATCH_VERSION "50")
set(PHONON_GST_VERSION "${PHONON_GST_MAJOR_VERSION}.${PHONON_GST_MINOR_VERSION}.${PHONON_GST_PATCH_VERSION}")

add_definitions(-DPHONON_GST_VERSION="${PHONON_GST_VERSION}")

find_package(Phonon REQUIRED)
macro_ensure_version("4.7.0" ${PHONON_VERSION} PHONON_FOUND)
macro_log_feature(PHONON_FOUND "Phonon" "Core Library" "git://anongit.kde.org/phonon.git" TRUE "4.7.0")

find_package(OpenGL)
macro_log_feature(OPENGL_FOUND "OpenGL" "OpenGL support is required to compile the gstreamer backend for Phonon" "" FALSE)

if(PHONON_BUILD_PHONON4QT5)
    find_package(Qt5OpenGL)
    macro_log_feature(Qt5OpenGL_FOUND "Qt5 OpenGL" "Qt5 OpenGL support is required to compile the gstreamer backend for Phonon" "" FALSE)
    set(QT_QTOPENGL_FOUND Qt5OpenGL_FOUND)
else ()
    macro_log_feature(QT_QTOPENGL_FOUND "Qt4 OpenGL" "Qt4 OpenGL support is required to compile the gstreamer backend for Phonon" "" FALSE)
endif()

if (NOT QT_QTOPENGL_FOUND)
    set(OPENGL_FOUND FALSE)
endif ()

add_definitions(${QT_DEFINITIONS})
include_directories(${PHONON_INCLUDES} ${QT_INCLUDES})

find_package(GStreamer REQUIRED)
find_package(GStreamerPlugins REQUIRED)
macro_log_feature(GSTREAMER_FOUND "GStreamer" "gstreamer 1.0 is required for the multimedia backend" "http://gstreamer.freedesktop.org/modules/" TRUE "1.0")
macro_log_feature(GSTREAMER_PLUGIN_VIDEO_FOUND "GStreamer video plugin" "The gstreamer video plugin (part of gstreamer-plugins-base 1.0) is required for the multimedia gstreamer backend" "http://gstreamer.freedesktop.org/modules/" TRUE "1.0")
macro_log_feature(GSTREAMER_PLUGIN_AUDIO_FOUND "GStreamer audio plugin" "The gstreamer audio plugin (part of gstreamer-plugins-base 1.0) is required for the multimedia gstreamer backend" "http://gstreamer.freedesktop.org/modules/" TRUE "1.0")
macro_log_feature(GSTREAMER_PLUGIN_PBUTILS_FOUND "GStreamer pbutils plugin" "The gstreamer pbutils plugin (part of gstreamer-plugins-base 1.0) is required for the multimedia gstreamer backend" "http://gstreamer.freedesktop.org/modules/" TRUE "1.0")

find_package(GLIB2 REQUIRED)
macro_log_feature(GLIB2_FOUND "GLib2" "GLib 2 is required to compile the gstreamer backend for Phonon" "http://www.gtk.org/download/" TRUE)

find_package(GObject REQUIRED)

find_package(LibXml2 REQUIRED)
macro_log_feature(LIBXML2_FOUND "LibXml2" "LibXml2 is required to compile the gstreamer backend for Phonon" "http://xmlsoft.org/downloads.html" TRUE)

set(REQUIRED_QT_VERSION 5.2.0)
macro_optional_find_package(Qt5X11Extras ${REQUIRED_QT_VERSION})
macro_log_feature(Qt5X11Extras_FOUND "Qt5X11Extras" "Qt5X11Extras is needed for the x11renderer" "http://doc.qt.io/qt-5/qtx11extras-index.html" FALSE "${REQUIRED_QT_VERSION}")

add_subdirectory(gstreamer)

macro_display_feature_log()
