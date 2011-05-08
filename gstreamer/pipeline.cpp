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

#include "pipeline.h"
#include "mediaobject.h"
#include "backend.h"

QT_BEGIN_NAMESPACE
namespace Phonon
{
namespace Gstreamer
{

Pipeline::Pipeline(QObject *parent)
    : QObject(parent)
{
    m_pipeline = GST_PIPELINE(gst_pipeline_new(NULL));
    gst_object_ref(m_pipeline);
    gst_object_sink(m_pipeline);

    GstBus *bus = gst_pipeline_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, gst_bus_sync_signal_handler, NULL);
    g_signal_connect(bus, "sync-message::eos", G_CALLBACK(cb_eos), this);
}

Pipeline::~Pipeline()
{
    gst_element_set_state(GST_ELEMENT(m_pipeline), GST_STATE_NULL);
    gst_object_unref(m_pipeline);
}

GstElement *Pipeline::element() const
{
    return GST_ELEMENT(m_pipeline);
}

GstStateChangeReturn Pipeline::setState(GstState state)
{
    return gst_element_set_state(GST_ELEMENT(m_pipeline), state);
}

void Pipeline::writeToDot(MediaObject *media, const QString &type)
{
    GstBin *bin = GST_BIN(m_pipeline);
    media->backend()->logMessage(QString("Dumping %0.dot").arg(type), Backend::Debug, media);
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(bin, GST_DEBUG_GRAPH_SHOW_ALL, QString("phonon-%0").arg(type).toUtf8().constData());
}

bool Pipeline::queryDuration(GstFormat *format, gint64 *duration)
{
    return gst_element_query_duration(GST_ELEMENT(m_pipeline), format, duration);
}

GstState Pipeline::state() const
{
    GstState state;
    gst_element_get_state(GST_ELEMENT(m_pipeline), &state, NULL, 1000);
    return state;
}

gboolean Pipeline::cb_eos(GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus)
    MediaObject *that = static_cast<MediaObject*>(data);
    gst_mini_object_ref(GST_MINI_OBJECT_CAST(msg));
    QMetaObject::invokeMethod(that, "handleEOSMessage", Qt::QueuedConnection, Q_ARG(GstMessage*, msg));
    return true;
}

void Pipeline::handleEOSMessage(GstMessage *gstMessage)
{
    gst_mini_object_unref(GST_MINI_OBJECT_CAST(gstMessage));
    emit eos();
}

gboolean Pipeline::cb_warning(GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus)
    MediaObject *that = static_cast<MediaObject*>(data);
    gst_mini_object_ref(GST_MINI_OBJECT_CAST(msg));
    QMetaObject::invokeMethod(that, "handleWarningMessage", Qt::QueuedConnection, Q_ARG(GstMessage*, msg));
    return true;
}

void Pipeline::handleWarningMessage(GstMessage *gstMessage)
{
    gchar *debug;
    GError *err;
    gst_message_parse_warning(gstMessage, &err, &debug);
    QString msgString;
    msgString.sprintf("Warning: %s\nMessage:%s", debug, err->message);
    emit warning(msgString);
    g_free (debug);
    g_error_free (err);
    gst_mini_object_unref(GST_MINI_OBJECT_CAST(gstMessage));
}

gboolean Pipeline::cb_duration(GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus)
    MediaObject *that = static_cast<MediaObject*>(data);
    gst_mini_object_ref(GST_MINI_OBJECT_CAST(msg));
    QMetaObject::invokeMethod(that, "handleDurationMessage", Qt::QueuedConnection, Q_ARG(GstMessage*, msg));
    return true;
}

void Pipeline::handleDurationMessage(GstMessage *gstMessage)
{
    gst_mini_object_unref(GST_MINI_OBJECT_CAST(gstMessage));
    emit durationChanged();
}


}
};

#include "moc_pipeline.cpp"
