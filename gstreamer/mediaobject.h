/*  This file is part of the KDE project.

    Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
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

#ifndef Phonon_GSTREAMER_MEDIAOBJECT_H
#define Phonon_GSTREAMER_MEDIAOBJECT_H

#include "medianode.h"
#include "pipeline.h"
#include <phonon/mediaobjectinterface.h>
#include <phonon/addoninterface.h>
#include <phonon/objectdescription.h>
#include <phonon/MediaController>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtCore/QWaitCondition>
#include <QtCore/QMutex>

#include "phonon-config-gstreamer.h"

QT_BEGIN_NAMESPACE

class QTimer;
typedef QMultiMap<QString, QString> TagMap;

namespace Phonon
{

class Mrl;

namespace Gstreamer
{

class VideoWidget;
class AudioPath;
class VideoPath;
class AudioOutput;

class MediaObject : public QObject, public MediaObjectInterface
#ifndef QT_NO_PHONON_MEDIACONTROLLER
        , public AddonInterface
#endif
        , public MediaNode
{
    friend class Stream;
    friend class AudioDataOutput;
    Q_OBJECT
    Q_INTERFACES(Phonon::MediaObjectInterface
#ifndef QT_NO_PHONON_MEDIACONTROLLER
                 Phonon::AddonInterface
#endif
                 Phonon::Gstreamer::MediaNode
    )

public:
    MediaObject(Backend *backend, QObject *parent);
    ~MediaObject();
    Phonon::State state() const;

    bool hasVideo() const;
    bool isSeekable() const;

    qint64 currentTime() const;
    qint32 tickInterval() const;

    void setTickInterval(qint32 newTickInterval);

    void play();
    void pause();
    void stop();
    void seek(qint64 time);

    Phonon::State translateState(GstState state) const;

    QString errorString() const;
    Phonon::ErrorType errorType() const;

    qint64 totalTime() const;

    qint32 prefinishMark() const;
    void setPrefinishMark(qint32 newPrefinishMark);

    qint32 transitionTime() const;
    void setTransitionTime(qint32);
    qint64 remainingTime() const;

    void setSource(const MediaSource &source);
    void setNextSource(const MediaSource &source);
    MediaSource source() const;

    // No additional interfaces currently supported
#ifndef QT_NO_PHONON_MEDIACONTROLLER
    bool hasInterface(Interface) const;
    QVariant interfaceCall(Interface, int, const QList<QVariant> &);
#endif
    bool isLoading()
    {
        return m_loading;
    }

    bool audioAvailable()
    {
        return m_pipeline->audioIsAvailable();
    }

    bool videoAvailable()
    {
        return m_pipeline->videoIsAvailable();
    }

    GstElement *audioGraph()
    {
        return m_pipeline->audioGraph();
    }

    GstElement *videoGraph()
    {
        return m_pipeline->videoGraph();
    }

    Pipeline *pipeline()
    {
        return m_pipeline;
    }

    void saveState();
    void resumeState();

    QMultiMap<QString, QString> metaData();
    void setMetaData(QMultiMap<QString, QString> newData);

public Q_SLOTS:
    void requestState(Phonon::State);

Q_SIGNALS:
    void currentSourceChanged(const MediaSource &newSource);
    void stateChanged(Phonon::State newstate, Phonon::State oldstate);
    void tick(qint64 time);
    void metaDataChanged(QMultiMap<QString, QString>);
    void seekableChanged(bool);
    void hasVideoChanged(bool);

    void finished();
    void prefinishMarkReached(qint32);
    void aboutToFinish();
    void totalTimeChanged(qint64 length);
    void bufferStatus(int percentFilled);

    // AddonInterface:
    void titleChanged(int);
    void availableTitlesChanged(int);
    void availableMenusChanged(QList<MediaController::NavigationMenu>);

    // Not implemented
    void chapterChanged(int);
    void availableChaptersChanged(int);
    void angleChanged(int);
    void availableAnglesChanged(int);

    void availableSubtitlesChanged();
    void availableAudioChannelsChanged();

protected:
    void loadingComplete();
    Q_INVOKABLE void setError(const QString &errorString, Phonon::ErrorType error = NormalError);

    GstElement *audioElement()
    {
        return m_pipeline->audioPipe();
    }

    GstElement *videoElement()
    {
        return m_pipeline->videoPipe();
    }

private Q_SLOTS:
    void handleTrackCountChange(int tracks);
    void getSubtitleInfo(int stream);
    void emitTick();
    void beginPlay();
    void autoDetectSubtitle();

    void handleEndOfStream();
    void logWarning(const QString &);
    void handleBuffering(int);
    void handleStateChange(GstState oldState, GstState newState);
    void handleDurationChange(qint64);

    void handleAboutToFinish();
    void handleStreamChange();

private:
    // GStreamer specific :
    void setTotalTime(qint64 newTime);
    qint64 getPipelinePos() const;

    int _iface_availableTitles() const;
    int _iface_currentTitle() const;
    void _iface_setCurrentTitle(int title);
    QList<MediaController::NavigationMenu> _iface_availableMenus() const;
    void _iface_jumpToMenu(MediaController::NavigationMenu menu);
    QList<SubtitleDescription> _iface_availableSubtitles() const;
    SubtitleDescription _iface_currentSubtitle() const;
    void _iface_setCurrentSubtitle(const SubtitleDescription &subtitle);
    void changeTitle(const QString &format, int title);
    void changeSubUri(const Mrl &mrl);

    bool m_resumeState;
    State m_oldState;
    quint64 m_oldPos;

    State m_state;
    State m_pendingState;
    QTimer *m_tickTimer;
    qint32 m_tickInterval;

    MediaSource m_nextSource;
    qint32 m_prefinishMark;
    qint32 m_transitionTime;
    bool m_isStream;

    qint64 m_posAtSeek;

    bool m_prefinishMarkReachedNotEmitted;
    bool m_aboutToFinishEmitted;
    bool m_loading;

    qint64 m_totalTime;
    bool m_atStartOfStream;
    Phonon::ErrorType m_error;
    QString m_errorString;

    Pipeline *m_pipeline;
    bool m_autoplayTitles;
    int m_availableTitles;
    int m_currentTitle;
    SubtitleDescription m_currentSubtitle;
    int m_pendingTitle;

    // When we emit aboutToFinish(), libphonon calls setNextSource. To achive gapless playback,
    // the pipeline is immediately told to start using that new source. This can break seeking
    // since the aboutToFinish signal tends to be emitted around 15 seconds or so prior to actually
    // ending the current track.
    // If we seek backwards in time, we'd still have the 'next' source but now be a different
    // position than the start. This flag tells us to reset the pipeline's source to the 'previous'
    // one prior to seeking.
    //
    // It also causes currentSourceChanged() to be emitted once the duration changes, which happens
    // when we actually *do* start hearing the new source.
    bool m_waitingForNextSource;
    bool m_waitingForPreviousSource;

    bool m_skippingEOS;
    bool m_doingEOS; // To prevent superfluously signal emission.

    // This keeps track of the source currently heard over the speakers.
    // It can be different from the pipeline's current source due to how the
    // almost-at-end gapless playback code works.
    Phonon::MediaSource m_source;
    QMultiMap<QString, QString> m_sourceMeta;

    //This simply pauses the gst signal handler 'till we get something
    QMutex m_aboutToFinishLock;
    QWaitCondition m_aboutToFinishWait;

    qint64 m_lastTime;
    bool m_skipGapless;

    /*** Tracks whereever the MO is actively handling an aboutToFinish CB right now. */
    bool m_handlingAboutToFinish;
};
}
} //namespace Phonon::Gstreamer

QT_END_NAMESPACE

#endif // Phonon_GSTREAMER_MEDIAOBJECT_H
