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

#include <assert.h>

#include "videosink.h"

G_DEFINE_TYPE(VideoSink, p_gst_video_sink, GST_TYPE_BASE_SINK)

static void p_gst_video_sink_init(PGstVideoSink *sink)
{
    assert(0);
}

static GstFlowReturn p_gst_video_sink_render(GstBaseSink *baseSink, GstBuffer *buffer)
{
    assert(0);
    return GST_FLOW_OK;
}

static void p_gst_video_sink_class_init(PGstVideoSinkClass *klass)
{
    GstBaseSinkClass *baseSinkClass;
    baseSinkClass = (GstBaseSinkClass *)klass;
    baseSinkClass->render = p_gst_video_sink_render;
#warning TODO: caps? preroll?
}
