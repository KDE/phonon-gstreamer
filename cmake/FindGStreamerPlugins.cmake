# - Try to find GStreamer_Plugins
# Once done this will define
#
#  GSTREAMERPLUGINSBASE_FOUND - system has GStreamer_Plugins
#  GSTREAMERPLUGINSBASE_INCLUDE_DIRS - the GStreamer_Plugins include directories
#  GSTREAMERPLUGINSBASE_LIBRARIES - the libraries needed to use GStreamer_Plugins
#
# The following variables are set for each plugin PLUGINNAME:
#
#  GSTREAMER_PLUGIN_PLUGINNAME_FOUND - plugin is found
#  GSTREAMER_PLUGIN_PLUGINNAME_INCLUDE_DIR - plugin include directory
#  GSTREAMER_PLUGIN_PLUGINNAME_LIBRARY - the library needed to use plugin
#
#  (c)2009 Nokia Corporation
#  (c)2010 Yury G. Kudryashov <urkud@ya.ru>

FIND_PACKAGE(PkgConfig REQUIRED)

IF (NOT WIN32)
   # don't make this check required - otherwise you can't use macro_optional_find_package on this one
   PKG_CHECK_MODULES( PKG_GSTREAMER_PLUGINSBASE gstreamer-plugins-base-0.11 )
ENDIF (NOT WIN32)

MACRO(MACRO_FIND_GSTREAMER_PLUGIN _plugin _header)
   STRING(TOUPPER ${_plugin} _upper)
   IF (NOT WIN32)
      # don't make this check required - otherwise you can't use macro_optional_find_package on this one
      PKG_CHECK_MODULES( PKG_GSTREAMER_${_upper} gstreamer-${_plugin}-0.11 )
   ENDIF (NOT WIN32)

   FIND_LIBRARY(GSTREAMER_PLUGIN_${_upper}_LIBRARY NAMES gst${_plugin}-0.11
      PATHS
      ${PKG_GSTREAMER_PLUGINSBASE_LIBRARY_DIRS}
      ${PKG_GSTREAMER_${_upper}_LIBRARY_DIRS}
      )

   FIND_PATH(GSTREAMER_PLUGIN_${_upper}_INCLUDE_DIR
       NAMES gst/${_plugin}/${_header}
       PATHS
       ${PKG_GSTREAMER_PLUGINSBASE_INCLUDE_DIRS}
       ${PKG_GSTREAMER_${_upper}_INCLUDE_DIRS}
       )

   IF(GSTREAMER_PLUGIN_${_upper}_LIBRARY AND GSTREAMER_PLUGIN_${_upper}_INCLUDE_DIR)
      SET(GSTREAMER_PLUGIN_${_upper}_FOUND TRUE)
      LIST(APPEND GSTREAMERPLUGINSBASE_INCLUDE_DIRS GSTREAMER_${_upper}_INCLUDE_DIR)
      LIST(APPEND GSTREAMERPLUGINSBASE_LIBRARIES GSTREAMER_${_upper}_LIBRARY)
   ELSE(GSTREAMER_PLUGIN_${_upper}_LIBRARY AND GSTREAMER_PLUGIN_${_upper}_INCLUDE_DIR)
      MESSAGE(STATUS "Could not find ${_plugin} plugin")
      MESSAGE(STATUS "${_upper} library: ${GSTREAMER_${_upper}_LIBRARY}")
      MESSAGE(STATUS "${_upper} include dir: ${GSTREAMER_${_upper}_INCLUDE_DIR}")
      SET(GSTREAMER_PLUGIN_${_upper}_FOUND FALSE)
      SET(GSTREAMER_PLUGIN_${_upper}_LIBRARY GSTREAMER_${_upper}_LIBRARY-NOTFOUND)
      SET(GSTREAMER_PLUGIN_${_upper}_INCLUDE_DIR GSTREAMER_${_upper}_INCLUDE_DIR-NOTFOUND)
      SET(GSTREAMERPLUGINSBASE_FOUND FALSE)
   ENDIF(GSTREAMER_PLUGIN_${_upper}_LIBRARY AND GSTREAMER_PLUGIN_${_upper}_INCLUDE_DIR)

   MARK_AS_ADVANCED(GSTREAMER_PLUGIN_${_upper}_LIBRARY
      GSTREAMER_PLUGIN_${_upper}_INCLUDE_DIR)
