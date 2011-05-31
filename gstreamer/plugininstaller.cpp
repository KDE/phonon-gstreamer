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
#include <gst/gst.h>
#include <QtCore/QCoreApplication>
#include <QtGui/QApplication>
#include <QtGui/QWidget>
#include <QtCore/QLibrary>
#include <QtCore/QPointer>
#include <QtCore/QMetaType>
#include <QtCore/QDebug>

#ifdef PLUGIN_INSTALL_API
#include <gst/pbutils/pbutils.h>
#endif

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{
bool PluginInstaller::s_ready = false;

bool PluginInstaller::init()
{
    if (!s_ready) {
#ifdef PLUGIN_INSTALL_API
        gst_pb_utils_init();
        s_ready = true;
#endif
    }
    return s_ready;
}

QString PluginInstaller::description(const GstCaps *caps, PluginType type)
{
#ifdef PLUGIN_INSTALL_API
    if (init()) {
        QString pluginStr;
        gchar *pluginDesc = 0;
        switch(type) {
            case Decoder:
                pluginDesc = gst_pb_utils_get_decoder_description(caps);
                break;
            case Encoder:
                pluginDesc = gst_pb_utils_get_encoder_description(caps);
                break;
            case Codec:
                pluginDesc = gst_pb_utils_get_codec_description(caps);
                break;
            default:
                return 0;
        }
        pluginStr = QString::fromUtf8(pluginDesc);
        g_free(pluginDesc);
        return pluginStr;
    }
#else
    Q_UNUSED(type)
    Q_UNUSED(caps)
#endif
    return getCapType(caps);
}

QString PluginInstaller::description(const gchar *name, PluginType type)
{
#ifdef PLUGIN_INSTALL_API
    if (init()) {
        QString pluginStr;
        gchar *pluginDesc = 0;
        switch(type) {
            case Source:
                pluginDesc = gst_pb_utils_get_source_description(name);
                break;
            case Sink:
                pluginDesc = gst_pb_utils_get_sink_description(name);
                break;
            case Element:
                pluginDesc = gst_pb_utils_get_element_description(name);
                break;
            default:
                return 0;
        }
        pluginStr = QString::fromUtf8(pluginDesc);
        g_free(pluginDesc);
        return pluginStr;
    }
#endif
    return name;
}

QString PluginInstaller::buildInstallationString(const GstCaps *caps, PluginType type)
{
    QString descType;
    switch(type) {
        case Codec:
        case Decoder:
            descType = "decoder";
            break;
        case Encoder:
            descType = "encoder";
            break;
        default:
            return QString();
    }

    return QString("gstreamer|0.10|%0|%1|%2-%3")
        .arg(qApp->applicationName())
        .arg(description(caps, type))
        .arg(descType)
        .arg(getCapType(caps));
}

QString PluginInstaller::buildInstallationString(const gchar *name, PluginType type)
{
    QString descType;
    switch(type) {
        case Element:
            descType = "element";
            break;
        default:
            return QString();
    }
    return QString("gstreamer|0.10|%0|%1|%2-%3")
        .arg(qApp->applicationName())
        .arg(description(name, type))
        .arg(descType)
        .arg(name);
}

PluginInstaller::PluginInstaller(QObject *parent)
    : QObject(parent)
    , m_state(Idle)
{
}

void PluginInstaller::addPlugin(const QString &name, PluginType type)
{
    m_pluginList.insert(name, type);
}

void PluginInstaller::addPlugin(GstMessage *gstMessage)
{
    gchar *details = gst_missing_plugin_message_get_installer_detail(gstMessage);
    m_descList << details;
    g_free(details);
}

#ifdef PLUGIN_INSTALL_API
void PluginInstaller::run()
{
    GstInstallPluginsContext *ctx = gst_install_plugins_context_new();
    QWidget *activeWindow = QApplication::activeWindow();
    if (activeWindow) {
        gst_install_plugins_context_set_xid(ctx, static_cast<int>(activeWindow->winId()));
    }
    gchar *details[m_pluginList.size()+m_descList.size()+1];
    int i = 0;
    foreach (QString plugin, m_pluginList.keys()) {
        details[i] = strdup(buildInstallationString(plugin.toLocal8Bit().data(), m_pluginList[plugin]).toLocal8Bit().data());
        i++;
    }
    foreach (QString desc, m_descList) {
        details[i] = strdup(desc.toLocal8Bit().data());
        i++;
    }
    details[i] = 0;

    GstInstallPluginsReturn status;
    status = gst_install_plugins_async(details, ctx, pluginInstallationDone, new QPointer<PluginInstaller>(this));
    gst_install_plugins_context_free(ctx);
    if (status != GST_INSTALL_PLUGINS_STARTED_OK) {
        if (status == GST_INSTALL_PLUGINS_HELPER_MISSING)
            emit failure(tr("Missing codec helper script assistant."));
        else
            emit failure(tr("Plugin codec installation failed."));
    } else {
        emit started();
    }
    while (i) {
        free(details[i--]);
    }
    reset();
}

void PluginInstaller::pluginInstallationDone(GstInstallPluginsReturn result, gpointer data)
{
    QPointer<PluginInstaller> *that = static_cast<QPointer<PluginInstaller>*>(data);
    if (*that) {
        qRegisterMetaType<GstInstallPluginsReturn>("GstInstallPluginsReturn");
        (*that)->pluginInstallationResult(result);
    }
}

void PluginInstaller::pluginInstallationResult(GstInstallPluginsReturn result)
{
    switch(result) {
        case GST_INSTALL_PLUGINS_INVALID:
            emit failure(tr("Phonon attempted to install an invalid codec name."));
            break;
        case GST_INSTALL_PLUGINS_CRASHED:
            emit failure(tr("The codec installer crashed."));
            break;
        case GST_INSTALL_PLUGINS_NOT_FOUND:
            emit failure(tr("The required codec could not be found for installation."));
            break;
        case GST_INSTALL_PLUGINS_ERROR:
            emit failure(tr("An unspecified error occurred during codec installation."));
            break;
        case GST_INSTALL_PLUGINS_PARTIAL_SUCCESS:
            emit failure(tr("Not all codecs could be installed."));
            break;
        case GST_INSTALL_PLUGINS_USER_ABORT:
            emit failure(tr("User aborted codec installation"));
            break;
        //These four should never ever be passed in.
        //If they have, gstreamer has probably imploded in on itself.
        case GST_INSTALL_PLUGINS_STARTED_OK:
        case GST_INSTALL_PLUGINS_INTERNAL_FAILURE:
        case GST_INSTALL_PLUGINS_HELPER_MISSING:
        case GST_INSTALL_PLUGINS_INSTALL_IN_PROGRESS:
        //But this one is OK.
        case GST_INSTALL_PLUGINS_SUCCESS:
            if (!gst_update_registry()) {
                emit failure(tr("Could not update plugin registry after update."));
            } else {
                emit success();
            }
            break;
    }
    m_state = Idle;
}
#endif

PluginInstaller::InstallStatus PluginInstaller::checkInstalledPlugins()
{
    if (m_state != Idle)
        return m_state;
    bool allFound = true;
    foreach (QString plugin, m_pluginList.keys()) {
        if (!gst_default_registry_check_feature_version(plugin.toLocal8Bit().data(), 0, 10, 0)) {
            allFound = false;
            break;
        }
    }
    if (!allFound || m_descList.size() > 0) {
#ifdef PLUGIN_INSTALL_API
        run();
        m_state = Installing;
        return Installing;
#else
        return Missing;
#endif
    } else {
        return Installed;
    }
}

QString PluginInstaller::getCapType(const GstCaps *caps)
{
    GstStructure *str = gst_caps_get_structure(caps, 0);
    gchar *strstr = gst_structure_to_string(str);
    QString capType = QString::fromUtf8(strstr);
    g_free(strstr);
    return capType;
}

void PluginInstaller::reset()
{
    m_descList.clear();
    m_pluginList.clear();
}

} // ns Gstreamer
} // ns Phonon

QT_END_NAMESPACE

#include "moc_plugininstaller.cpp"
