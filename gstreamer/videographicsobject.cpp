#include "videographicsobject.h"

namespace Phonon {
namespace Gstreamer {

VideoGraphicsObject::VideoGraphicsObject(Backend *backend, QObject *parent) :
    QObject(parent),
    MediaNode(backend, MediaNode::VideoSink)
{
    m_sink = P_GST_VIDEO_SINK(g_object_new(P_GST_TYPE_VIDEO_SINK, NULL));
}

VideoGraphicsObject::~VideoGraphicsObject()
{
}

void VideoGraphicsObject::renderCallback(GstBuffer *buffer, void *userData)
{
    if (!userData)
        return;

    VideoGraphicsObject *that = static_cast<VideoGraphicsObject *>(userData);
    if (!that || !that->videoGraphicsObject())
        return;



}

} // namespace Gstreamer
} // namespace Phonon