ENDMACRO(MACRO_FIND_GSTREAMER_PLUGIN)

#
# Base plugins:
#  audio
#  cdda
#  netbuffer
#  pbutils
#  riff
#  rtp
#  rtsp
#  sdp
#  tag
#  video
#
# The gstinterfaces-0.11 library is found by FindGStreamer.cmake
#

SET(GSTREAMER_PLUGINSBASE_FOUND TRUE)
MACRO_FIND_GSTREAMER_PLUGIN(audio audio.h)
MACRO_FIND_GSTREAMER_PLUGIN(cdda gstcddabasesrc.h)
MACRO_FIND_GSTREAMER_PLUGIN(netbuffer gstnetbuffer.h)
MACRO_FIND_GSTREAMER_PLUGIN(pbutils pbutils.h)
MACRO_FIND_GSTREAMER_PLUGIN(riff riff-ids.h)
MACRO_FIND_GSTREAMER_PLUGIN(rtp gstrtpbuffer.h)
MACRO_FIND_GSTREAMER_PLUGIN(rtsp gstrtspdefs.h)
MACRO_FIND_GSTREAMER_PLUGIN(sdp gstsdp.h)
MACRO_FIND_GSTREAMER_PLUGIN(tag tag.h)
MACRO_FIND_GSTREAMER_PLUGIN(video video.h)

IF (GSTREAMERPLUGINSBASE_FOUND)
   LIST(REMOVE_DUPLICATES GSTREAMERPLUGINSBASE_LIBRARIES)
   LIST(REMOVE_DUPLICATES GSTREAMERPLUGINSBASE_INCLUDE_DIRS)
   IF (NOT GStreamer_Plugins_FIND_QUIETLY)
      MESSAGE(STATUS "Found GStreamer Plugins:
    ${GSTREAMER_PLUGIN_AUDIO_LIBRARIES}
    ${GSTREAMER_PLUGIN_CDDA_LIBRARIES}
    ${GSTREAMER_PLUGIN_NETBUFFER_LIBRARIES}
    ${GSTREAMER_PLUGIN_PBUTILS_LIBRARIES}
    ${GSTREAMER_PLUGIN_RIFF_LIBRARIES}
    ${GSTREAMER_PLUGIN_RTP_LIBRARIES}
    ${GSTREAMER_PLUGIN_RTSP_LIBRARIES}
    ${GSTREAMER_PLUGIN_SDP_LIBRARIES}
    ${GSTREAMER_PLUGIN_TAG_LIBRARIES}
    ${GSTREAMER_PLUGIN_VIDEO_LIBRARIES}")
   ENDIF (NOT GStreamer_Plugins_FIND_QUIETLY)
ELSE (GSTREAMERPLUGINSBASE_FOUND)
   SET(GSTREAMERPLUGINSBASE_LIBRARIES GSTREAMERPLUGINSBASE_LIBRARIES-NOTFOUND)
   SET(GSTREAMERPLUGINSBASE_INCLUDE_DIRS GSTREAMERPLUGINSBASE_INCLUDE_DIRS-NOTFOUND)
   IF (GStreamer_Plugins_FIND_REQUIRED)
      MESSAGE(SEND_ERROR "Could NOT find GStreamer Plugins")
   ENDIF (GStreamer_Plugins_FIND_REQUIRED)
ENDIF (GSTREAMERPLUGINSBASE_FOUND)
