/*  This file is part of the KDE project.

    Copyright (C) 2011 Trever Fischer <tdfischer@fedoraproject.org>

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2.1 or 3 of the License.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef Phonon_GSTREAMER_PLUGININSTALLER_H
#define Phonon_GSTREAMER_PLUGININSTALLER_H

#include "common.h"
#include <gst/gstcaps.h>

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

typedef void (*Ptr_gst_pb_utils_init)();
typedef gchar* (*Ptr_gst_pb_utils_get_source_description)(const gchar *);
typedef gchar* (*Ptr_gst_pb_utils_get_sink_description)(const gchar *);
typedef gchar* (*Ptr_gst_pb_utils_get_decoder_description)(const GstCaps *);
typedef gchar* (*Ptr_gst_pb_utils_get_encoder_description)(const GstCaps *);
typedef gchar* (*Ptr_gst_pb_utils_get_element_description)(const gchar *);
typedef gchar* (*Ptr_gst_pb_utils_get_codec_description)(const GstCaps *);

/**
 * A class to help with installing missing gstreamer plugins
 */

class PluginInstaller {
    public: 
        enum PluginType {
            Source,
            Sink,
            Decoder,
            Encoder,
            Element,
            Codec
        };

        /**
         * Returns the translated, user-friendly string that describes a plugin
         */
        static QString description(const gchar *name, PluginType type);
        static QString description(const GstCaps *caps, PluginType type);

        /**
         * Builds a string suitable for passing to gst_install_plugins_*
         */
        static QString buildInstallationString(const gchar *name, PluginType type);
        static QString buildInstallationString(const GstCaps *caps, PluginType type);

    private:
        static bool init();
        static bool s_ready;
        static Ptr_gst_pb_utils_init p_gst_pb_utils_init;
        static Ptr_gst_pb_utils_get_source_description p_gst_pb_utils_get_source_description;
        static Ptr_gst_pb_utils_get_sink_description p_gst_pb_utils_get_sink_description;
        static Ptr_gst_pb_utils_get_decoder_description p_gst_pb_utils_get_decoder_description;
        static Ptr_gst_pb_utils_get_encoder_description p_gst_pb_utils_get_encoder_description;
        static Ptr_gst_pb_utils_get_element_description p_gst_pb_utils_get_element_description;
        static Ptr_gst_pb_utils_get_codec_description p_gst_pb_utils_get_codec_description;
};

} // ns Gstreamer
} // ns Phonon 

QT_END_NAMESPACE

#endif // Phonon_GSTREAMER_PLUGININSTALLER_H
