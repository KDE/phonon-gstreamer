/*
    Copyright (C) 2011 Harald Sitter <sitter@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) version 3, or any
    later version accepted by the membership of KDE e.V. (or its
    successor approved by the membership of KDE e.V.), Nokia Corporation
    (or its successors, if any) and the KDE Free Qt Foundation, which shall
    act as a proxy defined in Section 6 of version 3 of the license.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "videographicsobject.h"

#include <gst/video/video.h>

#include "gsthelper.h"

namespace Phonon {
namespace Gstreamer {

VideoGraphicsObject::VideoGraphicsObject(Backend *backend, QObject *parent) :
    QObject(parent),
    MediaNode(backend, MediaNode::VideoSink),
    m_buffer(0)
{
    static int count = 0;
    m_name = "VideoGraphicsObject" + QString::number(count++);

    m_frame.format = VideoFrame::Format_Invalid;

    m_bin = gst_bin_new(NULL);
    gst_object_ref(GST_OBJECT(m_bin));
    gst_object_sink(GST_OBJECT(m_bin));

    m_sink = P_GST_VIDEO_SINK(g_object_new(P_GST_TYPE_VIDEO_SINK, NULL));
    m_sink->userData = this;
    m_sink->stop_cb = VideoGraphicsObject::stop;
    m_sink->render_cb = VideoGraphicsObject::renderCallback;

    GstElement *sink = GST_ELEMENT(m_sink);
    GstElement *queue = gst_element_factory_make("queue", NULL);
    GstElement *convert = gst_element_factory_make("ffmpegcolorspace", NULL);

    GstCaps *caps = p_gst_video_sink_get_static_caps();

    gst_bin_add_many(GST_BIN(m_bin), sink, convert, queue, NULL);
    gst_element_link(queue, convert);
    gst_element_link_filtered(convert, sink, caps);
    gst_caps_unref(caps);

    GstPad *inputpad = gst_element_get_static_pad(queue, "sink");
    gst_element_add_pad(m_bin, gst_ghost_pad_new("sink", inputpad));
    gst_object_unref(inputpad);

    g_object_set(G_OBJECT(sink), "sync", true, NULL);

    m_isValid = true;
}

VideoGraphicsObject::~VideoGraphicsObject()
{
    gst_element_set_state(m_bin, GST_STATE_NULL);
    gst_object_unref(m_bin);
    if (m_buffer)
        gst_buffer_unref(m_buffer);
}

void VideoGraphicsObject::stop(void *userData)
{
    VideoGraphicsObject *that = reinterpret_cast<VideoGraphicsObject *>(userData);
    if (!that)
        return;

    that->m_frame.format = VideoFrame::Format_Invalid;

    QMetaObject::invokeMethod(that, "reset", Qt::QueuedConnection);
}

struct component_t {
    char *data;
    int bytes;
};

void VideoGraphicsObject::renderCallback(GstBuffer *buffer, void *userData)
{
    // No data, no pointer to this -> failure
    if (!buffer || !userData)
        return;

    VideoGraphicsObject *that = reinterpret_cast<VideoGraphicsObject *>(userData);
    if (!that || !that->videoGraphicsObject())
        return;

    // Frontend could hold lock on data
    that->m_mutex.lock();

    // At this point we can do stuff with the data, so we take it over.
    gst_buffer_ref(buffer);
    // Unref the old buffer first...
    if (that->m_buffer)
        gst_buffer_unref(that->m_buffer);
    that->m_buffer = buffer;

    VideoFrame *frame = &that->m_frame;
    frame->width = that->m_sink->width;
    frame->height = that->m_sink->height;
    frame->aspectRatio = static_cast<double>(frame->width / frame->height);

    if (that->m_sink->rgb == FALSE) { // YV12
        // http://gstreamer.freedesktop.org/wiki/RawVideo
        // Y
        struct component_t y;
        y.data = reinterpret_cast<char *>(GST_BUFFER_DATA(buffer));
        y.bytes = frame->width * frame->height;

        // V follows after Y (so start of y + bytes = start of v)
        struct component_t v;
        v.data = y.data + y.bytes;
        v.bytes = (frame->width/2) * (frame->height/2);

        // U follows after V, same size, same relative offset
        struct component_t u;
        u.data = v.data + v.bytes;
        u.bytes = v.bytes; // for YV12/I420 V and U are the same size.

        // If we did everything right then our components collective bytes count
        // should be equal to the buffer's overall size.
        Q_ASSERT((y.bytes + v.bytes + u.bytes) == GST_BUFFER_SIZE(buffer));
        Q_ASSERT(y.data != v.data && v.data != u.data && u.data != y.data);

        frame->format = VideoFrame::Format_YV12;
        frame->planeCount = 3;
        frame->plane[0].setRawData(y.data, y.bytes);
        frame->plane[1].setRawData(v.data, v.bytes);
        frame->plane[2].setRawData(u.data, u.bytes);
    } else { // RGB32
        frame->format = VideoFrame::Format_RGB32;
        frame->planeCount = 1;
        frame->plane[0].setRawData(reinterpret_cast<const char *>(GST_BUFFER_DATA(buffer)),
                                   GST_BUFFER_SIZE(buffer));
    }

    that->m_mutex.unlock();
    emit that->frameReady();
}

void VideoGraphicsObject::lock()
{
    m_mutex.lock();
}

bool VideoGraphicsObject::tryLock()
{
    return m_mutex.tryLock();
}

void VideoGraphicsObject::unlock()
{
    m_mutex.unlock();
}

const VideoFrame *VideoGraphicsObject::frame() const
{
    return &m_frame;
}

} // namespace Gstreamer
} // namespace Phonon
