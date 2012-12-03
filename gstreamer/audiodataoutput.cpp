/*
    Copyright (C) 2006 Matthias Kretz <kretz@kde.org>
    Copyright (C) 2009 Martin Sandsmark <sandsmark@samfundet.no>
    Copyright (C) 2011 Harald Sitter <sitter@kde.org>
    Copyright (C) 2011 Alessandro Siniscalchi <asiniscalchi@gmail.com>

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

#include "audiodataoutput.h"
#include "gsthelper.h"
#include "medianode.h"
#include <QtCore/QVector>
#include <QtCore/QMap>
#include <phonon/audiooutput.h>

#include <gst/gstghostpad.h>
#include <gst/gstutils.h>

QT_BEGIN_HEADER
QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

AudioDataOutput::AudioDataOutput(Backend *backend, QObject *parent)
    : QObject(parent)
    , MediaNode(backend, AudioSink)
{
    static int count = 0;
    m_name = "AudioDataOutput" + QString::number(count++);

    m_queue = gst_bin_new(NULL);
    gst_object_ref(GST_OBJECT(m_queue));
    gst_object_sink(GST_OBJECT(m_queue));
    GstElement* sink = gst_element_factory_make("fakesink", NULL);
    GstElement* queue = gst_element_factory_make("queue", NULL);
    GstElement* convert = gst_element_factory_make("audioconvert", NULL);

    g_signal_connect(sink, "handoff", G_CALLBACK(processBuffer), this);
    g_object_set(G_OBJECT(sink), "signal-handoffs", true, NULL);

    //G_BYTE_ORDER is the host machine's endianess
    GstCaps *caps = gst_caps_new_simple("audio/x-raw-int",
                                        "endianess", G_TYPE_INT, G_BYTE_ORDER,
                                        "width", G_TYPE_INT, 16,
                                        "depth", G_TYPE_INT, 16,
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

AudioDataOutput::~AudioDataOutput()
{
    gst_element_set_state(m_queue, GST_STATE_NULL);
    gst_object_unref(m_queue);
}

void AudioDataOutput::setDataSize(int size)
{
    m_dataSize = size;
}

int AudioDataOutput::dataSize() const
{
    return m_dataSize;
}

int AudioDataOutput::sampleRate() const
{
    return 44100;
}

inline void AudioDataOutput::convertAndEmit()
{
    QMap<Phonon::AudioDataOutput::Channel, QVector<qint16> > map;

    for (int i = 0 ; i < m_channels ; ++i) {
        map.insert(static_cast<Phonon::AudioDataOutput::Channel>(i), m_channelBuffers[i]);
    }

    emit dataReady(map);
}

void AudioDataOutput::processBuffer(GstElement*, GstBuffer* buffer, GstPad*, gpointer gThat)
{
    // TODO emit endOfMedia
    AudioDataOutput *that = static_cast<AudioDataOutput *>(gThat);

    // Copiend locally to avoid multithead problems
    qint32 dataSize = that->m_dataSize;
    if (dataSize == 0)
        return;

    // determine the number of channels
    GstStructure *structure = gst_caps_get_structure(GST_BUFFER_CAPS(buffer), 0);
    gst_structure_get_int(structure, "channels", &that->m_channels);

    // Let's get the buffers
    gint16 *gstBufferData = reinterpret_cast<gint16 *>(GST_BUFFER_DATA(buffer));
    guint gstBufferSize = GST_BUFFER_SIZE(buffer) / sizeof(gint16);

    if (gstBufferSize == 0) {
        qWarning() << Q_FUNC_INFO << ": received a buffer of 0 size ... doing nothing";
        return;
    }

    if ((gstBufferSize % that->m_channels) != 0) {
        qWarning() << Q_FUNC_INFO << ": corrupted data";
        return;
    }

    // I set the number of channels
    if (that->m_channelBuffers.size() != that->m_channels)
        that->m_channelBuffers.resize(that->m_channels);

    // check how many emits I will perform
    int nBlockToSend = (that->m_pendingData.size() + gstBufferSize) / (dataSize * that->m_channels);

    if (nBlockToSend == 0) { // add data to pending buffer
        const int prevPendingSize = that->m_pendingData.size();
        for (quint32 i = 0; i < gstBufferSize ; ++i) {
#warning should be QBA, so we can work with append(buffer, size)
            that->m_pendingData.append(gstBufferData[i]);
        }
        Q_ASSERT(prevPendingSize + gstBufferSize == that->m_pendingData.size());
        return;
    }

    // SENDING DATA

    // 1) I empty the stored data
    if (that->m_pendingData.size() != 0) {
        // Since pendingData is a concatenation of buffers it must share its
        // attribute of being a multiple of channelCount
        Q_ASSERT((that->m_pendingData.size() % that->m_channels) == 0);
        for (int i = 0; i < that->m_pendingData.size(); i += that->m_channels) {
            for (int j = 0; j < that->m_channels; ++j) {
                that->m_channelBuffers[j].append(that->m_pendingData[i+j]);
            }
        }

        if (that->m_pendingData.capacity() != dataSize)
            that->m_pendingData.reserve(dataSize);

        that->m_pendingData.resize(0);
    }

    // 2) I fill with fresh data and send
    for (int i = 0 ; i < that->m_channels ; ++i)
    {
        if (that->m_channelBuffers[i].capacity() != dataSize)
            that->m_channelBuffers.reserve(dataSize);
    }

    quint32 bufferPosition = 0;
    for (int i = 0 ; i < nBlockToSend ; ++i) {
        for ( ; (that->m_channelBuffers[0].size() < dataSize) && (bufferPosition < gstBufferSize) ; bufferPosition += that->m_channels ) {
            for (int j = 0 ; j < that->m_channels ; ++j) {
                that->m_channelBuffers[j].append(gstBufferData[bufferPosition+j]);
            }
        }

        that->convertAndEmit();

        for (int j = 0 ; j < that->m_channels ; ++j) {
            // QVector::resize doesn't reallocate the buffer
            that->m_channelBuffers[j].resize(0);
        }
    }

    // 3) I store the rest of data
    while (bufferPosition < gstBufferSize) {
        that->m_pendingData.append(gstBufferData[bufferPosition]);
        ++bufferPosition;
    }
}

}} //namespace Phonon::Gstreamer

QT_END_NAMESPACE
QT_END_HEADER

#include "moc_audiodataoutput.cpp"
