/*  This file is part of the KDE project.

    Copyright (C) 2011 Harald Sitter <sitter@kde.org>
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

#include "streamreader.h"

QT_BEGIN_NAMESPACE
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
namespace Phonon
{
namespace Gstreamer
{

StreamReader::StreamReader(const Phonon::MediaSource &source, MediaObject *parent)
    : m_pos(0)
    , m_size(0)
    , m_eos(false)
    , m_locked(false)
    , m_seekable(false)
    , m_mediaObject(parent)
{
    connectToSource(source);
}

int StreamReader::currentBufferSize() const
{
    return m_buffer.size();
}

void StreamReader::writeData(const QByteArray &data) {
    QMutexLocker locker(&m_mutex);

    m_buffer.append(data);
    m_waitingForData.wakeAll();
}

void StreamReader::setCurrentPos(qint64 pos)
{
    QMutexLocker locker(&m_mutex);

    m_pos = pos;
    seekStream(pos);
    m_buffer.clear();
}

quint64 StreamReader::currentPos() const
{
    return m_pos;
}

GstFlowReturn StreamReader::read(quint64 pos, int length, char *buffer)
{
    QMutexLocker locker(&m_mutex);

    if (currentPos() != pos) {
        if (!streamSeekable()) {
            return GST_FLOW_NOT_SUPPORTED;
        }
        // TODO: technically an error can occur here, however the abstractstream
        // API does not consider this, so we must assume that everything always goes
        // alright and continue processing.
        setCurrentPos(pos);
    }

    while (currentBufferSize() < length) {
        int oldSize = currentBufferSize();
        needData();

        m_waitingForData.wait(&m_mutex);

        // Abort instantly if we got unlocked, whether we got sufficient data or not
        // is absolutely unimportant at this point.
        if (!m_locked) {
            break;
        }

        if (oldSize == currentBufferSize()) {
            // We didn't get any data, check if we are at the end of stream already.
            if (m_eos) {
                return GST_FLOW_UNEXPECTED;
        }
    }
    if (m_mediaObject->state() != Phonon::BufferingState &&
        m_mediaObject->state() != Phonon::LoadingState) {
        enoughData();
    }

    qMemCopy(buffer, m_buffer.data(), length);
    m_pos += length;
    //truncate the buffer
    m_buffer = m_buffer.mid(length);
    return GST_FLOW_OK;
}

void StreamReader::endOfData()
{
    QMutexLocker locker(&m_mutex);
    m_eos = true;
    m_waitingForData.wakeAll();
}

void StreamReader::start()
{
    QMutexLocker locker(&m_mutex);
    m_buffer.clear();
    m_eos = false;
    m_locked = true;
    m_pos = 0;
    m_seekable = false;
    m_size = 0;
    reset();
}

void StreamReader::stop()
{
    enoughData();
    m_waitingForData.wakeAll();
}

void StreamReader::unlock()
{
    QMutexLocker locker(&m_mutex);
    enoughData();
    m_locked = false;
    m_waitingForData.wakeAll();
}

void StreamReader::setStreamSize(qint64 newSize)
{
    m_size = newSize;
}

qint64 StreamReader::streamSize() const
{
    return m_size;
}

void StreamReader::setStreamSeekable(bool seekable)
{
//    if (seekable) {
//        // The initial stream size of a seekable stream *must* not be 0 or
//        // GStreamer will refuse to typefind.
//        m_size = 1;
//    }
    m_seekable = seekable;
}

bool StreamReader::streamSeekable() const
{
    // TODO - problems with pull seeking and the way our current stack works.
    return false;
//    return m_seekable;
}

}
}
#endif //QT_NO_PHONON_ABSTRACTMEDIASTREAM

QT_END_NAMESPACE
