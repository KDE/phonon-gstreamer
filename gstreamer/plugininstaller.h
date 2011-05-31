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

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <gst/gstcaps.h>
#include <gst/gstmessage.h>
#include <gst/pbutils/install-plugins.h>
#include "phonon-config-gstreamer.h"


QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

/**
 * A class to help with installing missing gstreamer plugins
 */

class PluginInstaller : public QObject {
    Q_OBJECT
    public: 
        enum PluginType {
            Source,
            Sink,
            Decoder,
            Encoder,
            Element,
            Codec
        };

        enum InstallStatus {
            Idle,
            Installed,
            Installing,
            Missing
        };

        PluginInstaller(QObject *parent = 0);

        void addPlugin(const QString &name, PluginType type);
        void addPlugin(GstMessage *msg);

        InstallStatus checkInstalledPlugins();
#ifdef PLUGIN_INSTALL_API
        static void pluginInstallationDone(GstInstallPluginsReturn result, gpointer data);
        void pluginInstallationResult(GstInstallPluginsReturn result);
        void run();
#endif
        void reset();

        /**
         * Returns the translated, user-friendly string that describes a plugin
         */
        static QString description(const gchar *name, PluginType type);
        static QString description(const GstCaps *caps, PluginType type);

        static QString getCapType(const GstCaps *caps);

        /**
         * Builds a string suitable for passing to gst_install_plugins_*
         */
        static QString buildInstallationString(const gchar *name, PluginType type);
        static QString buildInstallationString(const GstCaps *caps, PluginType type);

    Q_SIGNALS:
        void started();
        void success();
        void failure(const QString &message);

    private:
        QHash<QString, PluginType> m_pluginList;
        QList<QString> m_descList;
        InstallStatus m_state;
        static bool init();
        static bool s_ready;
};

} // ns Gstreamer
} // ns Phonon 

QT_END_NAMESPACE

#endif // Phonon_GSTREAMER_PLUGININSTALLER_H
