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
#include "debug.h"
#include "gsthelper.h"
#include "pipeline.h"

#include <QtCore/QByteRef>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtGui/QApplication>
#include <QtGui/QFont>
#include <phonon/GlobalDescriptionContainer>

#define ABOUT_TO_FINNISH_TIME 2000
#define MAX_QUEUE_TIME 20 * GST_SECOND
#define GST_PLAY_FLAG_TEXT (1 << 2)

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
        , m_state(Phonon::StoppedState)
        , m_pendingState(Phonon::LoadingState)
        , m_tickTimer(new QTimer(this))
        , m_prefinishMark(0)
        , m_transitionTime(0)
        , m_isStream(false)
        , m_prefinishMarkReachedNotEmitted(true)
        , m_aboutToFinishEmitted(false)
        , m_loading(false)
        , m_totalTime(-1)
        , m_error(Phonon::NoError)
        , m_pipeline(0)
        , m_autoplayTitles(true)
        , m_availableTitles(0)
        , m_currentTitle(1)
        , m_currentSubtitle(0, QHash<QByteArray, QVariant>())
        , m_pendingTitle(0)
        , m_waitingForNextSource(false)
        , m_waitingForPreviousSource(false)
        , m_skippingEOS(false)
        , m_doingEOS(false)
        , m_skipGapless(false)
        , m_handlingAboutToFinish(false)
{
    qRegisterMetaType<GstCaps*>("GstCaps*");
    qRegisterMetaType<State>("State");
    qRegisterMetaType<GstMessage*>("GstMessage*");

    static int count = 0;
    m_name = "MediaObject" + QString::number(count++);

    m_root = this;
    m_pipeline = new Pipeline(this);
    m_isValid = true;
    GlobalSubtitles::instance()->register_(this);

    connect(m_pipeline, SIGNAL(aboutToFinish()),
            this, SLOT(handleAboutToFinish()), Qt::DirectConnection);
    connect(m_pipeline, SIGNAL(eos()),
            this, SLOT(handleEndOfStream()));
    connect(m_pipeline, SIGNAL(warning(QString)),
            this, SLOT(logWarning(QString)));
    connect(m_pipeline, SIGNAL(durationChanged(qint64)),
            this, SLOT(handleDurationChange(qint64)));
    connect(m_pipeline, SIGNAL(buffering(int)),
            this, SIGNAL(bufferStatus(int)));
    connect(m_pipeline, SIGNAL(stateChanged(GstState,GstState)),
            this, SLOT(handleStateChange(GstState,GstState)));
    connect(m_pipeline, SIGNAL(errorMessage(QString,Phonon::ErrorType)),
            this, SLOT(setError(QString,Phonon::ErrorType)));
    connect(m_pipeline, SIGNAL(metaDataChanged(QMultiMap<QString,QString>)),
            this, SIGNAL(metaDataChanged(QMultiMap<QString,QString>)));
    connect(m_pipeline, SIGNAL(availableMenusChanged(QList<MediaController::NavigationMenu>)),
            this, SIGNAL(availableMenusChanged(QList<MediaController::NavigationMenu>)));
    connect(m_pipeline, SIGNAL(videoAvailabilityChanged(bool)),
            this, SIGNAL(hasVideoChanged(bool)));
    connect(m_pipeline, SIGNAL(seekableChanged(bool)),
            this, SIGNAL(seekableChanged(bool)));
    connect(m_pipeline, SIGNAL(streamChanged()),
            this, SLOT(handleStreamChange()));

    connect(m_pipeline, SIGNAL(textTagChanged(int)),
            this, SLOT(getSubtitleInfo(int)));
    connect(m_pipeline, SIGNAL(trackCountChanged(int)),
            this, SLOT(handleTrackCountChange(int)));

    connect(m_tickTimer, SIGNAL(timeout()), SLOT(emitTick()));
}

MediaObject::~MediaObject()
{
    if (m_pipeline) {
        GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline->element()));
        g_signal_handlers_disconnect_matched(bus, G_SIGNAL_MATCH_DATA, 0, 0, 0, 0, this);
        gst_object_unref(bus);
        delete m_pipeline;
    }
    GlobalSubtitles::instance()->unregister_(this);
}

void MediaObject::saveState()
{
    //Only first resumeState is respected
    if (m_resumeState)
        return;

    if (m_state == Phonon::PlayingState || m_state == Phonon::PausedState) {
        m_resumeState = true;
        m_oldState = m_state;
        m_oldPos = getPipelinePos();
    }
}

