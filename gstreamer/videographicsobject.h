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

#ifndef PHONON_GSTREAMER_VIDEOGRAPHICSOBJECT_H
#define PHONON_GSTREAMER_VIDEOGRAPHICSOBJECT_H

#include <QtCore/QObject>
#include <QtCore/QMutex>

#include <phonon/videographicsobjectinterface.h>

#include "medianode.h"
#include "videosink.h"
#include "debug.h"

namespace Phonon {
namespace Gstreamer {

class VideoGraphicsObject : public QObject,
                            public VideoGraphicsObjectInterface,
                            public MediaNode
{
    Q_OBJECT
    Q_INTERFACES(Phonon::VideoGraphicsObjectInterface Phonon::Gstreamer::MediaNode)
public:
    explicit VideoGraphicsObject(Backend *backend, QObject *parent = 0);
    ~VideoGraphicsObject();
    QList<VideoFrame::Format> offering(QList<VideoFrame::Format> offers);
    void choose(VideoFrame::Format format);

    static void renderCallback(GstBuffer *buffer, void *userData);

    void lock();
    bool tryLock();
    void unlock();

    const VideoFrame *frame() const;

    GstElement *videoElement()
    {
        debug() << "fishy";
        return m_bin;
    }

signals:
    void frameReady();
    void reset();
    void needFormat();

private:
    Phonon::VideoFrame m_frame;

    PGstVideoSink *m_sink;

    QMutex m_mutex;

    GstElement *m_bin;
    GstBuffer *m_buffer;

    VideoFrame::Format m_format;
};

} // namespace Gstreamer
} // namespace Phonon

#endif // PHONON_GSTREAMER_VIDEOGRAPHICSOBJECT_H
