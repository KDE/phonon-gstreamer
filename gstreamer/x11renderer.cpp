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

#include "x11renderer.h"

#include "videowidget.h"

#ifndef Q_WS_QWS

#include "backend.h"
#include "debug.h"
#include "mediaobject.h"
#include <QtGui/QPalette>
#include <QApplication>
#include <QtGui/QPainter>
#include <X11/Xlib.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

namespace Phonon
{
namespace Gstreamer
{

class OverlayWidget : public QWidget
{
public:
    OverlayWidget(VideoWidget *videoWidget, X11Renderer *renderer)
            : QWidget(videoWidget)
            , m_videoWidget(videoWidget)
            , m_renderer(renderer)
    {
    }

    void paintEvent(QPaintEvent *) override {
        Phonon::State state = m_videoWidget->root() ? m_videoWidget->root()->state() : Phonon::LoadingState;
        if (state == Phonon::PlayingState || state == Phonon::PausedState) {
            m_renderer->windowExposed();
        } else {
            QPainter painter(this);
            painter.fillRect(m_videoWidget->rect(), m_videoWidget->palette().background());
        }
    }

    QPaintEngine *paintEngine() const override
    {
        return nullptr;
    }

private:
    VideoWidget *m_videoWidget;
    X11Renderer *m_renderer;
};

X11Renderer::X11Renderer(VideoWidget *videoWidget)
        : AbstractRenderer(videoWidget)
{
    m_renderWidget = new OverlayWidget(videoWidget, this);
    debug() << "Creating X11 overlay renderer";
    QPalette palette;
    palette.setColor(QPalette::Background, Qt::black);
    videoWidget->setPalette(palette);
    videoWidget->setAutoFillBackground(true);
    m_renderWidget->setMouseTracking(true);
    GstElement *videoSink = createVideoSink();
    if (videoSink) {
        setVideoSink(videoSink);
    }
    aspectRatioChanged(videoWidget->aspectRatio());
    setOverlay();
}

X11Renderer::~X11Renderer()
{
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    m_renderWidget->setAttribute(Qt::WA_PaintOnScreen, false);
#endif
    m_renderWidget->setAttribute(Qt::WA_NoSystemBackground, false);
    delete m_renderWidget;
}

GstElement* X11Renderer::createVideoSink()
{
    GstElement *videoSink = gst_element_factory_make("xvimagesink", NULL);
    if (videoSink) {
        // Check if the xv sink is usable
        if (gst_element_set_state(videoSink, GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS) {
            gst_object_unref(GST_OBJECT(videoSink));
            videoSink = 0;
        } else {
            // Note that this should not really be necessary as these are
            // default values, though under certain conditions values are retained
            // even between application instances. (reproducible on 0.10.16/Gutsy)
            g_object_set(G_OBJECT(videoSink), "brightness", 0, NULL);
            g_object_set(G_OBJECT(videoSink), "contrast", 0, NULL);
            g_object_set(G_OBJECT(videoSink), "hue", 0, NULL);
            g_object_set(G_OBJECT(videoSink), "saturation", 0, NULL);
        }
    }
    QByteArray tegraEnv = qgetenv("TEGRA_GST_OPENMAX");
    if (!tegraEnv.isEmpty()) {
        videoSink = gst_element_factory_make("nv_gl_videosink", NULL);
    }
    if (!videoSink) {
        videoSink = gst_element_factory_make("ximagesink", NULL);
    }

    return videoSink;
}

void X11Renderer::aspectRatioChanged(Phonon::VideoWidget::AspectRatio)
{
    if (m_renderWidget) {
        m_renderWidget->setGeometry(videoWidget()->calculateDrawFrameRect());
    }
}

void X11Renderer::scaleModeChanged(Phonon::VideoWidget::ScaleMode)
{
    if (m_renderWidget) {
        m_renderWidget->setGeometry(videoWidget()->calculateDrawFrameRect());
    }
}

void X11Renderer::movieSizeChanged(const QSize &movieSize)
{
    Q_UNUSED(movieSize);

    if (m_renderWidget) {
        m_renderWidget->setGeometry(videoWidget()->calculateDrawFrameRect());
    }
}

bool X11Renderer::eventFilter(QEvent *e)
{
    if (e->type() == QEvent::Show) {
        // Setting these values ensures smooth resizing since it
        // will prevent the system from clearing the background
        m_renderWidget->setAttribute(Qt::WA_NoSystemBackground, true);
        m_renderWidget->setAttribute(Qt::WA_PaintOnScreen, true);
        setOverlay();
    } else if (e->type() == QEvent::Resize) {
        // This is a workaround for missing background repaints
        // when reducing window size
        m_renderWidget->setGeometry(videoWidget()->calculateDrawFrameRect());
        windowExposed();
    }
    return false;
}

void X11Renderer::handlePaint(QPaintEvent *)
{
    QPainter painter(videoWidget());
    painter.fillRect(videoWidget()->rect(), videoWidget()->palette().background());
}

void X11Renderer::setOverlay()
{
    if (videoSink() && GST_IS_VIDEO_OVERLAY(videoSink())) {
        WId windowId = m_renderWidget->winId();
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink()), windowId);
    }
    windowExposed();
    m_overlaySet = true;
}

void X11Renderer::windowExposed()
{
    // This can be invoked within a callchain in an arbitrary thread, so make
    // sure we call syncX() from the main thread
    QMetaObject::invokeMethod(videoWidget(), "syncX",
                              Qt::QueuedConnection);

    if (videoSink() && GST_IS_VIDEO_OVERLAY(videoSink())) {
        gst_video_overlay_expose(GST_VIDEO_OVERLAY(videoSink()));
    }
}

}

} //namespace Phonon::Gstreamer

#endif // Q_WS_QWS
