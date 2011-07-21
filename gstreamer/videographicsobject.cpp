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

    m_bin = gst_bin_new(0);
    gst_object_ref(GST_OBJECT(m_bin));
    gst_object_sink(GST_OBJECT(m_bin));

    m_sink = P_GST_VIDEO_SINK(g_object_new(P_GST_TYPE_VIDEO_SINK, 0));
    m_sink->userData = this;
    m_sink->renderCallback = VideoGraphicsObject::renderCallback;

    GstElement *sink = GST_ELEMENT(m_sink);
    GstElement *queue = gst_element_factory_make("queue", 0);
    GstElement *convert = gst_element_factory_make("ffmpegcolorspace", 0);

    GstCaps *caps = p_gst_video_sink_get_static_caps();

    gst_bin_add_many(GST_BIN(m_bin), sink, convert, queue, 0);
    gst_element_link(queue, convert);
    gst_element_link_filtered(convert, sink, caps);
    gst_caps_unref(caps);

    GstPad *inputpad = gst_element_get_static_pad(queue, "sink");
    gst_element_add_pad(m_bin, gst_ghost_pad_new("sink", inputpad));
    gst_object_unref(inputpad);

    g_object_set(G_OBJECT(sink), "sync", true, (const char*)0);

    m_isValid = true;
}

VideoGraphicsObject::~VideoGraphicsObject()
{
}

void VideoGraphicsObject::renderCallback(GstBuffer *buffer, void *userData)
{
    // No data, no pointer to this -> failure
    if (!buffer || !userData)
        return;

    VideoGraphicsObject *that = static_cast<VideoGraphicsObject *>(userData);
    if (!that || !that->videoGraphicsObject())
        return;

    // Frontend holds lock on data
    if (!that->m_mutex.tryLock()) {
        qWarning("lock fail");
        return;
    }

    // At this point we can do stuff with the data, so we take it over.
    gst_buffer_ref(buffer);
    // Unref the old buffer first...
    if (that->m_buffer)
        gst_buffer_unref(that->m_buffer);
    that->m_buffer = buffer;

    VideoFrame *frame = &that->m_frame;
    GstStructure *structure = gst_caps_get_structure(GST_BUFFER_CAPS(buffer), 0);
    gst_structure_get_int(structure, "width", &frame->width);
    gst_structure_get_int(structure, "height", &frame->height);
    frame->aspectRatio =
            static_cast<double>(frame->width/frame->height);

    frame->format = VideoFrame::Format_RGB32;
    // RGB888 Means the data is 8 bits o' red, 8 bits o' green, and 8 bits o' blue per pixel.
    frame->data0 =
            QByteArray::fromRawData(
                reinterpret_cast<const char*>(GST_BUFFER_DATA(buffer)),
                GST_BUFFER_SIZE(buffer));
    frame->data1 = 0;
    frame->data2 = 0;

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
