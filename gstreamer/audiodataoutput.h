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

#ifndef Phonon_GSTREAMER_AUDIODATAOUTPUT_H
#define Phonon_GSTREAMER_AUDIODATAOUTPUT_H

#include "medianode.h"
#include <phonon/audiodataoutput.h>
#include <phonon/audiodataoutputinterface.h>

QT_BEGIN_HEADER
QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{
/**
 * \author Martin Sandsmark <sandsmark@samfundet.no>
 */
class AudioDataOutput : public QObject,
        public AudioDataOutputInterface,
        public MediaNode
{
    Q_OBJECT
    Q_INTERFACES(Phonon::AudioDataOutputInterface Phonon::Gstreamer::MediaNode)

public:
    AudioDataOutput(Backend *backend, QObject *parent);
    ~AudioDataOutput();

public Q_SLOTS:
    int dataSize() const;
    int sampleRate() const;
    void setDataSize(int size);

public:
    /// callback function for handling new audio data
    static void processBuffer(GstElement*, GstBuffer*, GstPad*, gpointer);

    Phonon::AudioDataOutput* frontendObject() const { return m_frontend; }
    void setFrontendObject(Phonon::AudioDataOutput *frontend) { m_frontend = frontend; }

    GstElement *audioElement() { return m_queue; }

signals:
    void dataReady(const QMap<Phonon::AudioDataOutput::Channel, QVector<qint16> > &data);
    void endOfMedia(int remainingSamples);

private:
    void convertAndEmit();

    GstElement *m_queue;
    Phonon::AudioDataOutput *m_frontend;
    QVector<qint16> m_pendingData;
    qint32 m_dataSize;
    int m_channels;
    QVector<QVector<qint16> > m_channelBuffers;
};
} // namespace Gstreamer
} // namespace Phonon

QT_END_NAMESPACE
QT_END_HEADER

// vim: sw=4 ts=4 tw=80
#endif // Phonon_GSTREAMER_AUDIODATAOUTPUT_H
