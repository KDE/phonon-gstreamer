/*  This file is part of the KDE project.

    Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
    Copyright (C) 2011 Trever Fischer <tdfischer@fedoraproject.org>
    Copyright (C) 2011 Harald Sitter <sitter@kde.org>

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
#include "debug.h"
#include "mediaobject.h"
#include "backend.h"
#include "plugininstaller.h"
#include "streamreader.h"
#include "gsthelper.h"
#include <gst/pbutils/missing-plugins.h>
#include <gst/interfaces/navigation.h>
#include <gst/app/gstappsrc.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QMutexLocker>
#include "debug.h"
#define MAX_QUEUE_TIME 20 * GST_SECOND

QT_BEGIN_NAMESPACE
namespace Phonon
{
namespace Gstreamer
{

Pipeline::Pipeline(QObject *parent)
    : QObject(parent)
    , m_bufferPercent(0)
    , m_isStream(false)
    , m_isHttpUrl(false)
    , m_installer(new PluginInstaller(this))
    , m_reader(0) // Lazy init
    , m_resetting(false)
{
    qRegisterMetaType<GstState>("GstState");
    m_pipeline = GST_PIPELINE(gst_element_factory_make("playbin2", NULL));
    gst_object_ref(m_pipeline);
    gst_object_sink(m_pipeline);
    g_signal_connect(m_pipeline, "video-changed", G_CALLBACK(cb_videoChanged), this);
    g_signal_connect(m_pipeline, "text-tags-changed", G_CALLBACK(cb_textTagsChanged), this);
    g_signal_connect(m_pipeline, "notify::source", G_CALLBACK(cb_setupSource), this);
    g_signal_connect(m_pipeline, "about-to-finish", G_CALLBACK(cb_aboutToFinish), this);

    GstBus *bus = gst_pipeline_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, gst_bus_sync_signal_handler, NULL);
    g_signal_connect(bus, "sync-message::eos", G_CALLBACK(cb_eos), this);
    g_signal_connect(bus, "sync-message::warning", G_CALLBACK(cb_warning), this);

    //FIXME: This never gets called..?
    g_signal_connect(bus, "sync-message::duration", G_CALLBACK(cb_duration), this);

    g_signal_connect(bus, "sync-message::buffering", G_CALLBACK(cb_buffering), this);
    g_signal_connect(bus, "sync-message::state-changed", G_CALLBACK(cb_state), this);
    g_signal_connect(bus, "sync-message::element", G_CALLBACK(cb_element), this);
    g_signal_connect(bus, "sync-message::error", G_CALLBACK(cb_error), this);
    g_signal_connect(bus, "sync-message::tag", G_CALLBACK(cb_tag), this);
    gst_object_unref(bus);

    // Set up audio graph
    m_audioGraph = gst_bin_new("audioGraph");
    gst_object_ref (GST_OBJECT (m_audioGraph));
    gst_object_sink (GST_OBJECT (m_audioGraph));

    // Note that these queues are only required for streaming content
    // And should ideally be created on demand as they will disable
    // pull-mode access. Also note that the max-size-time are increased to
    // reduce buffer overruns as these are not gracefully handled at the moment.
    m_audioPipe = gst_element_factory_make("queue", "audioPipe");
    g_object_set(G_OBJECT(m_audioPipe), "max-size-time",  MAX_QUEUE_TIME, NULL);

    QByteArray tegraEnv = qgetenv("TEGRA_GST_OPENMAX");
    if (!tegraEnv.isEmpty()) {
        g_object_set(G_OBJECT(m_audioPipe), "max-size-time", 0, NULL);
        g_object_set(G_OBJECT(m_audioPipe), "max-size-buffers", 0, NULL);
        g_object_set(G_OBJECT(m_audioPipe), "max-size-bytes", 0, NULL);
    }

    gst_bin_add(GST_BIN(m_audioGraph), m_audioPipe);
    GstPad *audiopad = gst_element_get_static_pad (m_audioPipe, "sink");
    gst_element_add_pad (m_audioGraph, gst_ghost_pad_new ("sink", audiopad));
    gst_object_unref (audiopad);

    g_object_set(m_pipeline, "audio-sink", m_audioGraph, NULL);

    // Set up video graph
    m_videoGraph = gst_bin_new("videoGraph");
    gst_object_ref (GST_OBJECT (m_videoGraph));
    gst_object_sink (GST_OBJECT (m_videoGraph));

    m_videoPipe = gst_element_factory_make("queue", "videoPipe");
    gst_bin_add(GST_BIN(m_videoGraph), m_videoPipe);
    GstPad *videopad = gst_element_get_static_pad(m_videoPipe, "sink");
    gst_element_add_pad(m_videoGraph, gst_ghost_pad_new("sink", videopad));
    gst_object_unref(videopad);

    g_object_set(m_pipeline, "video-sink", m_videoGraph, NULL);

    //FIXME: Put this stuff somewhere else, or at least document why its needed.
    if (!tegraEnv.isEmpty()) {
        //TODO: Move this line into the videooutput
        //g_object_set(G_OBJECT(videoQueue), "max-size-time", 33000, NULL);
        g_object_set(G_OBJECT(m_audioPipe), "max-size-buffers", 1, NULL);
        g_object_set(G_OBJECT(m_audioPipe), "max-size-bytes", 0, NULL);
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

void Pipeline::setSource(const Phonon::MediaSource &source, bool reset)
{
    m_isStream = false;
    m_seeking = false;
    m_installer->reset();
    m_resumeAfterInstall = false;
    m_isHttpUrl = false;
    m_metaData.clear();
    if (m_reader) {
        m_reader->stop();
        // Because libphonon stream stuff likes to fail connection assert
        delete m_reader;
        m_reader = 0;
    }

    debug() << "New source:" << source.mrl();
    QByteArray gstUri;
    switch(source.type()) {
        case MediaSource::Url:
        case MediaSource::LocalFile:
            gstUri = source.mrl().toEncoded();
            if(source.mrl().scheme() == QLatin1String("http"))
                m_isHttpUrl = true;
            break;
        case MediaSource::Invalid:
            emit errorMessage("Invalid source specified", Phonon::FatalError);
            return;
        case MediaSource::Stream:
            gstUri = "appsrc://";
            m_isStream = true;
            break;
        case MediaSource::CaptureDevice:
            gstUri = captureDeviceURI(source);
            if (gstUri.isEmpty())
                emit errorMessage("Invalid capture device specified", Phonon::FatalError);
            break;
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
                case Phonon::NoDisc:
                    emit errorMessage("Invalid disk source specified", Phonon::FatalError);
                    return;
            }
            break;
        case MediaSource::Empty:
            return;
    }

    //TODO: Test this to make sure that resuming playback after plugin installation
    //when using an abstract stream source doesn't explode.
    m_currentSource = source;

    GstState oldState = state();

    if (reset && oldState > GST_STATE_READY) {
        debug() << "Resetting pipeline for reverse seek";
        m_resetting = true;
        m_posAtReset = position();
        gst_element_set_state(GST_ELEMENT(m_pipeline), GST_STATE_READY);
    }

    g_object_set(m_pipeline, "uri", gstUri.constData(), NULL);

    if (reset && oldState > GST_STATE_READY) {
        gst_element_set_state(GST_ELEMENT(m_pipeline), oldState);
    }
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
    DEBUG_BLOCK;
    m_resumeAfterInstall = true;
    debug() << "Transitioning to state" << GstHelper::stateName(state);

    if (state == GST_STATE_READY && m_reader)
        m_reader->stop();

    return gst_element_set_state(GST_ELEMENT(m_pipeline), state);
}

void Pipeline::writeToDot(MediaObject *media, const QString &type)
{
    GstBin *bin = GST_BIN(m_pipeline);
    if (media)
        media->backend()->logMessage(QString("Dumping %0.dot").arg(type), Backend::Debug, media);
    else {
        debug() << type;
    }
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(bin, GST_DEBUG_GRAPH_SHOW_ALL, QString("phonon-%0").arg(type).toUtf8().constData());
}

bool Pipeline::queryDuration(GstFormat *format, gint64 *duration) const
{
    return gst_element_query_duration(GST_ELEMENT(m_pipeline), format, duration);
}

GstState Pipeline::state() const
{
    GstState state;
    gst_element_get_state(GST_ELEMENT(m_pipeline), &state, NULL, 1000);
    return state;
}

gboolean Pipeline::cb_eos(GstBus *bus, GstMessage *gstMessage, gpointer data)
{
    Q_UNUSED(bus)
    Pipeline *that = static_cast<Pipeline*>(data);
    emit that->eos();
    return true;
}

gboolean Pipeline::cb_warning(GstBus *bus, GstMessage *gstMessage, gpointer data)
{
    Q_UNUSED(bus)
    gchar *debug;
    GError *err;
    Pipeline *that = static_cast<Pipeline*>(data);
    gst_message_parse_warning(gstMessage, &err, &debug);
    QString msgString;
    msgString.sprintf("Warning: %s\nMessage:%s", debug, err->message);
    emit that->warning(msgString);
    g_free (debug);
    g_error_free (err);
    return true;
}

gboolean Pipeline::cb_duration(GstBus *bus, GstMessage *gstMessage, gpointer data)
{
    Q_UNUSED(bus)
    gint64 duration;
    GstFormat format;
    Pipeline *that = static_cast<Pipeline*>(data);
    debug() << "Duration message";
    if (that->m_resetting)
        return true;
    gst_message_parse_duration(gstMessage, &format, &duration);
    if (format == GST_FORMAT_TIME)
        emit that->durationChanged(duration/GST_MSECOND);
    return true;
}

qint64 Pipeline::totalDuration() const
{
    GstFormat format = GST_FORMAT_TIME;
    gint64 duration = 0;
    if (queryDuration(&format, &duration)) {
        return duration/GST_MSECOND;
    }
    return -1;
}

gboolean Pipeline::cb_buffering(GstBus *bus, GstMessage *gstMessage, gpointer data)
{
    Q_UNUSED(bus)
    Pipeline *that = static_cast<Pipeline*>(data);
    gint percent = 0;
    gst_structure_get_int (gstMessage->structure, "buffer-percent", &percent); //gst_message_parse_buffering was introduced in 0.10.11

    if (that->m_bufferPercent != percent) {
        emit that->buffering(percent);
        that->m_bufferPercent = percent;
    }

    return true;
}

gboolean Pipeline::cb_state(GstBus *bus, GstMessage *gstMessage, gpointer data)
{
    Q_UNUSED(bus)
    GstState oldState;
    GstState newState;
    GstState pendingState;
    gchar *transitionName = NULL;
    Pipeline *that = static_cast<Pipeline*>(data);
    gst_message_parse_state_changed(gstMessage, &oldState, &newState, &pendingState);

    if (oldState == newState) {
        return true;
    }

    if (gstMessage->src != GST_OBJECT(that->m_pipeline)) {
        return true;
    }

    // Apparently gstreamer sometimes enters the same state twice.
    // FIXME: Sometimes we enter the same state twice. currently not disallowed by the state machine
    if (that->m_seeking) {
        if (GST_STATE_TRANSITION(oldState, newState) == GST_STATE_CHANGE_PAUSED_TO_PLAYING)
            that->m_seeking = false;
        return true;
    }
    debug() << "State change";

    transitionName = g_strdup_printf ("%s_%s", gst_element_state_get_name (oldState),
        gst_element_state_get_name (newState));
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (that->m_pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
        QByteArray("phonon-gstreamer.") + QByteArray(transitionName));
    g_free(transitionName);

    if (newState == GST_STATE_READY) {
        that->m_installer->checkInstalledPlugins();
    }

    //FIXME: This is a hack until proper state engine is implemented in the pipeline
    // Wait to update stuff until we're at the final requested state
    if (pendingState == GST_STATE_VOID_PENDING && newState > GST_STATE_READY && that->m_resetting) {
        that->m_resetting = false;
        that->seekToMSec(that->m_posAtReset);
//        return;
    }

    if (pendingState == GST_STATE_VOID_PENDING) {
        emit that->durationChanged(that->totalDuration());
        emit that->seekableChanged(that->isSeekable());
    }

    emit that->stateChanged(oldState, newState);
    return true;
}

void Pipeline::cb_videoChanged(GstElement *playbin, gpointer data)
{
    gint videoCount;
    bool videoAvailable;
    Pipeline *that = static_cast<Pipeline*>(data);
    g_object_get(playbin, "n-video", &videoCount, NULL);
    // If there is at least one video stream, we've got video.
    videoAvailable = videoCount > 0;

    // FIXME: Only emit this if n-video goes between 0 and non zero.
    emit that->videoAvailabilityChanged(videoAvailable);
}

void Pipeline::cb_textTagsChanged(GstElement *playbin, gint stream, gpointer data)
{
    Pipeline *that = static_cast<Pipeline *>(data);
    emit that->textTagChanged(stream);
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

gboolean Pipeline::cb_element(GstBus *bus, GstMessage *gstMessage, gpointer data)
{
    Q_UNUSED(bus)
    Pipeline *that = static_cast<Pipeline*>(data);
    const GstStructure *str = gst_message_get_structure(gstMessage);
    if (gst_is_missing_plugin_message(gstMessage)) {
        that->m_installer->addPlugin(gstMessage);
    } else {
#if GST_VERSION >= GST_VERSION_CHECK(0,10,23,0)
        switch (gst_navigation_message_get_type(gstMessage)) {
        case GST_NAVIGATION_MESSAGE_MOUSE_OVER: {
            gboolean active;
            if (!gst_navigation_message_parse_mouse_over(gstMessage, &active)) {
                break;
            }
            emit that->mouseOverActive(active);
            break;
        }
        case GST_NAVIGATION_MESSAGE_COMMANDS_CHANGED:
            that->updateNavigation();
            break;
        default:
            break;
        }
#endif // GST_VERSION
    }
    // Currently undocumented, but discovered via gst-plugins-base commit 7e674d
    // gst 0.10.25.1
    if (gst_structure_has_name(str, "playbin2-stream-changed")) {
        gchar *uri;
        g_object_get(that->m_pipeline, "uri", &uri, NULL);
        debug() << "Stream changed to" << uri;
        g_free(uri);
        if (!that->m_resetting)
            emit that->streamChanged();
    }
    if (gst_structure_has_name(str, "prepare-xwindow-id"))
        emit that->windowIDNeeded();
    return true;
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
    debug() << "Install complete." << m_resumeAfterInstall;
    if (m_resumeAfterInstall) {
        setSource(m_currentSource);
        setState(GST_STATE_PLAYING);
    }
}

gboolean Pipeline::cb_error(GstBus *bus, GstMessage *gstMessage, gpointer data)
{
    Q_UNUSED(bus)
    Pipeline *that = static_cast<Pipeline*>(data);
    PluginInstaller::InstallStatus status = that->m_installer->checkInstalledPlugins();
    debug() << status;

    if (status == PluginInstaller::Missing) {
        Phonon::ErrorType type = (that->audioIsAvailable() || that->videoIsAvailable()) ? Phonon::NormalError : Phonon::FatalError;
        emit that->errorMessage(tr("One or more plugins are missing in your GStreamer installation."), type);
    } else if (status == PluginInstaller::Installed) {
        GError *err;
        //TODO: Log the error
        gst_message_parse_error (gstMessage, &err, NULL);
        emit that->errorMessage(err->message, Phonon::FatalError);
        g_error_free(err);
    }
    return true;
}

/*
 * Used to iterate through the gst_tag_list and extract values
 */
