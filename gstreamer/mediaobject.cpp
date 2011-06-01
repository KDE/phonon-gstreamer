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

#include <cmath>
#include <gst/interfaces/navigation.h>
#include <gst/interfaces/propertyprobe.h>
#include "mediaobject.h"
#include "backend.h"
#include "streamreader.h"
#include "phonon-config-gstreamer.h"
#include "gsthelper.h"
#include "pipeline.h"

#include <QtCore/QByteRef>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtGui/QApplication>

#define ABOUT_TO_FINNISH_TIME 2000
#define MAX_QUEUE_TIME 20 * GST_SECOND

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

MediaObject::MediaObject(Backend *backend, QObject *parent)
        : QObject(parent)
        , MediaNode(backend, AudioSource | VideoSource)
        , m_resumeState(false)
        , m_oldState(Phonon::LoadingState)
        , m_oldPos(0)
        , m_state(Phonon::LoadingState)
        , m_pendingState(Phonon::LoadingState)
        , m_tickTimer(new QTimer(this))
        , m_prefinishMark(0)
        , m_transitionTime(0)
        , m_isStream(false)
        , m_posAtSeek(-1)
        , m_prefinishMarkReachedNotEmitted(true)
        , m_aboutToFinishEmitted(false)
        , m_loading(false)
        , m_capsHandler(0)
        , m_totalTime(-1)
        , m_hasVideo(false)
        , m_hasAudio(false)
        , m_seekable(false)
        , m_atEndOfStream(false)
        , m_atStartOfStream(false)
        , m_error(Phonon::NoError)
        , m_pipeline(0)
        , m_previousTickTime(-1)
        , m_resetNeeded(false)
        , m_autoplayTitles(true)
        , m_availableTitles(0)
        , m_currentTitle(1)
        , m_pendingTitle(1)
{
    qRegisterMetaType<GstCaps*>("GstCaps*");
    qRegisterMetaType<State>("State");
    qRegisterMetaType<GstMessage*>("GstMessage*");

    static int count = 0;
    m_name = "MediaObject" + QString::number(count++);

    if (!m_backend->isValid()) {
        setError(tr("Cannot start playback. \n\nCheck your GStreamer installation and make sure you "
                    "\nhave libgstreamer-plugins-base installed."), Phonon::FatalError);
    } else {
        m_root = this;
        m_pipeline = new Pipeline(this);
        m_isValid = true;

        connect(m_pipeline, SIGNAL(eos()), this, SLOT(handleEndOfStream()));
        connect(m_pipeline, SIGNAL(warning(const QString &)), this, SLOT(logWarning(const QString &)));
        connect(m_pipeline, SIGNAL(durationChanged()), this, SLOT(updateTotalTime()));
        connect(m_pipeline, SIGNAL(buffering(int)), this, SLOT(handleBuffering(int)));
        connect(m_pipeline, SIGNAL(stateChanged(GstState, GstState)), this, SLOT(handleStateChange(GstState, GstState)));
        connect(m_pipeline, SIGNAL(errorMessage(const QString &, Phonon::ErrorType)), this, SLOT(setError(const QString &, Phonon::ErrorType)));
        connect(m_pipeline, SIGNAL(metaDataChanged(QMultiMap<QString, QString>)), this, SIGNAL(metaDataChanged(QMultiMap<QString, QString>)));
        connect(m_pipeline, SIGNAL(availableMenusChanged(QList<MediaController::NavigationMenu>)), this, SIGNAL(availableMenusChanged(QList<MediaController::NavigationMenu>)));

        connect(m_tickTimer, SIGNAL(timeout()), SLOT(emitTick()));
    }
    connect(this, SIGNAL(stateChanged(Phonon::State, Phonon::State)),
            this, SLOT(notifyStateChange(Phonon::State, Phonon::State)));
}

MediaObject::~MediaObject()
{
    if (m_pipeline) {
        GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline->element()));
        g_signal_handlers_disconnect_matched(bus, G_SIGNAL_MATCH_DATA, 0, 0, 0, 0, this);
        delete m_pipeline;
    }
}

