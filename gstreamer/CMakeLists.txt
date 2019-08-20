# Copyright (C) 2009 Nokia Corporation. All rights reserved.
# Copyright (C) 2008 Matthias Kretz <kretz@kde.org>
#
# This library is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 2 or 3 of the License.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this library.  If not, see <http://www.gnu.org/licenses/>.

include_directories(
      ${CMAKE_CURRENT_BINARY_DIR}
      ${GSTREAMER_INCLUDE_DIR}
      ${GSTREAMER_PLUGIN_VIDEO_INCLUDE_DIR}
      ${GSTREAMER_PLUGIN_AUDIO_INCLUDE_DIR}
      ${GSTREAMER_PLUGIN_PBUTILS_INCLUDE_DIR}
      ${GLIB2_INCLUDE_DIR}
      ${LIBXML2_INCLUDE_DIR}
      ${X11_X11_INCLUDE_PATH})

if(${PHONON_VERSION} VERSION_GREATER "4.6.49") # Pre-release 4.7 starts at 4.6.50
    message(STATUS "Building against Phonon 4.7 API")
    set(BACKEND_VERSION_DEFINE -DPHONON_BACKEND_VERSION_4_7)
elseif(${PHONON_VERSION} VERSION_GREATER "4.3.99")
    message(STATUS "Building against Phonon 4.4 API")
    set(BACKEND_VERSION_DEFINE -DPHONON_BACKEND_VERSION_4_4)
elseif(${PHONON_VERSION} VERSION_GREATER "4.1.99")
    message(STATUS "Building against Phonon 4.2 API")
    set(BACKEND_VERSION_DEFINE -DPHONON_BACKEND_VERSION_4_2)
else()
    message(STATUS "Building against Phonon 4.0 API")
    set(BACKEND_VERSION_DEFINE)
endif()

add_definitions(${BACKEND_VERSION_DEFINE})
set(AUTOMOC_MOC_OPTIONS ${BACKEND_VERSION_DEFINE})

# configure plugin api
if(USE_INSTALL_PLUGIN)
  set(PLUGIN_INSTALL_API TRUE)
endif(USE_INSTALL_PLUGIN)

configure_file(phonon-config-gstreamer.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/phonon-config-gstreamer.h )

set(phonon_gstreamer_SRCS
  abstractrenderer.cpp
  audiodataoutput.cpp
  audioeffect.cpp
  audiooutput.cpp
  backend.cpp
  debug.cpp
  devicemanager.cpp
  effect.cpp
  effectmanager.cpp
  gsthelper.cpp
  medianode.cpp
  mediaobject.cpp
  pipeline.cpp
  plugininstaller.cpp
  qwidgetvideosink.cpp
  streamreader.cpp
  videowidget.cpp
  volumefadereffect.cpp
  widgetrenderer.cpp
  )

if(PHONON_FOUND_EXPERIMENTAL)
    add_definitions(-DPHONON_EXPERIMENTAL)
    list(APPEND phonon_gstreamer_SRCS videodataoutput.cpp)
endif(PHONON_FOUND_EXPERIMENTAL)

if (OPENGL_FOUND)
    list(APPEND phonon_gstreamer_SRCS glrenderer.cpp)
endif ()

set(BUILD_X11RENDERER TRUE)
if (PHONON_BUILD_PHONON4QT5)
    if (Qt5X11Extras_FOUND)
        set(BUILD_X11RENDERER TRUE)
    endif()
elseif (NOT WIN32)
    set(BUILD_X11RENDERER TRUE)
endif()

if (BUILD_X11RENDERER)
  set(phonon_gstreamer_SRCS
      ${phonon_gstreamer_SRCS}
      x11renderer.cpp)
  add_definitions(-DBUILD_X11RENDERER)
endif()

ecm_create_qm_loader(phonon_gstreamer_SRCS phonon_gstreamer_qt)

add_library(phonon_gstreamer MODULE ${phonon_gstreamer_SRCS})
set_target_properties(phonon_gstreamer PROPERTIES
 PREFIX ""
 AUTOMOC_MOC_OPTIONS ${AUTOMOC_MOC_OPTIONS}
)

target_link_libraries(phonon_gstreamer
    Qt5::Core
    Qt5::Widgets
    Phonon::phonon4qt5
    ${PHONON_LIBRARY}
    ${GSTREAMER_LIBRARIES} ${GSTREAMER_BASE_LIBRARY} ${GSTREAMER_INTERFACE_LIBRARY}
    ${GSTREAMER_PLUGIN_VIDEO_LIBRARY} ${GSTREAMER_PLUGIN_AUDIO_LIBRARY} ${GSTREAMER_PLUGIN_PBUTILS_LIBRARY}
    ${GLIB2_LIBRARIES} ${GOBJECT_LIBRARIES} ${GSTREAMER_APP_LIBRARY}
)

if(PHONON_FOUND_EXPERIMENTAL)
    target_link_libraries(phonon_gstreamer Phonon::phonon4qt5experimental)
endif()

if(Qt5X11Extras_FOUND)
  target_link_libraries(phonon_gstreamer Qt5::X11Extras)
endif(Qt5X11Extras_FOUND)

if(USE_INSTALL_PLUGIN)
   target_link_libraries(phonon_gstreamer ${GSTREAMER_PLUGIN_PBUTILS_LIBRARIES})
endif(USE_INSTALL_PLUGIN)
if(OPENGL_FOUND)
  target_link_libraries(phonon_gstreamer ${OPENGL_gl_LIBRARY} Qt5::OpenGL)
endif(OPENGL_FOUND)

install(TARGETS phonon_gstreamer DESTINATION ${PHONON_BACKEND_DIR})

# Instead of desktop files we are embedding the information into the plugin itself.
# We have no KDE technology to help with finding the actual libraries anyway, so
# we need to have the library path anyway.
# Also see qtplugin/Q_PLUGIN_METADATA documentation.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/phonon-gstreamer.json.in
                ${CMAKE_CURRENT_BINARY_DIR}/phonon-gstreamer.json @ONLY)

add_subdirectory(icons)
