#ifndef PHONON_GSTREAMER_VIDEOGRAPHICSOBJECT_H
#define PHONON_GSTREAMER_VIDEOGRAPHICSOBJECT_H

#include <QtCore/QObject>

#include <phonon/videographicsobject.h>

#include "medianode.h"
#include "videosink.h"

namespace Phonon {
namespace Gstreamer {

class VideoGraphicsObject : public QObject,
                            public VideoGraphicsObjectInterface,
                            public MediaNode
{
    Q_OBJECT
    Q_INTERFACES(Phonon::VideoGraphicsObjectInterface Phonon::Gstreamer::MediaNode)
public:
    explicit VideoGraphicsObject(Backend *backend, QObject *parent = 0);
    ~VideoGraphicsObject();

    virtual Phonon::VideoGraphicsObject *videoGraphicsObject() { return m_frontendObject; }
    virtual void setVideoGraphicsObject(Phonon::VideoGraphicsObject *object) { m_frontendObject = object; }

    static void renderCallback(GstBuffer *buffer, void *userData);

    void lock();
    bool tryLock();
    void unlock();

    const VideoFrame *frame() const;

    GstElement *videoElement()
    {
        qDebug() << "fishy";
        return m_bin;
    }

signals:
    void frameReady();

private:
    Phonon::VideoFrame m_frame;
    Phonon::VideoGraphicsObject *m_frontendObject;

    PGstVideoSink *m_sink;

    QMutex m_mutex;

    GstElement *m_bin;
    GstBuffer *m_buffer;
};

} // namespace Gstreamer
} // namespace Phonon

#endif // PHONON_GSTREAMER_VIDEOGRAPHICSOBJECT_H