void foreach_tag_function(const GstTagList *list, const gchar *tag, gpointer user_data)
{
    TagMap *newData = static_cast<TagMap *>(user_data);
    QString value;
    GType type = gst_tag_get_type(tag);
    switch (type) {
    case G_TYPE_STRING: {
            char *str = 0;
            gst_tag_list_get_string(list, tag, &str);
            value = QString::fromUtf8(str);
            g_free(str);
        }
        break;

    case G_TYPE_BOOLEAN: {
            int bval;
            gst_tag_list_get_boolean(list, tag, &bval);
            value = QString::number(bval);
        }
        break;

    case G_TYPE_INT: {
            int ival;
            gst_tag_list_get_int(list, tag, &ival);
            value = QString::number(ival);
        }
        break;

    case G_TYPE_UINT: {
            unsigned int uival;
            gst_tag_list_get_uint(list, tag, &uival);
            value = QString::number(uival);
        }
        break;

    case G_TYPE_FLOAT: {
            float fval;
            gst_tag_list_get_float(list, tag, &fval);
            value = QString::number(fval);
        }
        break;

    case G_TYPE_DOUBLE: {
            double dval;
            gst_tag_list_get_double(list, tag, &dval);
            value = QString::number(dval);
        }
        break;

    default:
        //debug("Unsupported tag type: %s", g_type_name(type));
        break;
    }

    QString key = QString(tag).toUpper();
    QString currVal = newData->value(key);
    if (!value.isEmpty() && !(newData->contains(key) && currVal == value))
        newData->insert(key, value);
}

