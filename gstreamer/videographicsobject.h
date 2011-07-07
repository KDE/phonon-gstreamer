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

private:
    Phonon::VideoGraphicsObject *m_frontendObject;
    PGstVideoSink *m_sink;
};

} // namespace Gstreamer
} // namespace Phonon

#endif // PHONON_GSTREAMER_VIDEOGRAPHICSOBJECT_H
