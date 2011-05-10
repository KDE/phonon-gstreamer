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

#ifndef Phonon_GSTREAMER_PIPELINE_H
#define Phonon_GSTREAMER_PIPELINE_H

#include "plugininstaller.h"
#include <gst/gst.h>

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

class MediaObject;

class Pipeline : public QObject
{
    Q_OBJECT

    public:
        Pipeline(QObject *parent = 0);
        virtual ~Pipeline();
        GstElement *element() const;
        GstStateChangeReturn setState(GstState state);
        GstState state() const;
        void writeToDot(MediaObject *media, const QString &type);
        bool queryDuration(GstFormat *format, gint64 *duration);

        Q_INVOKABLE void handleEOSMessage(GstMessage *msg);
        static gboolean cb_eos(GstBus *bus, GstMessage *msg, gpointer data);

        static gboolean cb_warning(GstBus *bus, GstMessage *msg, gpointer data);
        Q_INVOKABLE void handleWarningMessage(GstMessage *msg);

        static gboolean cb_duration(GstBus *bus, GstMessage *msg, gpointer data);
        Q_INVOKABLE void handleDurationMessage(GstMessage *msg);

        static gboolean cb_buffering(GstBus *bus, GstMessage *msg, gpointer data);
        Q_INVOKABLE void handleBufferingMessage(GstMessage *msg);

    signals:
        void eos();
        void warning(const QString &message);
        void durationChanged();
        void buffering(int);

    private:
        GstPipeline *m_pipeline;
        int m_bufferPercent;

};

}
}

#endif // Phonon_GSTREAMER_PIPELINE_H
