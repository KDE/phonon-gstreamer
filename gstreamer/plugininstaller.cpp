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

#include "plugininstaller.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QLibrary>

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

Ptr_gst_pb_utils_init PluginInstaller::p_gst_pb_utils_init = 0;
Ptr_gst_pb_utils_get_source_description PluginInstaller::p_gst_pb_utils_get_source_description = 0;
Ptr_gst_pb_utils_get_sink_description PluginInstaller::p_gst_pb_utils_get_sink_description = 0;
Ptr_gst_pb_utils_get_decoder_description PluginInstaller::p_gst_pb_utils_get_decoder_description = 0;
Ptr_gst_pb_utils_get_encoder_description PluginInstaller::p_gst_pb_utils_get_encoder_description = 0;
Ptr_gst_pb_utils_get_element_description PluginInstaller::p_gst_pb_utils_get_element_description = 0;
Ptr_gst_pb_utils_get_codec_description PluginInstaller::p_gst_pb_utils_get_codec_description = 0;
bool PluginInstaller::s_ready = false;

bool PluginInstaller::init()
{
#ifndef QT_NO_LIBRARY
        if (!p_gst_pb_utils_init) {
            p_gst_pb_utils_init =  (Ptr_gst_pb_utils_init)QLibrary::resolve(QLatin1String("gstpbutils-0.10"), 0, "gst_pb_utils_init");
            p_gst_pb_utils_get_source_description =  (Ptr_gst_pb_utils_get_source_description)QLibrary::resolve(QLatin1String("gstpbutils-0.10"), 0, "gst_pb_utils_get_source_description");
            p_gst_pb_utils_get_sink_description =  (Ptr_gst_pb_utils_get_sink_description)QLibrary::resolve(QLatin1String("gstpbutils-0.10"), 0, "gst_pb_utils_get_sink_description");
            p_gst_pb_utils_get_decoder_description =  (Ptr_gst_pb_utils_get_decoder_description)QLibrary::resolve(QLatin1String("gstpbutils-0.10"), 0, "gst_pb_utils_get_encoder_description");
            p_gst_pb_utils_get_encoder_description =  (Ptr_gst_pb_utils_get_encoder_description)QLibrary::resolve(QLatin1String("gstpbutils-0.10"), 0, "gst_pb_utils_get_encoder_description");
            p_gst_pb_utils_get_element_description =  (Ptr_gst_pb_utils_get_element_description)QLibrary::resolve(QLatin1String("gstpbutils-0.10"), 0, "gst_pb_utils_get_element_description");
            p_gst_pb_utils_get_codec_description =  (Ptr_gst_pb_utils_get_codec_description)QLibrary::resolve(QLatin1String("gstpbutils-0.10"), 0, "gst_pb_utils_get_codec_description");
            if (p_gst_pb_utils_init
                && p_gst_pb_utils_get_source_description
                && p_gst_pb_utils_get_sink_description
                && p_gst_pb_utils_get_decoder_description
                && p_gst_pb_utils_get_encoder_description
                && p_gst_pb_utils_get_element_description
                && p_gst_pb_utils_get_codec_description) {
                p_gst_pb_utils_init();
                s_ready = true;
            }
        }
#endif
        return s_ready;
}

QString PluginInstaller::description(const GstCaps *caps, PluginType type)
{
    if (init()) {
        QString pluginStr;
        gchar *pluginDesc = NULL;
        switch(type) {
            case Decoder:
                pluginDesc = p_gst_pb_utils_get_decoder_description(caps);
                break;
            case Encoder:
                pluginDesc = p_gst_pb_utils_get_encoder_description(caps);
                break;
            case Codec:
                pluginDesc = p_gst_pb_utils_get_codec_description(caps);
                break;
            default:
                return 0;
        }
        pluginStr = QString::fromUtf8(pluginDesc);
        g_free (pluginDesc);
        return pluginStr;
    }
    GstStructure *str = gst_caps_get_structure (caps, 0);
    return QString::fromUtf8(gst_structure_get_name (str));;
}

QString PluginInstaller::description(const gchar *name, PluginType type)
{
    if (init()) {
        QString pluginStr;
        gchar *pluginDesc = NULL;
        switch(type) {
            case Source:
                pluginDesc = p_gst_pb_utils_get_source_description(name);
                break;
            case Sink:
                pluginDesc = p_gst_pb_utils_get_sink_description(name);
                break;
            case Element:
                pluginDesc = p_gst_pb_utils_get_element_description(name);
                break;
            default:
                return 0;
        }
        pluginStr = QString::fromUtf8(pluginDesc);
        g_free (pluginDesc);
        return pluginStr;
    }
    return name;
}

QString PluginInstaller::buildInstallationString(const GstCaps *caps, PluginType type)
{
    QString descType;
    switch(type) {
        case Decoder:
            descType = "decoder";
            break;
        case Encoder:
            descType = "encoder";
            break;
        case Codec:
            descType = "codec";
            break;
        default:
            return 0;
    }

    GstStructure *str = gst_caps_get_structure(caps, 0);
    return QString("gstreamer|0.10|%0|%1|%2-%3")
        .arg( qApp->applicationName() )
        .arg( description(caps, type) )
        .arg( descType )
        .arg( QString::fromUtf8(gst_structure_get_name(str)) );
}

QString PluginInstaller::buildInstallationString(const gchar *name, PluginType type)
{
    QString descType;
    switch(type) {
        case Element:
            descType = "element";
            break;
        default:
            return 0;
    }
    return QString("gstreamer|0.10|%0|%1|%2-%3")
        .arg( qApp->applicationName() )
        .arg( description(name, type) )
        .arg( descType )
        .arg( name );
}

} // ns Gstreamer
} // ns Phonon

QT_END_NAMESPACE
