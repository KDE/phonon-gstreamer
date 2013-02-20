/*  This file is part of the KDE project
    Copyright (C) 2010 Trever Fischer <tdfischer@fedoraproject.org>

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

#include "videodataoutput.h"
#include <phonon/experimental/videoframe2.h>
#include "phonon-config-gstreamer.h"

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/gstghostpad.h>
#include <gst/gstutils.h>

namespace Phonon
{
namespace Gstreamer
{

VideoDataOutput::VideoDataOutput(Backend *backend, QObject *parent)
    : QObject(parent),
      MediaNode(backend, VideoSink),
      m_frontend(0)
{
    static int count = 0;
    m_name = "VideoDataOutput" + QString::number(count++);

    m_queue = gst_bin_new(NULL);
    gst_object_ref(GST_OBJECT(m_queue));
#if GST_VERSION < GST_VERSION_CHECK (1,0,0,0)
    gst_object_sink(GST_OBJECT(m_queue));
#else
    gst_object_ref_sink(GST_OBJECT(m_queue));
#endif

    GstElement* sink = gst_element_factory_make("fakesink", NULL);
    GstElement* queue = gst_element_factory_make("queue", NULL);
    GstElement* convert =
#if GST_VERSION < GST_VERSION_CHECK (1,0,0,0)
            gst_element_factory_make("ffmpegcolorspace", NULL);
#else
            gst_element_factory_make("videoconvert", NULL);
#endif

    g_signal_connect(sink, "handoff", G_CALLBACK(processBuffer), this);
    g_object_set(G_OBJECT(sink), "signal-handoffs", true, NULL);

        // Save ourselves a metric crapton of work by simply requesting
        // a format native to Qt.
    GstCaps *caps = gst_caps_new_simple(
            #if GST_VERSION < GST_VERSION_CHECK (1,0,0,0)
                "video/x-raw-rgb",
            #else
                "video/x-raw",
            #endif
                                        "bpp", G_TYPE_INT, 24,
                                        "depth", G_TYPE_INT, 24,
                                        "endianess", G_TYPE_INT, G_BYTE_ORDER,
                                        NULL);

    gst_bin_add_many(GST_BIN(m_queue), sink, convert, queue, NULL);
    gst_element_link(queue, convert);
    gst_element_link_filtered(convert, sink, caps);
    gst_caps_unref(caps);

    GstPad *inputpad = gst_element_get_static_pad(queue, "sink");
    gst_element_add_pad(m_queue, gst_ghost_pad_new("sink", inputpad));
    gst_object_unref(inputpad);

    g_object_set(G_OBJECT(sink), "sync", true, NULL);

    m_isValid = true;
}

VideoDataOutput::~VideoDataOutput()
{
    gst_element_set_state(m_queue, GST_STATE_NULL);
    gst_object_unref(m_queue);
}

void VideoDataOutput::processBuffer(GstElement*, GstBuffer* buffer, GstPad* pad, gpointer gThat)
{
    VideoDataOutput *that = reinterpret_cast<VideoDataOutput*>(gThat);

    GstStructure* structure =
        #if GST_VERSION < GST_VERSION_CHECK (1,0,0,0)
            gst_caps_get_structure(GST_BUFFER_CAPS(buffer), 0);
        #else
            gst_caps_get_structure(gst_pad_get_current_caps(pad), 0);
        #endif
    int width;
    int height;
    double aspect;

    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);
    aspect = (double)width/height;
#if GST_VERSION > GST_VERSION_CHECK (1,0,0,0)
    GstMapInfo *info;
    gst_buffer_map(buffer, info, GST_MAP_READ);
#endif
    const Experimental::VideoFrame2 f = {
        width,
        height,
        aspect,
        Experimental::VideoFrame2::Format_RGB888,
                // RGB888 Means the data is 8 bits o' red, 8 bits o' green, and 8 bits o' blue per pixel.
        QByteArray::fromRawData(reinterpret_cast<const char*>(
#if GST_VERSION < GST_VERSION_CHECK (1,0,0,0)
        GST_BUFFER_DATA(buffer)
#else
        info->data
#endif
        ), 3*width*height),
        0,
        0
    };
    if (that->m_frontend)
        that->m_frontend->frameReady(f);
#if GST_VERSION > GST_VERSION_CHECK (1,0,0,0)
    gst_buffer_unmap(buffer, info);
#endif
}

}} // namespace Phonon::Gstreamer

#include "moc_videodataoutput.cpp"
// vim: sw=4 ts=4