gboolean Pipeline::cb_tag(GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus)
    Pipeline *that = static_cast<Pipeline*>(data);
    QMutexLocker lock(&that->m_tagLock);

    bool isStream = that->m_isStream || that->m_isHttpUrl;
    GstTagList* tag_list = 0;
    gst_message_parse_tag(msg, &tag_list);
    if (tag_list) {
        TagMap newTags;
        gst_tag_list_foreach (tag_list, &foreach_tag_function, &newTags);
        gst_tag_list_free(tag_list);

        // Determine if we should no fake the album/artist tags.
        // This is a little confusing as we want to fake it on initial
        // connection where title, album and artist are all missing.
        // There are however times when we get just other information,
        // e.g. codec, and so we want to only do clever stuff if we
        // have a commonly available tag (ORGANIZATION) or we have a
        // change in title
        bool fake_it =
           (isStream
            && ((!newTags.contains("TITLE")
                 && newTags.contains("ORGANIZATION"))
                || (newTags.contains("TITLE")
                    && that->m_metaData.value("TITLE") != newTags.value("TITLE")))
            && !newTags.contains("ALBUM")
            && !newTags.contains("ARTIST"));

        TagMap oldMap = that->m_metaData; // Keep a copy of the old one for reference

        // Now we've checked the new data, append any new meta tags to the existing tag list
        // We cannot use TagMap::iterator as this is a multimap and when streaming data
        // could in theory be lost.
        QList<QString> keys = newTags.keys();
        for (QList<QString>::iterator i = keys.begin(); i != keys.end(); ++i) {
            QString key = *i;
            if (isStream) {
                // If we're streaming, we need to remove data in m_metaData
                // in order to stop it filling up indefinitely (as it's a multimap)
                that->m_metaData.remove(key);
            }
            QList<QString> values = newTags.values(key);
            for (QList<QString>::iterator j = values.begin(); j != values.end(); ++j) {
                QString value = *j;
                QString currVal = that->m_metaData.value(key);
                if (!that->m_metaData.contains(key) || currVal != value) {
                    that->m_metaData.insert(key, value);
                }
            }
        }

        if (that->m_metaData.contains("TRACK-COUNT")) {
            that->m_metaData.insert("TRACKNUMBER", newTags.value("TRACK-COUNT"));
            emit that->trackCountChanged(newTags.value("TRACK-COUNT").toInt());
        }
        if (that->m_metaData.contains("MUSICBRAINZ-DISCID")) {
            that->m_metaData.insert("MUSICBRAINZ_DISCID", newTags.value("MUSICBRAINZ-DISCID"));
        }

        // For radio streams, if we get a metadata update where the title changes, we assume everything else is invalid.
        // If we don't already have a title, we don't do anything since we're actually just appending new data into that.
        if (that->m_isStream && oldMap.contains("TITLE") && that->m_metaData.value("TITLE") != oldMap.value("TITLE")) {
            that->m_metaData.clear();
        }

        if (oldMap != that->m_metaData) {
            // This is a bit of a hack to ensure that stream metadata is
            // returned. We get as much as we can from the Shoutcast server's
            // StreamTitle= header. If further info is decoded from the stream
            // itself later, then it will overwrite this info.
            if (fake_it) {
                that->m_metaData.remove("ALBUM");
                that->m_metaData.remove("ARTIST");

                // Detect whether we want to "fill in the blanks"
                QString str;
                if (that->m_metaData.contains("TITLE"))
                {
                    str = that->m_metaData.value("TITLE");
                    int splitpoint;
                    // Check to see if our title matches "%s - %s"
                    // Where neither %s are empty...
                    if ((splitpoint = str.indexOf(" - ")) > 0
                        && str.size() > (splitpoint+3)) {
                        that->m_metaData.insert("ARTIST", str.left(splitpoint));
                        that->m_metaData.replace("TITLE", str.mid(splitpoint+3));
                    }
                } else {
                    str = that->m_metaData.value("GENRE");
                    if (!str.isEmpty())
                        that->m_metaData.insert("TITLE", str);
                    else
                        that->m_metaData.insert("TITLE", "Streaming Data");
                }
                if (!that->m_metaData.contains("ARTIST")) {
                    str = that->m_metaData.value("LOCATION");
                    if (!str.isEmpty())
                        that->m_metaData.insert("ARTIST", str);
                    else
                        that->m_metaData.insert("ARTIST", "Streaming Data");
                }
                str = that->m_metaData.value("ORGANIZATION");
                if (!str.isEmpty())
                    that->m_metaData.insert("ALBUM", str);
                else
                    that->m_metaData.insert("ALBUM", "Streaming Data");
            }
            // As we manipulate the title, we need to recompare
            // oldMap and m_metaData here...
            //if (oldMap != m_metaData && !m_loading)

            // Only emit signal if we're on a live stream.
            // Its a kludgy hack that for 99% of cases of streaming should work.
            // If not, this needs fixed in mediaobject.cpp.
            guint kbps;
            g_object_get(that->m_pipeline, "connection-speed", &kbps, NULL);
	    // This hack does not work now.
            // if (that->m_currentSource.discType() == Phonon::Cd || kbps != 0)
                emit that->metaDataChanged(that->m_metaData);
        }
    }
    return true;
}

