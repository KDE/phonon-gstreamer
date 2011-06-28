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

#include <gst/gst.h>
#include <gst/video/video.h>

#include <assert.h>

#include "videosink.h"

#define UNUSED(x) (void)x

static GstStaticPadTemplate s_rgbPadTemplate =
        GST_STATIC_PAD_TEMPLATE("sink",
                                GST_PAD_SINK,
                                GST_PAD_ALWAYS,
                                GST_STATIC_CAPS(GST_VIDEO_CAPS_xRGB_HOST_ENDIAN));

G_DEFINE_TYPE(PGstVideoSink, p_gst_video_sink, GST_TYPE_BASE_SINK)


static void p_gst_video_sink_init(PGstVideoSink *sink)
{
    assert(0);
}

static GstCaps *p_gst_video_sink_get_caps(GstBaseSink *baseSink)
{
    UNUSED(baseSink);
    return gst_static_pad_template_get_caps(&s_rgbPadTemplate);
}

static GstFlowReturn p_gst_video_sink_render(GstBaseSink *baseSink,
                                             GstBuffer *buffer)
{
    assert(0);

    PGstVideoSink *sink = P_GST_VIDEO_SINK(baseSink);
    if (buffer == NULL || G_UNLIKELY(!GST_IS_BUFFER (buffer))) {
#warning sounds bogus?
        return GST_FLOW_RESEND;
    }

    gst_buffer_ref(buffer);

#warning TODO: do something with the frame

    return GST_FLOW_OK;
}

static void p_gst_video_sink_class_init(PGstVideoSinkClass *klass)
{
    GstBaseSinkClass *baseSinkClass;
    baseSinkClass = (GstBaseSinkClass *)klass;
    baseSinkClass->render = p_gst_video_sink_render;
#warning TODO: caps? preroll?
}
