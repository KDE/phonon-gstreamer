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

project(phonon-gstreamer)
include(ConfigureChecks.cmake)

if (BUILD_PHONON_GSTREAMER)
   include_directories(
          ${CMAKE_CURRENT_BINARY_DIR}
          ${GSTREAMER_INCLUDE_DIR}
          ${GSTREAMER_PLUGIN_VIDEO_INCLUDE_DIR}
          ${GSTREAMER_PLUGIN_AUDIO_INCLUDE_DIR}
          ${GSTREAMER_PLUGIN_PBUTILS_INCLUDE_DIR}
          ${GLIB2_INCLUDE_DIR}
          ${LIBXML2_INCLUDE_DIR}
          ${X11_X11_INCLUDE_PATH})
   add_definitions(-DPHONON_BACKEND_VERSION_4_2)

   # configure plugin api
   if(USE_INSTALL_PLUGIN)
      set(PLUGIN_INSTALL_API TRUE)
   endif(USE_INSTALL_PLUGIN)

   configure_file(phonon-config-gstreamer.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/phonon-config-gstreamer.h )
   configure_file(gstreamer.desktop.cmake   ${CMAKE_CURRENT_BINARY_DIR}/gstreamer.desktop)

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

   if(OPENGL_FOUND)
        list(APPEND phonon_gstreamer_SRCS glrenderer.cpp)
   endif(OPENGL_FOUND)

   if(NOT WIN32)
      set(phonon_gstreamer_SRCS
          ${phonon_gstreamer_SRCS}
          x11renderer.cpp)
   endif(NOT WIN32)

   automoc4_add_library(phonon_gstreamer MODULE ${phonon_gstreamer_SRCS})
   set_target_properties(phonon_gstreamer PROPERTIES PREFIX "")
   target_link_libraries(phonon_gstreamer
      ${QT_QTCORE_LIBRARY} ${QT_QTGUI_LIBRARY} ${PHONON_LIBRARY}
      ${GSTREAMER_LIBRARIES} ${GSTREAMER_BASE_LIBRARY} ${GSTREAMER_INTERFACE_LIBRARY}
      ${GSTREAMER_PLUGIN_VIDEO_LIBRARY} ${GSTREAMER_PLUGIN_AUDIO_LIBRARY} ${GSTREAMER_PLUGIN_PBUTILS_LIBRARY}
      ${GLIB2_LIBRARIES} ${GOBJECT_LIBRARIES} ${GSTREAMER_APP_LIBRARY})
   if(USE_INSTALL_PLUGIN)
       target_link_libraries(phonon_gstreamer ${GSTREAMER_PLUGIN_PBUTILS_LIBRARIES})
   endif(USE_INSTALL_PLUGIN)
   if(OPENGL_FOUND)
      target_link_libraries(phonon_gstreamer ${QT_QTOPENGL_LIBRARY} ${OPENGL_gl_LIBRARY})
   endif(OPENGL_FOUND)

   install(TARGETS phonon_gstreamer DESTINATION ${PLUGIN_INSTALL_DIR}/plugins/phonon_backend)
   install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gstreamer.desktop DESTINATION ${SERVICES_INSTALL_DIR}/phononbackends)

    add_subdirectory(icons)
endif (BUILD_PHONON_GSTREAMER)