QMultiMap<QString, QString> Pipeline::metaData() const
{
    return m_metaData;
}

//FIXME: This apparently was never implemented in mediaobject. No idea if clobbering all data is
//intended behavior...
void Pipeline::setMetaData(const QMultiMap<QString, QString> &newData)
{
    m_metaData = newData;
}

void Pipeline::updateNavigation()
{
    QList<MediaController::NavigationMenu> ret;
#if GST_VERSION >= GST_VERSION_CHECK(0,10,23,0)
    GstElement *target = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_NAVIGATION);
    if (target) {
        GstQuery *query = gst_navigation_query_new_commands();
        gboolean res = gst_element_query(target, query);
        guint count;
        if (res && gst_navigation_query_parse_commands_length(query, &count)) {
            for(guint i = 0; i < count; ++i) {
                GstNavigationCommand cmd;
                if (!gst_navigation_query_parse_commands_nth(query, i, &cmd))
                    break;
                switch (cmd) {
                case GST_NAVIGATION_COMMAND_DVD_ROOT_MENU:
                    ret << MediaController::RootMenu;
                    break;
                case GST_NAVIGATION_COMMAND_DVD_TITLE_MENU:
                    ret << MediaController::TitleMenu;
                    break;
                case GST_NAVIGATION_COMMAND_DVD_AUDIO_MENU:
                    ret << MediaController::AudioMenu;
                    break;
                case GST_NAVIGATION_COMMAND_DVD_SUBPICTURE_MENU:
                    ret << MediaController::SubtitleMenu;
                    break;
                case GST_NAVIGATION_COMMAND_DVD_CHAPTER_MENU:
                    ret << MediaController::ChapterMenu;
                    break;
                case GST_NAVIGATION_COMMAND_DVD_ANGLE_MENU:
                    ret << MediaController::AngleMenu;
                    break;
                default:
                    break;
                }
            }
        }
    }
