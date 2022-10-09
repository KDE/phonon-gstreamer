cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(PhononGStreamer VERSION 4.10.0)

option(USE_INSTALL_PLUGIN "Use GStreamer codec installation API" TRUE)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FeatureSummary)

# ECM
find_package(ECM 5.90 NO_MODULE)
set(KDE_COMPILERSETTINGS_LEVEL "5.82")
set_package_properties(ECM PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "Extra CMake Modules"
    URL "https://api.kde.org/frameworks/extra-cmake-modules/html/index.html")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(ECMPoQmTools)
include(KDECMakeSettings)
include(KDECompilerSettings)
include(ECMSetupVersion)
include(ECMInstallIcons)

ecm_setup_version(PROJECT VARIABLE_PREFIX PHONON_GST)

add_definitions(-DPHONON_GST_VERSION="${PHONON_GST_VERSION}")

# Phonon
find_package(Phonon4Qt${QT_MAJOR_VERSION} 4.10.60 NO_MODULE)
set_package_properties(Phonon4Qt${QT_MAJOR_VERSION} PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "Phonon core library"
    URL "https://api.kde.org/phonon/html/index.html")

find_package(Phonon4Qt${QT_MAJOR_VERSION}Experimental 4.10.60 NO_MODULE)
set_package_properties(Phonon4Qt${QT_MAJOR_VERSION}Experimental PROPERTIES
    TYPE OPTIONAL
    DESCRIPTION "Phonon experimental library"
    URL "https://api.kde.org/phonon/html/index.html")
if(Phonon4Qt${QT_MAJOR_VERSION}Experimental_FOUND)
    set(PHONON_EXPERIMENTAL TRUE)
endif()

find_package(OpenGL)
set_package_properties(OpenGL PROPERTIES
    TYPE OPTIONAL
    PURPOSE "OpenGL support is required to compile the gstreamer backend for Phonon")

find_package(Qt${QT_MAJOR_VERSION}OpenGL)
set_package_properties(Qt${QT_MAJOR_VERSION}OpenGL PROPERTIES
    TYPE OPTIONAL
    PURPOSE "Qt${QT_MAJOR_VERSION} OpenGL support is required to compile the gstreamer backend for Phonon")
set(QT_QTOPENGL_FOUND Qt${QT_MAJOR_VERSION}OpenGL_FOUND)

set(REQUIRED_QT_VERSION 5.2.0)

if (QT_MAJOR_VERSION STREQUAL "5")
    find_package(Qt5X11Extras ${REQUIRED_QT_VERSION})
    set_package_properties(Qt5X11Extras PROPERTIES
        TYPE OPTIONAL
        PURPOSE "Qt5X11Extras is needed for the x11renderer"
        URL "http://doc.qt.io/qt-5/qtx11extras-index.html")
elseif (QT_MAJOR_VERSION STREQUAL "6")
    find_package(Qt${QT_MAJOR_VERSION} ${REQUIRED_QT_VERSION} CONFIG REQUIRED Gui) # qtx11extras_p.h
endif()

include(ECMPoQmTools)

if (NOT QT_QTOPENGL_FOUND)
    set(OPENGL_FOUND FALSE)
endif ()

include_directories(${PHONON_INCLUDES})

find_package(GStreamer 1.0)
set_package_properties(GSTREAMER PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "GStreamer"
    PURPOSE "gstreamer 1.0 is required for the multimedia backend"
    URL "http://gstreamer.freedesktop.org/modules/")
find_package(GStreamerPlugins QUIET 1.0)
# They way GStreamerPlugins works is super crappy form a cmake POV. To
# get it to play nice with FeatureSummary we'll look for a bunch of fake
# packages which are in fact set by GStreamerPlugins.
find_package(GSTREAMER_PLUGIN_VIDEO)
find_package(GSTREAMER_PLUGIN_AUDIO)
find_package(GSTREAMER_PLUGIN_PBUTILS)
set_package_properties(GSTREAMER_PLUGIN_VIDEO PROPERTIES
    TYPE RUNTIME
    DESCRIPTION "GStreamer video plugin"
    PURPOSE "The gstreamer video plugin (part of gstreamer-plugins-base 1.0) is required for the multimedia gstreamer backend"
    URL "http://gstreamer.freedesktop.org/modules/")
set_package_properties(GSTREAMER_PLUGIN_AUDIO PROPERTIES
    TYPE RUNTIME
    DESCRIPTION "GStreamer audio plugin"
    PURPOSE "The gstreamer video plugin (part of gstreamer-plugins-base 1.0) is required for the multimedia gstreamer backend"
    URL "http://gstreamer.freedesktop.org/modules/")
set_package_properties(GSTREAMER_PLUGIN_PBUTILS PROPERTIES
    TYPE RUNTIME
    DESCRIPTION "GStreamer pbutils plugin"
    PURPOSE "The gstreamer pbutils plugin (part of gstreamer-plugins-base 1.0) is required for the multimedia gstreamer backend"
    URL "http://gstreamer.freedesktop.org/modules/")

find_package(GLIB2)
set_package_properties(GLIB2 PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "GLib2"
    PURPOSE "GLib 2 is required to compile the gstreamer backend for Phonon"
    URL "http://www.gtk.org/download/")

find_package(GObject REQUIRED)

find_package(LibXml2 REQUIRED)
set_package_properties(LibXml2 PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "LibXml2"
    PURPOSE "LibXml2 is required to compile the gstreamer backend for Phonon"
    URL "http://xmlsoft.org/downloads.html")

add_subdirectory(gstreamer)

ecm_install_po_files_as_qm(poqm)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
