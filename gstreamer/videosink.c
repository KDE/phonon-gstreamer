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
#define dbg(x) fprintf(stderr, "%s\n", x)

static GstStaticPadTemplate s_sinktemplate =
        GST_STATIC_PAD_TEMPLATE("sink",
                                GST_PAD_SINK,
                                GST_PAD_ALWAYS,
                                GST_STATIC_CAPS (
                                    GST_VIDEO_CAPS_xRGB_HOST_ENDIAN));

static GstStaticPadTemplate s_rgbPadTemplate =
        GST_STATIC_PAD_TEMPLATE("sink",
                                GST_PAD_SINK,
                                GST_PAD_ALWAYS,
                                GST_STATIC_CAPS(
                                    GST_VIDEO_CAPS_xRGB_HOST_ENDIAN));

static GstStaticPadTemplate s_yuvPadTemplate =
        GST_STATIC_PAD_TEMPLATE("sink",
                                GST_PAD_SINK,
                                GST_PAD_ALWAYS,
                                GST_STATIC_CAPS(
                                    GST_VIDEO_CAPS_YUV ("{ IYUV, I420, YV12 }")));


G_DEFINE_TYPE(PGstVideoSink, p_gst_video_sink, GST_TYPE_VIDEO_SINK)

static void p_gst_video_sink_init(PGstVideoSink *sink)
{
    dbg("p_gst_video_sink_init");
    /*    assert(0);*/
}

GstCaps *p_gst_video_sink_get_static_caps()
{
    return gst_static_pad_template_get_caps(&s_rgbPadTemplate);

}

static GstCaps *p_gst_video_sink_get_caps(GstBaseSink *baseSink)
{
    dbg("p_gst_video_sink_get_caps");

    UNUSED(baseSink);
    return gst_static_pad_template_get_caps(&s_rgbPadTemplate);
}

static gboolean p_gst_video_sink_set_caps(GstBaseSink *baseSink, GstCaps *caps)
{
    dbg("p_gst_video_sink_set_caps");
}

static GstFlowReturn p_gst_video_sink_render(GstBaseSink *baseSink,
                                             GstBuffer *buffer)
{
    dbg("p_gst_video_sink_render");

    PGstVideoSink *sink = P_GST_VIDEO_SINK(baseSink);
    if (buffer == NULL || G_UNLIKELY(!GST_IS_BUFFER (buffer))) {
#warning sounds bogus?
        return GST_FLOW_RESEND;
    }

    sink->renderCallback(buffer, sink->userData);

#warning TODO: do something with the frame

    return GST_FLOW_OK;
}

static void p_gst_video_sink_class_init(PGstVideoSinkClass *klass)
{
    dbg("p_gst_video_sink_class_init");

    GstBaseSinkClass *baseSinkClass = GST_BASE_SINK_CLASS(klass);
    baseSinkClass->render   = p_gst_video_sink_render;
    baseSinkClass->preroll  = p_gst_video_sink_render;
    baseSinkClass->get_caps = p_gst_video_sink_get_caps;
    baseSinkClass->set_caps = p_gst_video_sink_set_caps;

    GstElementClass *elementClass = GST_ELEMENT_CLASS(klass);
    gst_element_class_add_pad_template(elementClass,
                                       gst_static_pad_template_get(&s_sinktemplate));
#warning TODO: caps? preroll?
}