QString stateString(const Phonon::State &state)
{
    switch (state) {
    case Phonon::LoadingState:
        return QString("LoadingState");
    case Phonon::StoppedState:
        return QString("StoppedState");
    case Phonon::PlayingState:
        return QString("PlayingState");
    case Phonon::BufferingState:
        return QString("BufferingState");
    case Phonon::PausedState:
        return QString("PausedState");
    case Phonon::ErrorState:
        return QString("ErrorState");
    }
    return QString();
}

void MediaObject::saveState()
{
    //Only first resumeState is respected
    if (m_resumeState)
        return;

    if (m_pendingState == Phonon::PlayingState || m_pendingState == Phonon::PausedState) {
        m_resumeState = true;
        m_oldState = m_pendingState;
        m_oldPos = getPipelinePos();
    }
}

void MediaObject::resumeState()
{
    if (m_resumeState)
        QMetaObject::invokeMethod(this, "setState", Qt::QueuedConnection, Q_ARG(State, m_oldState));
}

//TODO: Move into pipeline, use gst_video_parse_caps_pixel_aspect_ratio() and/or gst_video_get_size()
void MediaObject::setVideoCaps(GstCaps *caps)
{
    GstStructure *str;
    gint width, height;

    if ((str = gst_caps_get_structure (caps, 0))) {
        if (gst_structure_get_int (str, "width", &width) && gst_structure_get_int (str, "height", &height)) {
            gint aspectNum = 0;
            gint aspectDenum = 0;
            if (gst_structure_get_fraction(str, "pixel-aspect-ratio", &aspectNum, &aspectDenum)) {
                if (aspectDenum > 0)
                    width = width*aspectNum/aspectDenum;
            }
            // Let child nodes know about our new video size
            QSize size(width, height);
            MediaNodeEvent event(MediaNodeEvent::VideoSizeChanged, &size);
            notify(&event);
        }
    }
    gst_caps_unref(caps);
}

/**
 * !reimp
 */
State MediaObject::state() const
{
    return m_state;
}

/**
 * !reimp
 */
bool MediaObject::hasVideo() const
{
    return m_hasVideo;
}

/**
 * !reimp
 */
bool MediaObject::isSeekable() const
{
    return m_seekable;
}

/**
 * !reimp
 */
qint64 MediaObject::currentTime() const
{
    if (m_resumeState)
        return m_oldPos;

    switch (state()) {
    case Phonon::PausedState:
    case Phonon::BufferingState:
    case Phonon::PlayingState:
        return getPipelinePos();
    case Phonon::StoppedState:
    case Phonon::LoadingState:
        return 0;
    case Phonon::ErrorState:
        break;
    }
    return -1;
}

/**
 * !reimp
 */
qint32 MediaObject::tickInterval() const
{
    return m_tickInterval;
}

/**
 * !reimp
 */
void MediaObject::setTickInterval(qint32 newTickInterval)
{
    m_tickInterval = newTickInterval;
    if (m_tickInterval <= 0)
        m_tickTimer->setInterval(50);
    else
        m_tickTimer->setInterval(newTickInterval);
}

/**
 * !reimp
 */
void MediaObject::play()
{
    setState(Phonon::PlayingState);
    m_resumeState = false;
}

/**
 * !reimp
 */
QString MediaObject::errorString() const
{
    return m_errorString;
}

/**
 * !reimp
 */
Phonon::ErrorType MediaObject::errorType() const
{
    return m_error;
}

/**
 * Set the current state of the mediaObject.
 *
 * !### Note that both Playing and Paused states are set immediately
 *     This should obviously be done in response to actual gstreamer state changes
 */
