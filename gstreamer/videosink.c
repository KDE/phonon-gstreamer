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
#include <string.h>

#include "videosink.h"

#define UNUSED(x) (void)x
#define dbg(x) fprintf(stderr, "%s\n", x)

static GstStaticPadTemplate s_sinktemplate =
GST_STATIC_PAD_TEMPLATE ("sink",
                         GST_PAD_SINK,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YV12 }") ";" \
                                          GST_VIDEO_CAPS_xRGB_HOST_ENDIAN));

static GstStaticPadTemplate s_rgbPadTemplate =
GST_STATIC_PAD_TEMPLATE ("sink",
                         GST_PAD_SINK,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS(
                             GST_VIDEO_CAPS_xRGB_HOST_ENDIAN));

static GstStaticPadTemplate s_yuvPadTemplate =
GST_STATIC_PAD_TEMPLATE ("sink",
                         GST_PAD_SINK,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS(
                             GST_VIDEO_CAPS_YUV ("{ I420, YV12 }")));
/*                                    GST_VIDEO_CAPS_YUV ("{ IYUV, I420, YV12 }")));*/

G_DEFINE_TYPE (PGstVideoSink, p_gst_video_sink, GST_TYPE_VIDEO_SINK)

GstCaps *
p_gst_video_sink_get_static_caps()
{
    GstCaps *caps = gst_caps_new_empty();
    char *env = getenv ("PHONON_COLOR");
    if (env == 0 || strcmp (env, "rgb") != 0)
        gst_caps_append (caps, gst_caps_copy (gst_static_pad_template_get_caps (&s_yuvPadTemplate)));
    else if (env == 0 || strcmp (env, "yuv") != 0)
        gst_caps_append (caps, gst_caps_copy (gst_static_pad_template_get_caps (&s_rgbPadTemplate)));
    return caps;
}

static void
p_gst_video_sink_init (PGstVideoSink *sink)
{
    sink->width  = -1;
    sink->height = -1;
    sink->format = NoFormat;
}

static GstCaps
*p_gst_video_sink_get_caps (GstBaseSink *baseSink)
{
    UNUSED(baseSink);
    return p_gst_video_sink_get_static_caps ();
}

static gboolean
p_gst_video_sink_set_caps (GstBaseSink *baseSink, GstCaps *caps)
{
    PGstVideoSink *sink = P_GST_VIDEO_SINK (baseSink);
    GstStructure *structure = gst_caps_get_structure (caps, 0);

    if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0) {
        guint32 fourcc = 0;
        gst_structure_get_fourcc (structure, "format", &fourcc);
        if (fourcc == GST_STR_FOURCC ("I420")) {
            sink->format = I420Format;
            dbg("-----using I420------");
        }
        else if (fourcc == GST_STR_FOURCC ("YV12")) {
            sink->format = YV12Format;
            dbg("-----using YV12------");
        }
    } else if (strcmp (gst_structure_get_name (structure), "video/x-raw-rgb") == 0) {
        sink->format = RGB32Format;
    }

    gst_structure_get_int (structure, "width",  &sink->width);
    gst_structure_get_int (structure, "height", &sink->height);

    return TRUE;
}

static GstFlowReturn
p_gst_video_sink_render (GstBaseSink *baseSink,
                         GstBuffer *buffer)
{
    PGstVideoSink *sink = P_GST_VIDEO_SINK (baseSink);
    if (buffer == NULL || G_UNLIKELY (!GST_IS_BUFFER (buffer))) {
#warning sounds bogus?
        return GST_FLOW_RESEND;
    }

    sink->render_cb(buffer, sink->userData);

    return GST_FLOW_OK;
}

static void
p_gst_video_sink_stop (GstBaseSink *baseSink)
{
    PGstVideoSink *sink = P_GST_VIDEO_SINK (baseSink);
    sink->stop_cb (sink->userData);
    p_gst_video_sink_init (sink);
}

static void
p_gst_video_sink_class_init (PGstVideoSinkClass *klass)
{
    GstBaseSinkClass *baseSinkClass = GST_BASE_SINK_CLASS (klass);
    baseSinkClass->render   = p_gst_video_sink_render;
#warning yeah, right, ehm, needs improvements I guess?
    baseSinkClass->preroll  = p_gst_video_sink_render;
    baseSinkClass->get_caps = p_gst_video_sink_get_caps;
    baseSinkClass->set_caps = p_gst_video_sink_set_caps;
    baseSinkClass->stop = p_gst_video_sink_stop;

    GstElementClass *elementClass = GST_ELEMENT_CLASS (klass);
    gst_element_class_add_pad_template (elementClass,
                                        gst_static_pad_template_get(&s_sinktemplate));
}
