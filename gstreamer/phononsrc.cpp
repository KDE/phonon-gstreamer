/*  This file is part of the KDE project.

    Copyright (C) 2010-2011 Harald Sitter <sitter@kde.org>
    Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2.1 or 3 of the License.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "phononsrc.h"

#include "streamreader.h"

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

static GstStaticPadTemplate srctemplate =
       GST_STATIC_PAD_TEMPLATE ("src",
                                GST_PAD_SRC,
                                GST_PAD_ALWAYS,
                                GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (phonon_src_debug);

// PhononSrc args
enum
{
    ARG_0,
    ARG_PHONONSRC
};

static void phonon_src_finalize(GObject *object);

static void phonon_src_set_property(GObject *object, guint prop_id,
                                    const GValue *value, GParamSpec *pspec);
static void phonon_src_get_property(GObject *object, guint prop_id,
                                    GValue *value, GParamSpec *pspec);

static gboolean phonon_src_start(GstBaseSrc *basesrc);
static gboolean phonon_src_stop(GstBaseSrc *basesrc);

static gboolean phonon_src_unlock(GstBaseSrc *basesrc);
static gboolean phonon_src_unlock_stop(GstBaseSrc *basesrc);

static gboolean phonon_src_is_seekable(GstBaseSrc *src);
static gboolean phonon_src_get_size(GstBaseSrc *src, guint64 *size);
static GstFlowReturn phonon_src_create(GstBaseSrc *src, guint64 offset,
                                       guint length, GstBuffer **buffer);

static gboolean register_elements(GstPlugin *plugin)
{
    if (!gst_element_register(plugin, "phononsrc", GST_RANK_NONE, GST_TYPE_PHONON_SRC))
        return false;
    return true;
}

gboolean register_phonon_elements()
{
    gst_plugin_register_static(
        GST_VERSION_MAJOR,
        GST_VERSION_MINOR,
        "phonon-plugins",
        "Private elements of Phonon",
        register_elements,
        PHONON_VERSION_STR,
        "LGPL",
        "phonon",
        "phonon",
        "http://phonon.kde.org");
    return true;
}

static void _do_init(GType filesrc_type)
{
    Q_UNUSED(filesrc_type);
    GST_DEBUG_CATEGORY_INIT (phonon_src_debug, "phononsrc", 0, "QIODevice element");
}

GST_BOILERPLATE_FULL(PhononSrc, phonon_src, GstBaseSrc, GST_TYPE_BASE_SRC, _do_init)

// Register element details
static void phonon_src_base_init(gpointer g_class) {
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
    static gchar longname[] = "Phonon Stream Source",
                    klass[] = "Source/File",
              description[] = "Read from a Phonon StreamInterface",
                   author[] = "Nokia Corporation and/or its subsidiary(-ies) <qt-info@nokia.com>";
    GstElementDetails details = GST_ELEMENT_DETAILS (longname,
                                          klass,
                                          description,
                                          author);
    gst_element_class_set_details(gstelement_class, &details);
    gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&srctemplate));
}

static void phonon_src_class_init (PhononSrcClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;
    GstBaseSrcClass *gstbasesrc_class;

    gobject_class = G_OBJECT_CLASS(klass);
    gstelement_class = GST_ELEMENT_CLASS(klass);
    gstbasesrc_class = GST_BASE_SRC_CLASS(klass);

    gobject_class->set_property = phonon_src_set_property;
    gobject_class->get_property = phonon_src_get_property;

    g_object_class_install_property(gobject_class, ARG_PHONONSRC,
                                    g_param_spec_pointer ("iodevice", "A Phonon StreamReader",
                                                          "A Phonon::GStreamer::StreamReader to read from", GParamFlags(G_PARAM_READWRITE)));

    gobject_class->finalize = GST_DEBUG_FUNCPTR(phonon_src_finalize);

    gstbasesrc_class->start = GST_DEBUG_FUNCPTR(phonon_src_start);
    gstbasesrc_class->stop = GST_DEBUG_FUNCPTR(phonon_src_stop);
    gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR(phonon_src_unlock);
    gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR(phonon_src_unlock_stop);
    gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR(phonon_src_is_seekable);
    gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR(phonon_src_get_size);
    gstbasesrc_class->create = GST_DEBUG_FUNCPTR(phonon_src_create);
}

static void phonon_src_init(PhononSrc *src, PhononSrcClass *g_class)
{
    Q_UNUSED(g_class);
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    src->device = 0;
#endif
}

static void phonon_src_finalize(GObject *object)
{
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    PhononSrc *src;
    src = GST_PHONON_SRC(object);
    delete src->device;
    src->device = 0;
#endif // QT_NO_PHONON_ABSTRACTMEDIASTREAM
    G_OBJECT_CLASS (parent_class)->finalize(object);
}

#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
static gboolean phonon_src_set_device(PhononSrc *src, StreamReader *device)
{
    GstState state;
    // The element must be stopped in order to do this
    GST_OBJECT_LOCK(src);
    state = GST_STATE(src);

    if (state != GST_STATE_READY && state != GST_STATE_NULL)
        goto wrong_state;

    GST_OBJECT_UNLOCK(src);

    src->device = device;
    g_object_notify(G_OBJECT (src), "iodevice");
    return true;

    // Error
wrong_state:
    {
        //GST_DEBUG_OBJECT (src, "setting location in wrong state");
        GST_OBJECT_UNLOCK (src);
        return false;
    }
}
#endif //QT_NO_PHONON_ABSTRACTMEDIASTREAM

static void phonon_src_set_property(GObject *object, guint prop_id, const GValue *value,
                                    GParamSpec *pspec)
{
    PhononSrc *src;
    g_return_if_fail(GST_IS_PHONON_SRC(object));
    src = GST_PHONON_SRC(object);

    switch (prop_id) {
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    case ARG_PHONONSRC:
    {
        StreamReader *dev = (StreamReader*)(g_value_get_pointer(value));
        if (dev)
            phonon_src_set_device(src, dev);
        break;
    }
#endif //QT_NO_PHONON_ABSTRACTMEDIASTREAM
   default:
       G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
       break;
   }
}

static void phonon_src_get_property(GObject *object, guint prop_id, GValue *value,
                                    GParamSpec *pspec)
{
    PhononSrc *src;
    g_return_if_fail (GST_IS_PHONON_SRC (object));
    src = GST_PHONON_SRC (object);

    switch (prop_id) {
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    case ARG_PHONONSRC:
        g_value_set_pointer(value, src->device);
        break;
#endif // QT_NO_PHONON_ABSTRACTMEDIASTREAM
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static GstFlowReturn phonon_src_create_read(PhononSrc *src, guint64 offset, guint length,
                                            GstBuffer **buffer)
{
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    Q_ASSERT(src->device);
    if (!src->device)
        return GST_FLOW_ERROR;

    GstBuffer *buf = gst_buffer_new_and_alloc (length);
    GST_BUFFER_SIZE (buf) = length;
    GST_BUFFER_OFFSET (buf) = offset;
    GST_BUFFER_OFFSET_END (buf) = offset + length;

    GstFlowReturn ret = src->device->read(offset, length, (char*)GST_BUFFER_DATA (buf));
//    GST_LOG_OBJECT (src, "Reading %d bytes", length);

    if (ret == GST_FLOW_OK) {
        *buffer = buf;
        return ret;
    } else if (ret == GST_FLOW_UNEXPECTED) {
        return ret;
    }

    gst_mini_object_unref(GST_MINI_OBJECT(buf));
    return GST_FLOW_ERROR;
#endif // QT_NO_PHONON_ABSTRACTMEDIASTREAM
    return GST_FLOW_ERROR;
}

static GstFlowReturn phonon_src_create(GstBaseSrc *basesrc, guint64 offset, guint length, GstBuffer **buffer)
{
    PhononSrc *src = GST_PHONON_SRC (basesrc);
    return phonon_src_create_read (src, offset, length, buffer);
}

static gboolean phonon_src_is_seekable(GstBaseSrc *basesrc)
{
    PhononSrc *src = GST_PHONON_SRC(basesrc);
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    if (src->device)
        return src->device->streamSeekable();
#endif // QT_NO_PHONON_ABSTRACTMEDIASTREAM
    return false;
}

static gboolean phonon_src_get_size(GstBaseSrc *basesrc, guint64 *size)
{
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    PhononSrc *src = GST_PHONON_SRC(basesrc);
    if (src->device && src->device->streamSeekable()) {
        *size = src->device->streamSize();
        return true;
    }
#endif // QT_NO_PHONON_ABSTRACTMEDIASTREAM
    *size = 0;
    return false;
}

static gboolean phonon_src_unlock(GstBaseSrc *basesrc)
{
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    PhononSrc *src = GST_PHONON_SRC(basesrc);
    src->device->unlock();
    return true;
#endif // QT_NO_PHONON_ABSTRACTMEDIASTREAM
    return false;
}

static gboolean phonon_src_unlock_stop(GstBaseSrc *basesrc)
{
    Q_UNUSED(basesrc);
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    // Resource locking and unlocking happens onthefly, unlock just wakes our
    // QWaitConditions.
    return true;
#endif // QT_NO_PHONON_ABSTRACTMEDIASTREAM
    return false;
}

// Necessary to go to READY state
static gboolean phonon_src_start(GstBaseSrc *basesrc)
{
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    // Opening the device is handled by the frontend, still we need to make sure
    // that the streamer is in initial state WRT member variables etc.
    PhononSrc *src = GST_PHONON_SRC(basesrc);
    src->device->start();
    return true;
#endif // QT_NO_PHONON_ABSTRACTMEDIASTREAM
    return false;
}

static gboolean phonon_src_stop(GstBaseSrc *basesrc)
{
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
    // Closing the device is handled by the frontend, we just need to ensure
    // the reader is unlocked and send a final enoughData.
    PhononSrc *src = GST_PHONON_SRC(basesrc);
    src->device->stop();
    return true;
#endif // QT_NO_PHONON_ABSTRACTMEDIASTREAM
    return false;
}

}
} //namespace Phonon::Gstreamer

QT_END_NAMESPACE