void MediaObject::setState(State newstate)
{
    if (!isValid())
        return;

    if (m_state == newstate)
        return;

    if (m_loading) {
        // We are still loading. The state will be requested
        // when loading has completed.
        m_pendingState = newstate;
        return;
    }

    GstState currentState = m_pipeline->state();

    switch (newstate) {
    case Phonon::BufferingState:
        m_backend->logMessage("phonon state request: buffering", Backend::Info, this);
        break;

    case Phonon::PausedState:
        m_backend->logMessage("phonon state request: paused", Backend::Info, this);
        if (currentState == GST_STATE_PAUSED) {
            changeState(Phonon::PausedState);
        } else if (m_pipeline->setState(GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE) {
            m_pendingState = Phonon::PausedState;
        } else {
            m_backend->logMessage("phonon state request failed", Backend::Info, this);
        }
        break;

    case Phonon::StoppedState:
        m_backend->logMessage("phonon state request: Stopped", Backend::Info, this);
        if (currentState == GST_STATE_READY) {
            changeState(Phonon::StoppedState);
        } else if (m_pipeline->setState(GST_STATE_READY) != GST_STATE_CHANGE_FAILURE) {
            m_pendingState = Phonon::StoppedState;
        } else {
            m_backend->logMessage("phonon state request failed", Backend::Info, this);
        }
        m_atEndOfStream = false;
        break;

    case Phonon::PlayingState:
#ifdef __GNUC__
#warning TODO - drop m_resetNeeded (messes with one-time-connection URLs)
#endif
        // TODO 4.5: drop m_resetNeeded completely and use live connections, whatever
        // those might be.
        if (m_source.url().host().contains(QLatin1String("last.fm"))) {
            // Never reset for last.fm as they only allow one connection attempt.
            // https://bugs.kde.org/show_bug.cgi?id=252649
            m_resetNeeded = false;
        }
        if (m_resetNeeded) {
            // ### Note this is a workaround and it should really be gracefully
            // handled by medianode when we implement live connections.
            // This generally happens if medianodes have been connected after the MediaSource was set
            // Note that a side-effect of this is that we resend all meta data.
            m_pipeline->setState(GST_STATE_NULL);
            m_resetNeeded = false;
            // Send a source change so the X11 renderer
            // will re-set the overlay
            MediaNodeEvent event(MediaNodeEvent::SourceChanged);
            notify(&event);
        }
        m_backend->logMessage("phonon state request: Playing", Backend::Info, this);
        if (m_atEndOfStream) {
            m_backend->logMessage("EOS already reached", Backend::Info, this);
        } else if (currentState == GST_STATE_PLAYING) {
            m_backend->logMessage("Already playing", Backend::Info, this);
            changeState(Phonon::PlayingState);
        } else {
            GstStateChangeReturn status = m_pipeline->setState(GST_STATE_PLAYING);
            if (status == GST_STATE_CHANGE_ASYNC) {
                m_backend->logMessage("Playing state is now pending");
                m_pendingState = Phonon::PlayingState;
            } else if (status == GST_STATE_CHANGE_FAILURE) {
                m_backend->logMessage("phonon state request failed", Backend::Info, this);
                changeState(Phonon::ErrorState);
            }
        }
        break;

    case Phonon::ErrorState:
        m_backend->logMessage("phonon state request : Error", Backend::Warning, this);
        m_backend->logMessage(QString("Last error : %0").arg(errorString()) , Backend::Warning, this);
        changeState(Phonon::ErrorState); //immediately set error state
        break;

    case Phonon::LoadingState:
        m_backend->logMessage("phonon state request: Loading", Backend::Info, this);
        changeState(Phonon::LoadingState);
        break;
    }
}

/*
 * Signals that the requested state has completed
 * by emitting stateChanged and updates the internal state.
 */
void MediaObject::changeState(State newstate)
{
    if (newstate == m_state)
        return;

    Phonon::State oldState = m_state;
    m_state = newstate; // m_state must be set before emitting, since
                        // Error state requires that state() will return the new value
    m_pendingState = newstate;

    switch (newstate) {
    case Phonon::PausedState:
        m_backend->logMessage("phonon state changed: paused", Backend::Info, this);
        break;

    case Phonon::BufferingState:
        m_backend->logMessage("phonon state changed: buffering", Backend::Info, this);
        break;

    case Phonon::PlayingState:
        m_backend->logMessage("phonon state changed: Playing", Backend::Info, this);
        break;

    case Phonon::StoppedState:
        m_backend->logMessage("phonon state changed: Stopped", Backend::Info, this);
        // We must reset the pipeline when playing again
        m_resetNeeded = true;
        m_tickTimer->stop();
        break;

    case Phonon::ErrorState:
        m_loading = false;
        m_backend->logMessage("phonon state changed : Error", Backend::Info, this);
        m_backend->logMessage(errorString(), Backend::Warning, this);
        break;

    case Phonon::LoadingState:
        m_backend->logMessage("phonon state changed: Loading", Backend::Info, this);
        break;
    }

    emit stateChanged(newstate, oldState);
}

void MediaObject::setError(const QString &errorString, Phonon::ErrorType error)
{
    m_backend->logMessage(QString("Phonon error: %1 (code %2)").arg(errorString).arg(error), Backend::Warning);
    m_errorString = errorString;
    m_error = error;
    m_tickTimer->stop();

    if (error == Phonon::FatalError) {
        m_hasVideo = false;
        emit hasVideoChanged(false);
        m_pipeline->setState(GST_STATE_READY);
        changeState(Phonon::ErrorState);
    } else {
        if (m_loading) //Flag error only after loading has completed
            m_pendingState = Phonon::ErrorState;
        else
            changeState(Phonon::ErrorState);
    }
}

qint64 MediaObject::totalTime() const
{
    return m_totalTime;
}

qint32 MediaObject::prefinishMark() const
{
    return m_prefinishMark;
}

qint32 MediaObject::transitionTime() const
{
    return m_transitionTime;
}

void MediaObject::setTransitionTime(qint32 time)
{
    m_transitionTime = time;
}

qint64 MediaObject::remainingTime() const
{
    return totalTime() - currentTime();
}

MediaSource MediaObject::source() const
{
    return m_source;
}

void MediaObject::setNextSource(const MediaSource &source)
{
    if (source.type() == MediaSource::Invalid &&
        source.type() == MediaSource::Empty)
        return;
    m_nextSource = source;
}

/**
 * Update total time value from the pipeline
 */
bool MediaObject::updateTotalTime()
{
    GstFormat   format = GST_FORMAT_TIME;
    gint64 duration = 0;
    if (m_pipeline->queryDuration(&format, &duration)) {
        setTotalTime(duration / GST_MSECOND);
        return true;
    }
    return false;
}

/**
 * Checks if the current source is seekable
 */
void MediaObject::updateSeekable()
{
    if (!isValid())
        return;

    GstQuery *query;
    gboolean result;
    gint64 start, stop;
    query = gst_query_new_seeking(GST_FORMAT_TIME);
    result = gst_element_query (m_pipeline->element(), query);
    if (result) {
        gboolean seekable;
        GstFormat format;
        gst_query_parse_seeking (query, &format, &seekable, &start, &stop);

        if (m_seekable != seekable) {
            m_seekable = seekable;
            emit seekableChanged(m_seekable);
        }

        if (m_seekable) {
            m_backend->logMessage("Stream is seekable", Backend::Info, this);
            m_pipeline->writeToDot(this, "updateSeekable-true");
        } else {
            m_backend->logMessage("Stream is non-seekable", Backend::Info, this);
            m_pipeline->writeToDot(this, "updateSeekable-false");
        }
    } else {
        m_backend->logMessage("updateSeekable query failed", Backend::Info, this);
        m_pipeline->writeToDot(this, "updateSeekable-failed");
    }
    gst_query_unref (query);
}

qint64 MediaObject::getPipelinePos() const
{
    Q_ASSERT(m_pipeline);

    // Note some formats (usually mpeg) do not allow us to accurately seek to the
    // beginning or end of the file so we 'fake' it here rather than exposing the front end to potential issues.
    if (m_atEndOfStream)
        return totalTime();
    if (m_atStartOfStream)
        return 0;
    if (m_posAtSeek >= 0)
        return m_posAtSeek;

    gint64 pos = 0;
    GstFormat format = GST_FORMAT_TIME;
    gst_element_query_position (GST_ELEMENT(m_pipeline->element()), &format, &pos);
    return (pos / GST_MSECOND);
}

/*
 * Internal method to set a new total time for the media object
 */
void MediaObject::setTotalTime(qint64 newTime)
{

    if (newTime == m_totalTime)
        return;

    m_totalTime = newTime;

    emit totalTimeChanged(m_totalTime);
}

/*
 * !reimp
 */
void MediaObject::setSource(const MediaSource &source)
{
    if (!isValid())
        return;

    // We have to reset the state completely here, otherwise
    // remnants of the old pipeline can result in strangenes
    // such as failing duration queries etc
    m_pipeline->setState(GST_STATE_NULL);
    m_pipeline->state();

    m_source = source;
    emit currentSourceChanged(m_source);
    m_previousTickTime = -1;

    // Go into to loading state
    changeState(Phonon::LoadingState);
    m_loading = true;
    // IMPORTANT: Honor the m_resetNeeded flag as it currently stands.
    // See https://qa.mandriva.com/show_bug.cgi?id=56807
    //m_resetNeeded = false;
    m_resumeState = false;
    m_pendingState = Phonon::StoppedState;

     // Make sure we start out unconnected
/*    if (GST_ELEMENT_PARENT(m_audioGraph))
        gst_bin_remove(GST_BIN(m_pipeline->element()), m_audioGraph);
    if (GST_ELEMENT_PARENT(m_videoGraph))
        gst_bin_remove(GST_BIN(m_pipeline->element()), m_videoGraph);*/

    // Clear any existing errors
    m_aboutToFinishEmitted = false;
    m_error = NoError;
    m_errorString.clear();

    m_prefinishMarkReachedNotEmitted = true;
    m_aboutToFinishEmitted = false;
    m_hasAudio = false;
    setTotalTime(-1);
    m_atEndOfStream = false;

    m_availableTitles = 0;
    m_pendingTitle = 1;
    m_currentTitle = 1;

    // Clear existing meta tags
    m_isStream = false;

    m_pipeline->setSource(source);

    MediaNodeEvent event(MediaNodeEvent::SourceChanged);
    notify(&event);

    m_pipeline->writeToDot(this,
                           QString("setSource-complete-%0")
                           .arg(QUrl::toPercentEncoding(source.mrl().toString()).constData()));

    // We need to link this node to ensure that fake sinks are connected
    // before loading, otherwise the stream will be blocked
    link();
    beginLoad();
}

void MediaObject::beginLoad()
{
    if (m_pipeline->setState(GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE) {
        m_backend->logMessage("Begin source load", Backend::Info, this);
    } else {
        setError(tr("Could not open media source."));
    }
}

// Called when we are ready to leave the loading state
void MediaObject::loadingComplete()
{
    if (m_pipeline->videoIsAvailable()) {
        MediaNodeEvent event(MediaNodeEvent::VideoAvailable);
        notify(&event);
    }
    getStreamInfo();
    m_loading = false;

    setState(m_pendingState);
    emit metaDataChanged(metaData());
}

void MediaObject::updateNavigation()
{
    m_pipeline->updateNavigation();
}

void MediaObject::getStreamInfo()
{
    updateSeekable();
    updateTotalTime();
    updateNavigation();

    if (m_pipeline->videoIsAvailable() != m_hasVideo) {
        m_hasVideo = m_pipeline->videoIsAvailable();
        emit hasVideoChanged(m_hasVideo);
    }

    if (m_source.discType() == Phonon::Cd) {
        gint64 titleCount;
        GstFormat format = gst_format_get_by_nick("track");
        if (m_pipeline->queryDuration(&format, &titleCount)) {
        //check if returned format is still "track",
        //gstreamer sometimes returns the total time, if tracks information is not available.
            if (qstrcmp(gst_format_get_name(format), "track") == 0)  {
                int oldAvailableTitles = m_availableTitles;
                m_availableTitles = (int)titleCount;
                if (m_availableTitles != oldAvailableTitles) {
                    emit availableTitlesChanged(m_availableTitles);
                    m_backend->logMessage(QString("Available titles changed: %0").arg(m_availableTitles), Backend::Info, this);
                }
            }
        }
    }
}

void MediaObject::setPrefinishMark(qint32 newPrefinishMark)
{
    m_prefinishMark = newPrefinishMark;
    if (currentTime() < totalTime() - m_prefinishMark) // not about to finish
        m_prefinishMarkReachedNotEmitted = true;
}

void MediaObject::pause()
{
    m_backend->logMessage("pause()", Backend::Info, this);
    if (state() != Phonon::PausedState)
        setState(Phonon::PausedState);
    m_resumeState = false;
}

void MediaObject::stop()
{
    if (state() != Phonon::StoppedState) {
        setState(Phonon::StoppedState);
        m_prefinishMarkReachedNotEmitted = true;
    }
    m_resumeState = false;
}

void MediaObject::seek(qint64 time)
{
    if (!isValid())
        return;

    if (isSeekable()) {
        switch (state()) {
        case Phonon::PlayingState:
        case Phonon::StoppedState:
        case Phonon::PausedState:
        case Phonon::BufferingState:
            m_backend->logMessage(QString("Seek to pos %0").arg(time), Backend::Info, this);

            if (time <= 0)
                m_atStartOfStream = true;
            else
                m_atStartOfStream = false;

            m_posAtSeek = getPipelinePos();
            m_tickTimer->stop();

            if (gst_element_seek(m_pipeline->element(), 1.0, GST_FORMAT_TIME,
                                 GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET,
                                 time * GST_MSECOND, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE))
            break;
        case Phonon::LoadingState:
        case Phonon::ErrorState:
            return;
        }

        quint64 current = currentTime();
        quint64 total = totalTime();

        if (current < total - m_prefinishMark)
            m_prefinishMarkReachedNotEmitted = true;
        if (current < total - ABOUT_TO_FINNISH_TIME)
            m_aboutToFinishEmitted = false;
        m_atEndOfStream = false;
    }
}

void MediaObject::emitTick()
{
    if (m_resumeState) {
        return;
    }

    qint64 currentTime = getPipelinePos();
    qint64 totalTime = m_totalTime;
    // We don't get any other kind of notification when we change DVD chapters, so here's the best place...
    updateTotalTime();

    if (m_tickInterval > 0 && currentTime != m_previousTickTime) {
        emit tick(currentTime);
        m_previousTickTime = currentTime;
    }
    if (m_state == Phonon::PlayingState) {
        if (currentTime >= totalTime - m_prefinishMark) {
            if (m_prefinishMarkReachedNotEmitted) {
                m_prefinishMarkReachedNotEmitted = false;
                emit prefinishMarkReached(totalTime - currentTime);
            }
        }
        // Prepare load of next source
        if (currentTime >= totalTime - ABOUT_TO_FINNISH_TIME) {
            if (m_source.type() == MediaSource::Disc &&
                m_autoplayTitles &&
                m_availableTitles > 1 &&
                m_currentTitle < m_availableTitles) {
                m_aboutToFinishEmitted = false;
            } else if (!m_aboutToFinishEmitted) {
                m_aboutToFinishEmitted = true; // track is about to finish
                emit aboutToFinish();
            }
        }
    }
}

/**
 * Triggers playback after a song has completed in the current media queue
 */
void MediaObject::beginPlay()
{
    setSource(m_nextSource);
    m_nextSource = MediaSource();
    m_pendingState = Phonon::PlayingState;
}

void MediaObject::handleStateChange(GstState oldState, GstState newState)
{

    m_posAtSeek = -1;

    switch (newState) {

    case GST_STATE_PLAYING :
        m_atStartOfStream = false;
        m_backend->logMessage("gstreamer: pipeline state set to playing", Backend::Info, this);
        m_tickTimer->start();
        changeState(Phonon::PlayingState);
        if ((m_source.type() == MediaSource::Disc) && (m_currentTitle != m_pendingTitle)) {
            setTrack(m_pendingTitle);
        }
        if (m_resumeState && m_oldState == Phonon::PlayingState) {
            seek(m_oldPos);
            m_resumeState = false;
        }
        break;

    case GST_STATE_NULL:
        m_backend->logMessage("gstreamer: pipeline state set to null", Backend::Info, this);
        m_tickTimer->stop();
        break;

    case GST_STATE_PAUSED :
        m_backend->logMessage("gstreamer: pipeline state set to paused", Backend::Info, this);
        m_tickTimer->start();
        if (state() == Phonon::LoadingState) {
            loadingComplete();
        } else if (m_resumeState && m_oldState == Phonon::PausedState) {
            changeState(Phonon::PausedState);
            m_resumeState = false;
            break;
        } else {
            // A lot of autotests can break if we allow all paused changes through.
            if (m_pendingState == Phonon::PausedState) {
                changeState(Phonon::PausedState);
            }
        }
        break;

    case GST_STATE_READY :
        if (!m_loading && m_pendingState == Phonon::StoppedState)
            changeState(Phonon::StoppedState);
        m_backend->logMessage("gstreamer: pipeline state set to ready", Backend::Debug, this);
        m_tickTimer->stop();
        if ((m_source.type() == MediaSource::Disc) && (m_currentTitle != m_pendingTitle)) {
            setTrack(m_pendingTitle);
        }
        break;

    case GST_STATE_VOID_PENDING :
        m_backend->logMessage("gstreamer: pipeline state set to pending (void)", Backend::Debug, this);
        m_tickTimer->stop();
        break;
    }
}

void MediaObject::handleEndOfStream()
{
    m_backend->logMessage("EOS received", Backend::Info, this);
    // If the stream is not seekable ignore
    // otherwise chained radio broadcasts would stop

    if (m_atEndOfStream)
        return;

    if (!m_seekable)
        m_atEndOfStream = true;

    if (m_source.type() == MediaSource::Disc &&
        m_autoplayTitles &&
        m_availableTitles > 1 &&
        m_currentTitle < m_availableTitles) {
        _iface_setCurrentTitle(m_currentTitle + 1);
        return;
    }

    if (m_nextSource.type() != MediaSource::Invalid
        && m_nextSource.type() != MediaSource::Empty) {  // We only emit finish when the queue is actually empty
        QTimer::singleShot (qMax(0, transitionTime()), this, SLOT(beginPlay()));
    } else {
        m_pendingState = Phonon::PausedState;
        emit finished();
        if (!m_seekable) {
            setState(Phonon::StoppedState);
            // Note the behavior for live streams is not properly defined
            // But since we cant seek to 0, we don't have much choice other than stopping
            // the stream
        } else {
            // Only emit paused if the finished signal
            // did not result in a new state
            if (m_pendingState == Phonon::PausedState)
                setState(m_pendingState);
        }
    }
}

void MediaObject::invalidateGraph()
{
    m_resetNeeded = true;
    if (m_state == Phonon::PlayingState || m_state == Phonon::PausedState) {
        changeState(Phonon::StoppedState);
    }
}

// Notifes the pipeline about state changes in the media object
void MediaObject::notifyStateChange(Phonon::State newstate, Phonon::State oldstate)
{
    Q_UNUSED(oldstate);
    MediaNodeEvent event(MediaNodeEvent::StateChanged, &newstate);
    notify(&event);
}

#ifndef QT_NO_PHONON_MEDIACONTROLLER
//interface management
bool MediaObject::hasInterface(Interface iface) const
{
    return iface == AddonInterface::TitleInterface || iface == AddonInterface::NavigationInterface;
}

QVariant MediaObject::interfaceCall(Interface iface, int command, const QList<QVariant> &params)
{
    if (hasInterface(iface)) {

        switch (iface)
        {
        case TitleInterface:
            switch (command)
            {
            case availableTitles:
                return _iface_availableTitles();
            case title:
                return _iface_currentTitle();
            case setTitle:
                _iface_setCurrentTitle(params.first().toInt());
                break;
            case autoplayTitles:
                return m_autoplayTitles;
            case setAutoplayTitles:
                m_autoplayTitles = params.first().toBool();
                break;
            }
            break;
                default:
            break;
        case NavigationInterface:
            switch(command)
            {
                case availableMenus:
                    return QVariant::fromValue<QList<MediaController::NavigationMenu> >(_iface_availableMenus());
                case setMenu:
                    _iface_jumpToMenu(params.first().value<Phonon::MediaController::NavigationMenu>());
                    break;
            }
            break;
        }
    }
    return QVariant();
}
#endif

QList<MediaController::NavigationMenu> MediaObject::_iface_availableMenus() const
{
    return m_pipeline->availableMenus();
}

void MediaObject::_iface_jumpToMenu(MediaController::NavigationMenu menu)
{
#if GST_VERSION >= GST_VERSION_CHECK(0,10,23,0)
    GstNavigationCommand command;
    switch(menu) {
    case MediaController::RootMenu:
        command = GST_NAVIGATION_COMMAND_DVD_ROOT_MENU;
        break;
    case MediaController::TitleMenu:
        command = GST_NAVIGATION_COMMAND_DVD_TITLE_MENU;
        break;
    case MediaController::AudioMenu:
        command = GST_NAVIGATION_COMMAND_DVD_AUDIO_MENU;
        break;
    case MediaController::SubtitleMenu:
        command = GST_NAVIGATION_COMMAND_DVD_SUBPICTURE_MENU;
        break;
    case MediaController::ChapterMenu:
        command = GST_NAVIGATION_COMMAND_DVD_CHAPTER_MENU;
        break;
    case MediaController::AngleMenu:
        command = GST_NAVIGATION_COMMAND_DVD_ANGLE_MENU;
        break;
    default:
        return;
    }

    GstElement *target = gst_bin_get_by_interface(GST_BIN(m_pipeline->element()), GST_TYPE_NAVIGATION);
    if (target)
        gst_navigation_send_command(GST_NAVIGATION(target), command);
#endif
}

int MediaObject::_iface_availableTitles() const
{
    return m_availableTitles;
}

int MediaObject::_iface_currentTitle() const
{
    return m_currentTitle;
}

void MediaObject::_iface_setCurrentTitle(int title)
{
    m_backend->logMessage(QString("setCurrentTitle %0").arg(title), Backend::Info, this);
    if ((title == m_currentTitle) || (title == m_pendingTitle))
        return;

    m_pendingTitle = title;

    if (m_state == Phonon::PlayingState || m_state == Phonon::StoppedState) {
        setTrack(m_pendingTitle);
    } else {
        setState(Phonon::StoppedState);
    }
}

void MediaObject::setTrack(int title)
{
    if (((m_state != Phonon::PlayingState) && (m_state != Phonon::StoppedState)) || (title < 1) || (title > m_availableTitles))
        return;

    //let's seek to the beginning of the song
    GstFormat trackFormat = gst_format_get_by_nick("track");
    m_backend->logMessage(QString("setTrack %0").arg(title), Backend::Info, this);
    if (gst_element_seek_simple(m_pipeline->element(), trackFormat, GST_SEEK_FLAG_FLUSH, title - 1)) {
        m_currentTitle = title;
        updateTotalTime();
        m_atEndOfStream = false;
        emit titleChanged(title);
        emit totalTimeChanged(totalTime());
    }
}

void MediaObject::logWarning(const QString &msg)
{
    m_backend->logMessage(msg, Backend::Warning);
}

void MediaObject::handleBuffering(int percent)
{
    m_backend->logMessage(QString("Stream buffering %0").arg(percent), Backend::Debug, this);
    if (m_state != Phonon::BufferingState)
        emit stateChanged(m_state, Phonon::BufferingState);
    else if (percent == 100)
        emit stateChanged(Phonon::BufferingState, m_state);
}

QMultiMap<QString, QString> MediaObject::metaData()
{
    return m_pipeline->metaData();
}

void MediaObject::setMetaData(QMultiMap<QString, QString> newData)
{
    m_pipeline->setMetaData(newData);
}

void MediaObject::handleMouseOverChange(bool active)
{
    MediaNodeEvent mouseOverEvent(MediaNodeEvent::VideoMouseOver, &active);
    notify(&mouseOverEvent);
}

} // ns Gstreamer
} // ns Phonon

QT_END_NAMESPACE

#include "moc_mediaobject.cpp"
