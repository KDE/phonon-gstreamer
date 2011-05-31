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
#include "plugininstaller.h"
#include <gst/pbutils/missing-plugins.h>
#define MAX_QUEUE_TIME 20 * GST_SECOND

QT_BEGIN_NAMESPACE
namespace Phonon
{
namespace Gstreamer
{

Pipeline::Pipeline(QObject *parent)
    : QObject(parent)
    , m_bufferPercent(0)
    , m_installer(new PluginInstaller(this))
{
    m_pipeline = GST_PIPELINE(gst_element_factory_make("playbin2", NULL));
    gst_object_ref(m_pipeline);
    gst_object_sink(m_pipeline);
    g_signal_connect(m_pipeline, "video-changed", G_CALLBACK(cb_videoChanged), this);

    GstBus *bus = gst_pipeline_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, gst_bus_sync_signal_handler, NULL);
    g_signal_connect(bus, "sync-message::eos", G_CALLBACK(cb_eos), this);
    g_signal_connect(bus, "sync-message::warning", G_CALLBACK(cb_warning), this);
    g_signal_connect(bus, "sync-message::duration", G_CALLBACK(cb_duration), this);
    g_signal_connect(bus, "sync-message::buffering", G_CALLBACK(cb_buffering), this);
    g_signal_connect(bus, "sync-message::state-changed", G_CALLBACK(cb_state), this);
    g_signal_connect(bus, "sync-message::element", G_CALLBACK(cb_element), this);
    g_signal_connect(bus, "sync-message::error", G_CALLBACK(cb_error), this);

    // Set up audio graph
    m_audioGraph = gst_bin_new("audioGraph");
    gst_object_ref (GST_OBJECT (m_audioGraph));
    gst_object_sink (GST_OBJECT (m_audioGraph));

    // Note that these queues are only required for streaming content
    // And should ideally be created on demand as they will disable
    // pull-mode access. Also note that the max-size-time are increased to
    // reduce buffer overruns as these are not gracefully handled at the moment.
    m_audioPipe = gst_element_factory_make("queue", "audioPipe");
    g_object_set(G_OBJECT(m_audioPipe), "max-size-time",  MAX_QUEUE_TIME, (const char*)NULL);

    QByteArray tegraEnv = qgetenv("TEGRA_GST_OPENMAX");
    if (!tegraEnv.isEmpty()) {
        g_object_set(G_OBJECT(m_audioPipe), "max-size-time", 0, (const char*)NULL);
        g_object_set(G_OBJECT(m_audioPipe), "max-size-buffers", 0, (const char*)NULL);
        g_object_set(G_OBJECT(m_audioPipe), "max-size-bytes", 0, (const char*)NULL);
    }

    gst_bin_add(GST_BIN(m_audioGraph), m_audioPipe);
    GstPad *audiopad = gst_element_get_pad (m_audioPipe, "sink");
    gst_element_add_pad (m_audioGraph, gst_ghost_pad_new ("sink", audiopad));
    gst_object_unref (audiopad);

    g_object_set(m_pipeline, "audio-sink", m_audioGraph, NULL);

    // Set up video graph
    m_videoGraph = gst_bin_new("videoGraph");
    gst_object_ref (GST_OBJECT (m_videoGraph));
    gst_object_sink (GST_OBJECT (m_videoGraph));

    m_videoPipe = gst_element_factory_make("queue", "videoPipe");
    gst_bin_add(GST_BIN(m_videoGraph), m_videoPipe);
    GstPad *videopad = gst_element_get_pad(m_videoPipe, "sink");
    gst_element_add_pad(m_videoGraph, gst_ghost_pad_new("sink", videopad));
    gst_object_unref(audiopad);

    g_object_set(m_pipeline, "video-sink", m_videoGraph, NULL);