void MediaObject::resumeState()
{
    if (m_resumeState) {
        m_resumeState = false;
        requestState(m_oldState);
        seek(m_oldPos);
    }
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
    return m_pipeline->videoIsAvailable();
}

/**
 * !reimp
 */
bool MediaObject::isSeekable() const
{
    return m_pipeline->isSeekable();
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
    DEBUG_BLOCK;
    requestState(Phonon::PlayingState);
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

void MediaObject::setError(const QString &errorString, Phonon::ErrorType error)
{
    DEBUG_BLOCK;
    debug() << errorString;
    m_errorString = errorString;
    m_error = error;
    // Perform this asynchronously because this is also called from within the pipeline's cb_error
    // handler, which can cause deadlock once we finally get down to gst_element_set_state
    QMetaObject::invokeMethod(this, "requestState", Qt::QueuedConnection, Q_ARG(Phonon::State, Phonon::ErrorState));
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

void MediaObject::changeSubUri(const Mrl &mrl)
{
    QString fontDesc;
    QByteArray customFont = qgetenv("PHONON_SUBTITLE_FONT");
    QByteArray customEncoding = qgetenv("PHONON_SUBTITLE_ENCODING");

    if (customFont.isNull()) {
        QFont videoWidgetFont = QApplication::font("VideoWidget");
        fontDesc = videoWidgetFont.family() + " " + QString::number(videoWidgetFont.pointSize());
    }
    //FIXME: Try to detect common encodings, like libvlc does
    g_object_set(G_OBJECT(m_pipeline->element()), "suburi", mrl.toEncoded().constData(),
        "subtitle-font-desc", customFont.isNull() ? fontDesc.toStdString().c_str() : customFont.constData(),
        "subtitle-encoding", customEncoding.isNull() ? "UTF-8" : customEncoding.constData(), NULL);
}

void MediaObject::autoDetectSubtitle()
{
    if (m_source.type() == MediaSource::LocalFile ||
       (m_source.type() == MediaSource::Url && m_source.mrl().scheme() == "file") ) {

        QList<QLatin1String> exts = QList<QLatin1String>()
            << QLatin1String("sub") << QLatin1String("srt")
            << QLatin1String("smi") << QLatin1String("ssa")
            << QLatin1String("ass") << QLatin1String("asc");

        // Remove the file extension
        QString absCompleteBaseName = m_source.fileName();
        absCompleteBaseName.replace(QFileInfo(absCompleteBaseName).suffix(), "");

        // Looking for a subtitle in the same directory and matching the same name
        foreach(QLatin1String ext, exts) {
            if (QFile::exists(absCompleteBaseName + ext)) {
                changeSubUri(Mrl("file://" + absCompleteBaseName + ext));
                break;
            }
        }
    }
}

void MediaObject::setNextSource(const MediaSource &source)
{
    DEBUG_BLOCK;

    m_aboutToFinishLock.lock();
    if (m_handlingAboutToFinish) {
        debug() << "Got next source. Waiting for end of current.";

        // If next source is valid and is not empty (an empty source is sent by Phonon if
        // there are no more sources) skip EOS for the current source in order to seamlessly
        // pass to the next source.
        if (source.type() == Phonon::MediaSource::Invalid ||
            source.type() == Phonon::MediaSource::Empty)
            m_skippingEOS = false;
        else
            m_skippingEOS = true;

        m_waitingForNextSource = true;
        m_waitingForPreviousSource = false;
        m_skipGapless = false;
        m_pipeline->setSource(source);
        m_aboutToFinishWait.wakeAll();
    } else
        qDebug() << "Ignoring source as no aboutToFinish handling is in progress.";
    m_aboutToFinishLock.unlock();
}

qint64 MediaObject::getPipelinePos() const
{
    Q_ASSERT(m_pipeline);

    // Note some formats (usually mpeg) do not allow us to accurately seek to the
    // beginning or end of the file so we 'fake' it here rather than exposing the front end to potential issues.
    //
    return m_pipeline->position();
}

/*
 * !reimp
 */
void MediaObject::setSource(const MediaSource &source)
{
    DEBUG_BLOCK;

    if (source.type() == Phonon::MediaSource::Invalid) {
        qWarning("Trying to set an invalid MediaSource -> ignoring.");
        return;
    }

    debug() << "Setting new source";
    m_source = source;
    autoDetectSubtitle();
    m_pipeline->setSource(source);
    m_skipGapless = false;
    m_aboutToFinishWait.wakeAll();
    //emit currentSourceChanged(source);
}

// Called when we are ready to leave the loading state
void MediaObject::loadingComplete()
{
    DEBUG_BLOCK;
    link();
}

void MediaObject::getSubtitleInfo(int stream)
{
    gint spuCount = 0; // Sub picture units.
    g_object_get(G_OBJECT(m_pipeline->element()), "n-text", &spuCount, NULL);
    if (spuCount)
        GlobalSubtitles::instance()->add(this, -1, tr("Disable"), "");
    for (gint i = 0; i < spuCount; ++i) {
        GstTagList *tags = 0;
        g_signal_emit_by_name (G_OBJECT(m_pipeline->element()), "get-text-tags",
                               i, &tags);

        if (tags) {
            gchar *tagLangCode = 0;
            gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &tagLangCode);
            QString name;
            if (tagLangCode)
                name = QLatin1String(tagLangCode); // Language code is ISO -> always Latin1
            else
                name = tr("Unknown");
            GlobalSubtitles::instance()->add(this, i, name);
            // tagLangCode was implicat converted to QString, so we can drop
            // the ref.
            g_free(tagLangCode);
        }
    }
    emit availableSubtitlesChanged();
}

void MediaObject::setPrefinishMark(qint32 newPrefinishMark)
{
    m_prefinishMark = newPrefinishMark;
    if (currentTime() < totalTime() - m_prefinishMark) // not about to finish
        m_prefinishMarkReachedNotEmitted = true;
}

void MediaObject::pause()
{
    DEBUG_BLOCK;
    requestState(Phonon::PausedState);
}

void MediaObject::stop()
{
    DEBUG_BLOCK;
    requestState(Phonon::StoppedState);
}

void MediaObject::seek(qint64 time)
{
    DEBUG_BLOCK;

    if (m_waitingForNextSource) {
        debug() << "Seeking back within old source";
        m_waitingForNextSource = false;
        m_waitingForPreviousSource = true;
        m_pipeline->setSource(m_source, true);
    }
    m_pipeline->seekToMSec(time);
    m_lastTime = 0;
}

void MediaObject::handleStreamChange()
{
    if (m_waitingForPreviousSource) {
        m_waitingForPreviousSource = false;
    } else {
        m_source = m_pipeline->currentSource();
        m_sourceMeta = m_pipeline->metaData();
        m_waitingForNextSource = false;
        emit metaDataChanged(m_pipeline->metaData());
        emit currentSourceChanged(m_pipeline->currentSource());
    }
}

void MediaObject::handleDurationChange(qint64 duration)
{
    m_totalTime = duration;
    emit totalTimeChanged(duration);
}

void MediaObject::emitTick()
{
    if (m_resumeState) {
        return;
    }

    qint64 currentTime = getPipelinePos();
    // We don't get any other kind of notification when we change DVD chapters, so here's the best place...
    // TODO: Verify that this is fixed with playbin2 and that we don't need to manually update the
    // time when playing a DVD.
    //updateTotalTime();
    emit tick(currentTime);

    if (m_state == Phonon::PlayingState) {
        if (currentTime >= totalTime() - m_prefinishMark) {
            if (m_prefinishMarkReachedNotEmitted) {
                m_prefinishMarkReachedNotEmitted = false;
                emit prefinishMarkReached(totalTime() - currentTime);
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

Phonon::State MediaObject::translateState(GstState state) const
{
    switch (state) {
        case GST_STATE_PLAYING:
            return Phonon::PlayingState;
        case GST_STATE_PAUSED:
            return Phonon::PausedState;
        case GST_STATE_READY:
            return Phonon::StoppedState;
        case GST_STATE_NULL:
            return Phonon::LoadingState;
        case GST_STATE_VOID_PENDING: // Quiet GCC
            break;
    }
    return Phonon::ErrorState;
}

void MediaObject::handleStateChange(GstState oldState, GstState newState)
{
    DEBUG_BLOCK;

    Phonon::State prevPhononState = m_state;
    prevPhononState = translateState(oldState);
    m_state = translateState(newState);
    debug() << "Moving from" << GstHelper::stateName(oldState) << prevPhononState << "to" << GstHelper::stateName(newState) << m_state;
    if (GST_STATE_TRANSITION(oldState, newState) == GST_STATE_CHANGE_NULL_TO_READY)
        loadingComplete();
    if (GST_STATE_TRANSITION(oldState, newState) == GST_STATE_CHANGE_READY_TO_PAUSED && m_pendingTitle != 0) {
        _iface_setCurrentTitle(m_pendingTitle);
    }
    if (newState == GST_STATE_PLAYING)
        m_tickTimer->start();
    else
        m_tickTimer->stop();

    if (newState == GST_STATE_READY)
        emit tick(0);

    // Avoid signal emission while processing EOS to avoid bogus UI updates.
    if (!m_doingEOS)
        emit stateChanged(m_state, prevPhononState);
}

void MediaObject::handleEndOfStream()
{
    DEBUG_BLOCK;
    if (!m_skippingEOS) {
        debug() << "not skipping EOS";
        m_doingEOS = true;
        { // When working on EOS we do not want signals emitted to avoid bogus UI updates.
            emit stateChanged(Phonon::StoppedState, m_state);
            m_aboutToFinishWait.wakeAll();m_aboutToFinishLock.unlock();
            m_pipeline->setState(GST_STATE_READY);
            emit finished();
        }
        m_doingEOS = false;
    } else {
        debug() << "skipping EOS";
        GstState state = m_pipeline->state();
        m_pipeline->setState(GST_STATE_READY);
        m_pipeline->setState(state);
        m_skippingEOS = false;
    }
}

#ifndef QT_NO_PHONON_MEDIACONTROLLER
//interface management
bool MediaObject::hasInterface(Interface iface) const
{
    return iface == AddonInterface::TitleInterface || iface == AddonInterface::NavigationInterface
        || iface == AddonInterface::SubtitleInterface;
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
        case SubtitleInterface:
            switch(command)
            {
                case availableSubtitles:
                    return QVariant::fromValue(_iface_availableSubtitles());
                    break;
                case currentSubtitle:
                    return QVariant::fromValue(_iface_currentSubtitle());
                    break;
                case setCurrentSubtitle:
                    if (params.isEmpty() || !params.first().canConvert<SubtitleDescription>()) {
                        error() << Q_FUNC_INFO << "arguments invalid";
                        return QVariant();
                    }
                    _iface_setCurrentSubtitle(params.first().value<SubtitleDescription>());
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

void MediaObject::handleTrackCountChange(int tracks)
{
    m_backend->logMessage(QString("handleTrackCountChange %0").arg(tracks), Backend::Info, this);

    int old_availableTitles = m_availableTitles;
    m_availableTitles = tracks;
    if (old_availableTitles != m_availableTitles) {
        emit availableTitlesChanged(m_availableTitles);
    }
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
    if (m_source.discType() == Phonon::NoDisc || title == m_currentTitle) {
        return;
    }
    m_backend->logMessage(QString("setCurrentTitle %0").arg(title), Backend::Info, this);
    QString format = m_source.discType() == Phonon::Cd ? "track" : "title";
    m_pendingTitle = title;

    switch (m_state) {
        case Phonon::PlayingState:
        case Phonon::PausedState:
            changeTitle(format, m_pendingTitle);
            break;
        default:
            break;
    }
    if (m_currentTitle == m_pendingTitle)
        m_pendingTitle = 0;
}

QList<SubtitleDescription> MediaObject::_iface_availableSubtitles() const
{
    return GlobalSubtitles::instance()->listFor(this);
}

SubtitleDescription MediaObject::_iface_currentSubtitle() const
{
    return m_currentSubtitle;
}

void MediaObject::_iface_setCurrentSubtitle(const SubtitleDescription &subtitle)
{
    if (subtitle.property("type").toString() == "file") {
        QString filename = subtitle.name();

        if (!filename.startsWith("file://"))
            filename.prepend("file://");
        // It's not possible to change the suburi when the pipeline is PLAYING mainly
        // because the pipeline has not been built with the subtitle element. A workaround
        // consists to restart the pipeline and set the suburi property (totem does exactly the same thing)
        // TODO: Harald suggests to insert a empty bin into the playbin2 pipeline and then insert a subtitle element
        // on the fly into that bin when the subtitle feature is required...
        stop();
        changeSubUri(Mrl(filename));
        play();
        m_currentSubtitle = subtitle;
        GlobalSubtitles::instance()->add(this, m_currentSubtitle);
        emit availableSubtitlesChanged();
    } else {
        const int localIndex = GlobalSubtitles::instance()->localIdFor(this, subtitle.index());
        int flags;

        g_object_get (G_OBJECT(m_pipeline->element()), "flags", &flags, NULL);
        if (localIndex == -1) {
            flags &= ~GST_PLAY_FLAG_TEXT;
        } else {
            flags |= GST_PLAY_FLAG_TEXT;
        }
        g_object_set(G_OBJECT(m_pipeline->element()), "flags", flags, "current-text", localIndex, NULL);
        m_currentSubtitle = subtitle;
    }
}

void MediaObject::changeTitle(const QString &format, int title)
{
    if ((title < 1) || (title > m_availableTitles))
        return;

    //let's seek to the beginning of the song
    GstFormat titleFormat = gst_format_get_by_nick(format.toLocal8Bit().constData());

    if (!titleFormat)
        return;

    m_backend->logMessage(QString("changeTitle %0 %1").arg(format).arg(title), Backend::Info, this);
    if (gst_element_seek_simple(m_pipeline->element(), titleFormat, GST_SEEK_FLAG_FLUSH, title - 1)) {
        m_currentTitle = title;
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
    Q_ASSERT(0);
    m_backend->logMessage(QString("Stream buffering %0").arg(percent), Backend::Debug, this);
    if (m_state != Phonon::BufferingState)
        emit stateChanged(m_state, Phonon::BufferingState);
    else if (percent == 100)
        emit stateChanged(Phonon::BufferingState, m_state);
}

QMultiMap<QString, QString> MediaObject::metaData()
{
    return m_sourceMeta;
}

void MediaObject::setMetaData(QMultiMap<QString, QString> newData)
{
    m_pipeline->setMetaData(newData);
}

void MediaObject::requestState(Phonon::State state)
{
    DEBUG_BLOCK;
    // Only abort handling here iff the handler is active.
    if (m_aboutToFinishLock.tryLock()) {
        // Note that this is not condition to unlocking, so the nesting is
        // necessary.
        if (m_handlingAboutToFinish) {
            qDebug() << "Aborting aboutToFinish handling.";
            m_skipGapless = true;
            m_aboutToFinishWait.wakeAll();
        }
        m_aboutToFinishLock.unlock();
    }
    debug() << state;
    switch (state) {
        case Phonon::PlayingState:
            m_pipeline->setState(GST_STATE_PLAYING);
            break;
        case Phonon::PausedState:
            m_pipeline->setState(GST_STATE_PAUSED);
            break;
        case Phonon::StoppedState:
            m_pipeline->setState(GST_STATE_READY);
            break;
        case Phonon::ErrorState:
            // Use ErrorState to represent a fatal error
            m_pipeline->setState(GST_STATE_NULL);
            break;
        case Phonon::LoadingState: //Quiet GCC
        case Phonon::BufferingState:
            break;
    }
}

void MediaObject::handleAboutToFinish()
{
    DEBUG_BLOCK;
    debug() << "About to finish";
    m_aboutToFinishLock.lock();
    m_handlingAboutToFinish = true;
    emit aboutToFinish();
    // As our signal gets emitted queued we need to wait here until either a
    // new source or a timeout is reached.
    // If we got a new source in time -> hooray + gapless
    // If we did not get a new source in time -> boooh + stop()
    if (!m_skipGapless) {
        // Dynamic lock timeout is our friend.
        // Instead of simply waiting for a fixed amount of ms for the next source, we wait for the
        // most sensible amount of time. This is whatever amount of time is remaining to play
        // minus a 0.5 seconds safety delta (time values not precise etc.).
        // A source for which we have no time or for which the remaining time is < 0.5 seconds is
        // immediately unlocked again. Otherwise the consumer has as much time as gst gave us to
        // set a new source.
        // This in particular prevents pointless excessive locking on sources which have a totalTime
        // < whatever fixed lock value we have (so that we'd lock longer than what we are playing).
        // An issue apparent with notification-like sounds, that are rather short and do not need
        // gapless transitioning. As outlined in https://bugs.kde.org/show_bug.cgi?id=307530
        unsigned long timeout = 0;
        if (totalTime() <= 0 || (remainingTime() - 500 <= 0))
            timeout = 0;
        else
            timeout = remainingTime() - 500;

        debug() << "waiting for" << timeout;
        if (m_aboutToFinishWait.wait(&m_aboutToFinishLock, timeout)) {
            debug() << "Finally got a source";
            if (m_skipGapless) { // Was explicitly set by stateChange interrupt
                debug() << "...oh, no, just got aborted, skipping EOS";
                m_skippingEOS = false;
            }
        } else {
            warning() << "aboutToFinishWait timed out!";
            m_skippingEOS = false;
        }
    } else {
        debug() << "Skipping gapless audio";
        m_skippingEOS = false;
    }
    m_handlingAboutToFinish = false;
    m_aboutToFinishLock.unlock();
}

} // ns Gstreamer
} // ns Phonon

QT_END_NAMESPACE

#include "moc_mediaobject.cpp"
