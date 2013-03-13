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
#include <QtGui/QApplication>
#include <QtGui/QPainter>
#include <X11/Xlib.h>
#include <gst/gst.h>
#if GST_VERSION < GST_VERSION_CHECK (1,0,0,0)
#include <gst/interfaces/xoverlay.h>
#include <gst/interfaces/propertyprobe.h>
#else
#include <gst/video/videooverlay.h>
#endif

namespace Phonon
{
namespace Gstreamer
{

class OverlayWidget : public QWidget
{
public:
    OverlayWidget(VideoWidget *videoWidget, X11Renderer *renderer) :
                  QWidget(videoWidget),
                  m_videoWidget(videoWidget),
                  m_renderer(renderer) { }
    void paintEvent(QPaintEvent *) {
        Phonon::State state = m_videoWidget->root() ? m_videoWidget->root()->state() : Phonon::LoadingState;
        if (state == Phonon::PlayingState || state == Phonon::PausedState) {
            m_renderer->windowExposed();
        } else {
            QPainter painter(this);
            painter.fillRect(m_videoWidget->rect(), m_videoWidget->palette().background());
        }
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
    m_videoWidget->setPalette(palette);
    m_videoWidget->setAutoFillBackground(true);
    m_renderWidget->setMouseTracking(true);
    m_videoSink = createVideoSink();
    aspectRatioChanged(videoWidget->aspectRatio());
    setOverlay();
}

X11Renderer::~X11Renderer()
{
    m_renderWidget->setAttribute(Qt::WA_PaintOnScreen, false);
    m_renderWidget->setAttribute(Qt::WA_NoSystemBackground, false);
    delete m_renderWidget;
}

GstElement* X11Renderer::createVideoSink()
{
    GstElement *videoSink = gst_element_factory_make ("xvimagesink", NULL);
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
        videoSink = gst_element_factory_make ("nv_gl_videosink", NULL);
    }
    if (!videoSink) {
        videoSink = gst_element_factory_make ("ximagesink", NULL);
    }

    gst_object_ref (GST_OBJECT (videoSink)); //Take ownership
#if GST_VERSION < GST_VERSION_CHECK (1,0,0,0)
    gst_object_sink (GST_OBJECT (videoSink));
#else
    gst_object_ref_sink (GST_OBJECT (videoSink));
#endif

    return videoSink;
}

void X11Renderer::aspectRatioChanged(Phonon::VideoWidget::AspectRatio)
{
    if (m_renderWidget) {
        m_renderWidget->setGeometry(m_videoWidget->calculateDrawFrameRect());
    }
}

void X11Renderer::scaleModeChanged(Phonon::VideoWidget::ScaleMode)
{
    if (m_renderWidget) {
        m_renderWidget->setGeometry(m_videoWidget->calculateDrawFrameRect());
    }
}

void X11Renderer::movieSizeChanged(const QSize &movieSize)
{
    Q_UNUSED(movieSize);

    if (m_renderWidget) {
        m_renderWidget->setGeometry(m_videoWidget->calculateDrawFrameRect());
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
        m_renderWidget->setGeometry(m_videoWidget->calculateDrawFrameRect());
        windowExposed();
    }
    return false;
}

void X11Renderer::handlePaint(QPaintEvent *)
{
    QPainter painter(m_videoWidget);
    painter.fillRect(m_videoWidget->rect(), m_videoWidget->palette().background());
}

void X11Renderer::setOverlay()
{
    if (m_videoSink &&
        #if GST_VERSION < GST_VERSION_CHECK (1,0,0,0)
            GST_IS_X_OVERLAY(m_videoSink)
        #else
            GST_IS_VIDEO_OVERLAY(m_videoSink)
        #endif
            ) {
        WId windowId = m_renderWidget->winId();
        // Even if we have created a winId at this point, other X applications
        // need to be aware of it.
        QApplication::syncX();
#if GST_VERSION >= GST_VERSION_CHECK (1,0,0,0)
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_videoSink), windowId);
#elif GST_VERSION <= GST_VERSION_CHECK (1,0,0,0)
        gst_x_overlay_set_window_handle(GST_X_OVERLAY(m_videoSink), windowId);
#elif GST_VERSION <= GST_VERSION_CHECK(0,10,31,0)
        gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(m_videoSink), windowId);
#endif // GST_VERSION
    }
    windowExposed();
    m_overlaySet = true;
}

void X11Renderer::windowExposed()
{
    QApplication::syncX();
    if (m_videoSink &&
        #if GST_VERSION < GST_VERSION_CHECK (1,0,0,0)
            GST_IS_X_OVERLAY(m_videoSink)
        #else
            GST_IS_VIDEO_OVERLAY(m_videoSink)
        #endif
            )
#if GST_VERSION < GST_VERSION_CHECK (1,0,0,0)
        gst_x_overlay_expose(GST_X_OVERLAY(m_videoSink));
#else
        gst_video_overlay_expose(GST_VIDEO_OVERLAY(m_videoSink));
#endif
}

}

} //namespace Phonon::Gstreamer

#endif // Q_WS_QWS