    //FIXME: Put this stuff somewhere else, or at least document why its needed.
    if (!tegraEnv.isEmpty()) {
        //TODO: Move this line into the videooutput
        //g_object_set(G_OBJECT(videoQueue), "max-size-time", 33000, (const char*)NULL);
        g_object_set(G_OBJECT(m_audioPipe), "max-size-buffers", 1, (const char*)NULL);
        g_object_set(G_OBJECT(m_audioPipe), "max-size-bytes", 0, (const char*)NULL);
    }

    connect(m_installer, SIGNAL(failure(const QString&)), this, SLOT(pluginInstallFailure(const QString&)));
    connect(m_installer, SIGNAL(started()), this, SLOT(pluginInstallStarted()));
    connect(m_installer, SIGNAL(success()), this, SLOT(pluginInstallComplete()));
}

GstElement *Pipeline::audioPipe()
{
    return m_audioPipe;
}

GstElement *Pipeline::videoPipe()
{
    return m_videoPipe;
}

GstElement *Pipeline::audioGraph()
{
    return m_audioGraph;
}

GstElement *Pipeline::videoGraph()
{
    return m_videoGraph;
}

void Pipeline::setSource(const Phonon::MediaSource &source)
{
    m_installer->reset();
    m_resumeAfterInstall = false;

    qDebug() << source.mrl();
    QByteArray gstUri;
    switch(source.type()) {
        case MediaSource::Url:
        case MediaSource::LocalFile:
            gstUri = source.mrl().toEncoded();
            break;
        case MediaSource::Invalid:
            //TODO: Raise error
            return;
        case MediaSource::Stream:
            setStreamSource(source);
            gstUri = "phonon:/";
            return;
        case MediaSource::Disc:
            switch(source.discType()) {
                case Phonon::Cd:
                    gstUri = "cdda://";
                    break;
                case Phonon::Vcd:
                    gstUri = "vcd://";
                    break;
                case Phonon::Dvd:
                    gstUri = "dvd://";
                    break;
            }
            break;
    }
    g_object_set(m_pipeline, "uri", gstUri.constData(), NULL);

    //TODO: Test this to make sure that resuming playback after plugin installation
    //when using an abstract stream source doesn't explode.
    m_lastSource = source;
}

void Pipeline::setStreamSource(const Phonon::MediaSource &source)
{
    //FIXME: Implement GstUriHandler in phononsrc
#if 0
    GstElement *source;
    g_object_get(m_pipeline, "source", source, NULL);
    StreamReader *streamReader = new StreamReader(source, this);
    g_object_set (G_OBJECT (source), "iodevice", streamReader, (const char*)NULL);
#endif
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
    m_resumeAfterInstall = true;

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

gboolean Pipeline::cb_buffering(GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus)
    MediaObject *that = static_cast<MediaObject*>(data);
    gst_mini_object_ref(GST_MINI_OBJECT_CAST(msg));
    QMetaObject::invokeMethod(that, "handleBufferingMessage", Qt::QueuedConnection, Q_ARG(GstMessage*, msg));
    return true;
}

/**
 * Handles GST_MESSAGE_BUFFERING messages
 */
void Pipeline::handleBufferingMessage(GstMessage *gstMessage)
{
    gint percent = 0;
    gst_structure_get_int (gstMessage->structure, "buffer-percent", &percent); //gst_message_parse_buffering was introduced in 0.10.11

    if (m_bufferPercent != percent) {
        emit buffering(percent);
        m_bufferPercent = percent;
    }

    gst_mini_object_unref(GST_MINI_OBJECT_CAST(gstMessage));
}

gboolean Pipeline::cb_state(GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus)
    Pipeline *that = static_cast<Pipeline*>(data);
    gst_mini_object_ref(GST_MINI_OBJECT_CAST(msg));
    QMetaObject::invokeMethod(that, "handleStateMessage", Qt::QueuedConnection, Q_ARG(GstMessage*, msg));
    return true;
}