#endif
    if (ret != m_menus) {
        m_menus = ret;
        emit availableMenusChanged(m_menus);
    }
}

QList<MediaController::NavigationMenu> Pipeline::availableMenus() const
{
    return m_menus;
}

bool Pipeline::seekToMSec(qint64 time)
{
    m_posAtReset = time;
    if (m_resetting)
        return true;
    if (state() == GST_STATE_PLAYING)
        m_seeking = true;
    return gst_element_seek(GST_ELEMENT(m_pipeline), 1.0, GST_FORMAT_TIME,
                     GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET,
                     time * GST_MSECOND, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
}

bool Pipeline::isSeekable() const
{
    GstQuery *query;
    gboolean result;
    gint64 start, stop;
    query = gst_query_new_seeking(GST_FORMAT_TIME);
    result = gst_element_query (GST_ELEMENT(m_pipeline), query);
    if (result) {
        gboolean seekable;
        GstFormat format;
        gst_query_parse_seeking(query, &format, &seekable, &start, &stop);
        gst_query_unref (query);
        return seekable;
    } else {
        //TODO: Log failure
    }
    gst_query_unref (query);
    return false;
}

Phonon::State Pipeline::phononState() const
{
    return Phonon::PlayingState;
    switch (state()) {
        case GST_STATE_PLAYING:
            return Phonon::PlayingState;
        case GST_STATE_READY:
            return Phonon::StoppedState;
        case GST_STATE_NULL:
            return Phonon::LoadingState;
        case GST_STATE_PAUSED:
            return Phonon::PausedState;
        case GST_STATE_VOID_PENDING: //Quiet GCC
            break;
    }
    return Phonon::ErrorState;
}

static void cb_feedAppSrc(GstAppSrc *appSrc, guint buffsize, gpointer data)
{
    DEBUG_BLOCK;
    StreamReader *reader = static_cast<StreamReader*>(data);
    GstBuffer *buf = gst_buffer_new_and_alloc(buffsize);
    reader->read(reader->currentPos(), buffsize, (char*)GST_BUFFER_DATA(buf));
    gst_app_src_push_buffer(appSrc, buf);
}

static void cb_seekAppSrc(GstAppSrc *appSrc, guint64 pos, gpointer data)
{
    Q_UNUSED(appSrc);
    DEBUG_BLOCK;
    StreamReader *reader = static_cast<StreamReader*>(data);
    reader->setCurrentPos(pos);
}

void Pipeline::cb_setupSource(GstElement *playbin, GParamSpec *param, gpointer data)
{
    Q_UNUSED(playbin);
    Q_UNUSED(param);
    DEBUG_BLOCK;
    GstElement *phononSrc;
    Pipeline *that = static_cast<Pipeline*>(data);
    gst_object_ref(that->m_pipeline);
    g_object_get(that->m_pipeline, "source", &phononSrc, NULL);
    if (that->m_isStream) {
        if (!that->m_reader)
            that->m_reader = new StreamReader(that->m_currentSource, that);
        if (that->m_reader->streamSize() > 0)
            g_object_set(phononSrc, "size", that->m_reader->streamSize(), NULL);
        int streamType = 0;
        if (that->m_reader->streamSeekable())
            streamType = GST_APP_STREAM_TYPE_SEEKABLE;
        else
            streamType = GST_APP_STREAM_TYPE_STREAM;
        g_object_set(phononSrc, "stream-type", streamType, NULL);
        g_object_set(phononSrc, "block", TRUE, NULL);
        g_signal_connect(phononSrc, "need-data", G_CALLBACK(cb_feedAppSrc), that->m_reader);
        g_signal_connect(phononSrc, "seek-data", G_CALLBACK(cb_seekAppSrc), that->m_reader);
        that->m_reader->start();
    } else {
        if (that->currentSource().type() == MediaSource::Url
                && that->currentSource().mrl().scheme().startsWith(QLatin1String("http"))
                // Check whether this property exists.
                // Setting it on a source other than souphttpsrc (which supports it) may break playback.
                && g_object_class_find_property(G_OBJECT_GET_CLASS(phononSrc), "user-agent")) {
            QString userAgent = QCoreApplication::applicationName() + '/' + QCoreApplication::applicationVersion();
            userAgent += QString(" (Phonon/%0; Phonon-GStreamer/%1)").arg(PHONON_VERSION_STR).arg(PHONON_GST_VERSION);
            g_object_set(phononSrc, "user-agent", userAgent.toUtf8().constData(), NULL);
        } else if (that->currentSource().type() == MediaSource::Disc &&
                   !that->currentSource().deviceName().isEmpty()) {
            debug() << "setting device prop to" << that->currentSource().deviceName();
            g_object_set(phononSrc, "device", that->currentSource().deviceName().toUtf8().constData(), NULL);
        }
    }
    gst_object_unref(that->m_pipeline);
}

void Pipeline::cb_aboutToFinish(GstElement *appSrc, gpointer data)
{
    Q_UNUSED(appSrc);
    Pipeline *that = static_cast<Pipeline*>(data);
    emit that->aboutToFinish();
}

Phonon::MediaSource Pipeline::currentSource() const
{
    return m_currentSource;
}

qint64 Pipeline::position() const
{
    gint64 pos = 0;
    GstFormat format = GST_FORMAT_TIME;
    if (m_resetting)
        return m_posAtReset;
    gst_element_query_position (GST_ELEMENT(m_pipeline), &format, &pos);
    return (pos / GST_MSECOND);
}

QByteArray Pipeline::captureDeviceURI(const MediaSource &source) const
{
#ifndef PHONON_NO_AUDIOCAPTURE
    //TODO
#endif
#ifndef PHONON_NO_VIDEOCAPTURE
    if (source.videoCaptureDevice().isValid()) {
        DeviceAccessList devList = source.videoCaptureDevice().property("deviceAccessList").value<Phonon::DeviceAccessList>();
        QString devPath;
        foreach (DeviceAccess dev, devList) {
            if (dev.first == "v4l2") {
                return QString("v4l2://%0").arg(dev.second).toUtf8();
            }
        }
    }
#endif
    return QByteArray();
}

}
};

#include "moc_pipeline.cpp"
