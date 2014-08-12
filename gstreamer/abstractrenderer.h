/*  This file is part of the KDE project.

    Copyright (C) 2    //Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).007 Nokia Corporation and/or its subsidiary(-ies).

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

#ifndef Phonon_GSTREAMER_ABSTRACTRENDERER_H
#define Phonon_GSTREAMER_ABSTRACTRENDERER_H

#include <gst/gstelement.h>

#include <phonon/videowidget.h>

class QString;

namespace Phonon
{
namespace Gstreamer
{

class VideoWidget;

class AbstractRenderer
{
public:
    AbstractRenderer(VideoWidget *video);
    virtual ~AbstractRenderer();

    virtual void aspectRatioChanged(Phonon::VideoWidget::AspectRatio aspectRatio);
    virtual void scaleModeChanged(Phonon::VideoWidget::ScaleMode scaleMode);
    virtual void movieSizeChanged(const QSize &movieSize);
    virtual bool eventFilter(QEvent *) = 0;
    virtual void handlePaint(QPaintEvent *)
    {
    }

    virtual bool paintsOnWidget() const {  // Controls overlays
        return true;
    }

    GstElement *videoSink() const {
        return m_videoSink;
    }

protected:
    /**
     * Takes ownership of @p sink
     */
    void setVideoSink(GstElement *sink);

    VideoWidget *videoWidget() const;

private:
    VideoWidget *m_videoWidget;
    GstElement *m_videoSink;
};

}
} //namespace Phonon::Gstreamer

#endif // Phonon_GSTREAMER_ABSTRACTRENDERER_H