void Pipeline::handleStateMessage(GstMessage *gstMessage)
{
    GstState oldState;
    GstState newState;
    GstState pendingState;
    gst_message_parse_state_changed(gstMessage, &oldState, &newState, &pendingState);
    if (gstMessage->src != GST_OBJECT(m_pipeline)) {
        gst_mini_object_unref(GST_MINI_OBJECT_CAST(gstMessage));
        return;
    }

    if (newState == GST_STATE_READY) {
        m_installer->checkInstalledPlugins();
    }

    emit stateChanged(oldState, newState);
}

void Pipeline::cb_videoChanged(GstElement *playbin, gpointer data)
{
    gint videoCount;
    bool videoAvailable;
    Pipeline *that = static_cast<Pipeline*>(data);
    g_object_get(playbin, "n-video", &videoCount, NULL);
    // If there is at least one video stream, we've got video.
    videoAvailable = videoCount > 0;

    emit that->videoAvailabilityChanged(videoAvailable);
}

bool Pipeline::videoIsAvailable() const
{
    gint videoCount;
    g_object_get(m_pipeline, "n-video", &videoCount, NULL);
    return videoCount > 0;
}

bool Pipeline::audioIsAvailable() const
{
    gint audioCount;
    g_object_get(m_pipeline, "n-audio", &audioCount, NULL);
    return audioCount > 0;
}

gboolean Pipeline::cb_element(GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus)
    MediaObject *that = static_cast<MediaObject*>(data);
    gst_mini_object_ref(GST_MINI_OBJECT_CAST(msg));
    QMetaObject::invokeMethod(that, "handleElementMessage", Qt::QueuedConnection, Q_ARG(GstMessage*, msg));
    return true;
}

void Pipeline::handleElementMessage(GstMessage *gstMessage)
{
    if (gst_is_missing_plugin_message(gstMessage)) {
        m_installer->addPlugin(gstMessage);
    }
    gst_mini_object_unref(GST_MINI_OBJECT_CAST(gstMessage));
}

//TODO: implement state changes
void Pipeline::pluginInstallFailure(const QString &msg)
{
    bool canPlay = audioIsAvailable() || videoIsAvailable();
    Phonon::ErrorType error = canPlay ? Phonon::NormalError : Phonon::FatalError;
    emit errorMessage(msg, error);
}

void Pipeline::pluginInstallStarted()
{
    //setState(Phonon::LoadingState);
}

void Pipeline::pluginInstallComplete()
{
    qDebug() << "Install complete." << m_resumeAfterInstall;
    if (m_resumeAfterInstall) {
        setSource(m_lastSource);
        setState(GST_STATE_PLAYING);
    }
}

gboolean Pipeline::cb_error(GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus)
    Pipeline *that = static_cast<Pipeline*>(data);
    gst_mini_object_ref(GST_MINI_OBJECT_CAST(msg));
    QMetaObject::invokeMethod(that, "handleErrorMessage", Qt::QueuedConnection, Q_ARG(GstMessage*, msg));
    return true;
}

void Pipeline::handleErrorMessage(GstMessage *gstMessage)
{
    PluginInstaller::InstallStatus status = m_installer->checkInstalledPlugins();
    qDebug() << status;

    if (status == PluginInstaller::Missing) {
        Phonon::ErrorType type = (audioIsAvailable() || videoIsAvailable()) ? Phonon::NormalError : Phonon::FatalError;
        emit errorMessage(tr("One or more plugins are missing in your GStreamer installation."), type);
    } else if (status == PluginInstaller::Installed) {
        gchar *debug;
        GError *err;
        gst_message_parse_error (gstMessage, &err, &debug);
        //TODO: Log the error
        emit errorMessage(err->message, Phonon::FatalError);
        g_error_free(err);
    }
    gst_mini_object_unref(GST_MINI_OBJECT_CAST(gstMessage));
}

}
};

#include "moc_pipeline.cpp"
