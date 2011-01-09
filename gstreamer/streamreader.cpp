/*  This file is part of the KDE project.

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

#include <QtCore/QDebug>
#define d qDebug() << Q_FUNC_INFO << ": "

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
    , m_seekable(false)
    , m_mediaObject(parent)
{
    connectToSource(source);
}

int StreamReader::currentBufferSize() const
{
    d << m_buffer.size();
    return m_buffer.size();
}

void StreamReader::writeData(const QByteArray &data) {
    d << "getting ze data and other plunder";
    QMutexLocker locker(&m_mutex);

    m_buffer.append(data);

    m_waitingForData.wakeAll();

#warning enoughData sometimes locks any furhter needata?
//    if (m_mediaObject->state() != Phonon::BufferingState &&
//        m_mediaObject->state() != Phonon::LoadingState) {
//        d << "we haz had enuogh, kthxbai!";
//        enoughData();
//    }
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
#warning ret something
        setCurrentPos(pos);
    }

    while (currentBufferSize() < length) {
        d << "whiling";
        int oldSize = currentBufferSize();
        needData();

        m_waitingForData.wait(&m_mutex);

        if (oldSize == currentBufferSize()) {
            // We didn't get any data.
            if (m_eos) {
                return GST_FLOW_UNEXPECTED;
            } else {
                return GST_FLOW_ERROR;
            }
        }
    }
//    enoughData();

    d << "filling up that stinky old buffer";
    qMemCopy(buffer, m_buffer.data(), length);
    m_pos += length;
    //truncate the buffer
    m_buffer = m_buffer.mid(length);
    return GST_FLOW_OK;
}

void StreamReader::endOfData()
{
    d << "out of the data!!!";
    QMutexLocker locker(&m_mutex);
    m_eos = true;
    m_waitingForData.wakeAll();
}

void StreamReader::start()
{
    d;
    QMutexLocker locker(&m_mutex);
    m_buffer.clear();
    m_eos = false;
    m_pos = 0;
    m_seekable = false;
    m_size = 0;
    reset();
}

void StreamReader::stop()
{
#ifdef __GNUC__
#warning implications of stop on still working read????
#endif
    d << "stopping!";
    enoughData();
//    m_waitingForData.wakeAll();
}

void StreamReader::unlock()
{
    QMutexLocker locker(&m_mutex);
    d << "lock lock I shall unlock...";
//    enoughData();
//    m_waitingForData.wakeAll();
}

void StreamReader::unlockStop()
{
    QMutexLocker locker(&m_mutex);
}

void StreamReader::setStreamSize(qint64 newSize) {
    d << "setting der streamsize to - " << newSize;
    m_size = newSize;
}

qint64 StreamReader::streamSize() const {
    return m_size;
}

void StreamReader::setStreamSeekable(bool seekable) {
    d << seekable;
    m_seekable = seekable;
}

bool StreamReader::streamSeekable() const {
    d << m_seekable;
    return false;
    return m_seekable;
}

}
}
#endif //QT_NO_PHONON_ABSTRACTMEDIASTREAM

QT_END_NAMESPACE
