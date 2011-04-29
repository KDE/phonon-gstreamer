# Copyright (C) 2009 Nokia Corporation. All rights reserved.
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

find_package(GStreamer REQUIRED)
macro_log_feature(GSTREAMER_FOUND "GStreamer" "gstreamer 0.10 is required for the multimedia backend" "http://gstreamer.freedesktop.org/modules/" TRUE "0.10")

find_package(GStreamerPlugins REQUIRED)
macro_log_feature(GSTREAMER_PLUGIN_VIDEO_FOUND "GStreamer video plugin" "The gstreamer video plugin (part of gstreamer-plugins-base 0.10) is required for the multimedia gstreamer backend" "http://gstreamer.freedesktop.org/modules/" TRUE "0.10")
macro_log_feature(GSTREAMER_PLUGIN_AUDIO_FOUND "GStreamer audio plugin" "The gstreamer audio plugin (part of gstreamer-plugins-base 0.10) is required for the multimedia gstreamer backend" "http://gstreamer.freedesktop.org/modules/" TRUE "0.10")
macro_log_feature(GSTREAMER_PLUGIN_PBUTILS_FOUND "GStreamer pbutils plugin" "The gstreamer pbutils plugin (part of gstreamer-plugins-base 0.10) is required for the multimedia gstreamer backend" "http://gstreamer.freedesktop.org/modules/" TRUE "0.10")

find_package(GLIB2 REQUIRED)
macro_log_feature(GLIB2_FOUND "GLib2" "GLib 2 is required to compile the gstreamer backend for Phonon" "http://www.gtk.org/download/" TRUE)

find_package(GObject REQUIRED)
# No log, since GObject is bundled with GLib

find_package(LibXml2 REQUIRED)
macro_log_feature(LIBXML2_FOUND "LibXml2" "LibXml2 is required to compile the gstreamer backend for Phonon" "http://xmlsoft.org/downloads.html" TRUE)

find_package(OpenGL)
macro_log_feature(OPENGL_FOUND "OpenGL" "OpenGL support is required to compile the gstreamer backend for Phonon" "" FALSE)

if (GSTREAMER_FOUND AND GSTREAMER_PLUGIN_VIDEO_FOUND AND GSTREAMER_PLUGIN_AUDIO_FOUND AND GSTREAMER_PLUGIN_PBUTILS_FOUND AND GLIB2_FOUND AND GOBJECT_FOUND AND LIBXML2_FOUND)
   set(BUILD_PHONON_GSTREAMER TRUE)
else (GSTREAMER_FOUND AND GSTREAMER_PLUGIN_VIDEO_FOUND AND GSTREAMER_PLUGIN_AUDIO_FOUND AND GSTREAMER_PLUGIN_PBUTILS_FOUND AND GLIB2_FOUND AND GOBJECT_FOUND AND LIBXML2_FOUND)
   set(BUILD_PHONON_GSTREAMER FALSE)
endif (GSTREAMER_FOUND AND GSTREAMER_PLUGIN_VIDEO_FOUND AND GSTREAMER_PLUGIN_AUDIO_FOUND AND GSTREAMER_PLUGIN_PBUTILS_FOUND AND GLIB2_FOUND AND GOBJECT_FOUND AND LIBXML2_FOUND)
