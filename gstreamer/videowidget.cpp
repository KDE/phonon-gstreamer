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

#include "videowidget.h"
#include <QtCore/QEvent>
#include <QtCore/QDebug>
#include <QtGui/QResizeEvent>
#include <QtGui/QPalette>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QBoxLayout>
#include <QApplication>
#include <gst/gst.h>
#include <gst/interfaces/navigation.h>
#include <gst/interfaces/propertyprobe.h>
#include <gst/video/video.h>
#include "abstractrenderer.h"
#include "backend.h"
#include "devicemanager.h"
#include "mediaobject.h"
#include "x11renderer.h"

#include "widgetrenderer.h"

#ifndef QT_NO_PHONON_VIDEO
QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

VideoWidget::VideoWidget(Backend *backend, QWidget *parent) :
    QWidget(parent),
    MediaNode(backend, VideoSink),
    m_videoBin(0),
    m_renderer(0),
    m_aspectRatio(Phonon::VideoWidget::AspectRatioAuto),
    m_brightness(0.0),
    m_hue(0.0),
    m_contrast(0.0),
    m_saturation(0.0),
    m_scaleMode(Phonon::VideoWidget::FitInView),
    m_videoBalance(0),
    m_colorspace(0),
    m_videoplug(0)
{
    setupVideoBin();
    setFocusPolicy(Qt::ClickFocus);
}

VideoWidget::~VideoWidget()
{
    if (m_videoBin) {
        gst_element_set_state (m_videoBin, GST_STATE_NULL);
        gst_object_unref (m_videoBin);
    }

    if (m_renderer)
        delete m_renderer;
}

void VideoWidget::updateWindowID()
{
    X11Renderer *render = dynamic_cast<X11Renderer*>(m_renderer);
    if (render)
        render->setOverlay();
}

void VideoWidget::finalizeLink()
{
    connect(root()->pipeline(), SIGNAL(mouseOverActive(bool)), this, SLOT(mouseOverActive(bool)));
    connect(root()->pipeline(), SIGNAL(windowIDNeeded()), this, SLOT(updateWindowID()));
}

void VideoWidget::prepareToUnlink()
{
    disconnect(root()->pipeline());
}

void VideoWidget::setupVideoBin()
{

    m_renderer = m_backend->deviceManager()->createVideoRenderer(this);
    GstElement *videoSink = m_renderer->videoSink();
    GstPad *videoPad = gst_element_get_static_pad(videoSink, "sink");
    g_signal_connect(videoPad, "notify::caps", G_CALLBACK(cb_capsChanged), this);

    m_videoBin = gst_bin_new (NULL);
    Q_ASSERT(m_videoBin);
    gst_object_ref (GST_OBJECT (m_videoBin)); //Take ownership
    gst_object_sink (GST_OBJECT (m_videoBin));
    QByteArray tegraEnv = qgetenv("TEGRA_GST_OPENMAX");
    if (tegraEnv.isEmpty()) {
        //The videoplug element is the final element before the pluggable videosink
        m_videoplug = gst_element_factory_make ("identity", NULL);

        //Colorspace ensures that the output of the stream matches the input format accepted by our video sink
        m_colorspace = gst_element_factory_make ("ffmpegcolorspace", NULL);

        //Video scale is used to prepare the correct aspect ratio and scale.
        GstElement *videoScale = gst_element_factory_make ("videoscale", NULL);

        //We need a queue to support the tee from parent node
        GstElement *queue = gst_element_factory_make ("queue", NULL);

        if (queue && m_videoBin && videoScale && m_colorspace && videoSink && m_videoplug) {
        //Ensure that the bare essentials are prepared
            gst_bin_add_many (GST_BIN (m_videoBin), queue, m_colorspace, m_videoplug, videoScale, videoSink, NULL);
            bool success = false;
            //Video balance controls color/sat/hue in the YUV colorspace
            m_videoBalance = gst_element_factory_make ("videobalance", NULL);
            if (m_videoBalance) {
                // For video balance to work we have to first ensure that the video is in YUV colorspace,
                // then hand it off to the videobalance filter before finally converting it back to RGB.
                // Hence we nede a videoFilter to convert the colorspace before and after videobalance
                GstElement *m_colorspace2 = gst_element_factory_make ("ffmpegcolorspace", NULL);
                gst_bin_add_many(GST_BIN(m_videoBin), m_videoBalance, m_colorspace2, NULL);
                success = gst_element_link_many(queue, m_colorspace, m_videoBalance, m_colorspace2, videoScale, m_videoplug, videoSink, NULL);
            } else {
                //If video balance is not available, just connect to sink directly
                success = gst_element_link_many(queue, m_colorspace, videoScale, m_videoplug, videoSink, NULL);
            }
            if (success) {
                GstPad *videopad = gst_element_get_static_pad (queue, "sink");
                gst_element_add_pad (m_videoBin, gst_ghost_pad_new ("sink", videopad));
                gst_object_unref (videopad);
                QWidget *parentWidget = qobject_cast<QWidget*>(parent());
                if (parentWidget)
                    parentWidget->winId();  // Due to some existing issues with alien in 4.4,
                                        //  we must currently force the creation of a parent widget.
                m_isValid = true; //initialization ok, accept input
            }
        }
    } else {
        gst_bin_add_many (GST_BIN (m_videoBin), videoSink, NULL);
        GstPad *videopad = gst_element_get_static_pad (videoSink,"sink");
        gst_element_add_pad (m_videoBin, gst_ghost_pad_new ("sink", videopad));
        gst_object_unref (videopad);
        QWidget *parentWidget = qobject_cast<QWidget*>(parent());
        if (parentWidget)
            parentWidget->winId();  // Due to some existing issues with alien in 4.4,
                                    //  we must currently force the creation of a parent widget.
        m_isValid = true; //initialization ok, accept input
    }
}

void VideoWidget::paintEvent(QPaintEvent *event)
{
    Q_ASSERT(m_renderer);
    m_renderer->handlePaint(event);
}

void VideoWidget::setVisible(bool val) {
    Q_ASSERT(m_renderer);

    // Disable overlays for graphics view
    if (root() && window() && window()->testAttribute(Qt::WA_DontShowOnScreen) && !m_renderer->paintsOnWidget()) {
        m_backend->logMessage(QString("Widget rendering forced"), Backend::Info, this);
        GstElement *videoSink = m_renderer->videoSink();
        Q_ASSERT(videoSink);

        gst_element_set_state (videoSink, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_videoBin), videoSink);
        delete m_renderer;
        m_renderer = 0;

        // Use widgetRenderer as a fallback
        m_renderer = new WidgetRenderer(this);
        videoSink = m_renderer->videoSink();
        GstPad *videoPad = gst_element_get_static_pad(videoSink, "sink");
        g_signal_connect(videoPad, "notify::caps", G_CALLBACK(cb_capsChanged), this);
        gst_bin_add(GST_BIN(m_videoBin), videoSink);
        gst_element_link(m_videoplug, videoSink);
        gst_element_set_state (videoSink, GST_STATE_PAUSED);

    }
    QWidget::setVisible(val);
}

bool VideoWidget::event(QEvent *event)
{
    if (m_renderer && m_renderer->eventFilter(event))
        return true;
    return QWidget::event(event);
}

Phonon::VideoWidget::AspectRatio VideoWidget::aspectRatio() const
{
    return m_aspectRatio;
}

QSize VideoWidget::sizeHint() const
{
    if (!m_movieSize.isEmpty())
        return m_movieSize;
    else
        return QSize(640, 480);
}

void VideoWidget::setAspectRatio(Phonon::VideoWidget::AspectRatio aspectRatio)
{
    m_aspectRatio = aspectRatio;
    if (m_renderer)
        m_renderer->aspectRatioChanged(aspectRatio);
}

Phonon::VideoWidget::ScaleMode VideoWidget::scaleMode() const
{
    return m_scaleMode;
}

QRect VideoWidget::scaleToAspect(QRect srcRect, int w, int h) const
{
    float width = srcRect.width();
    float height = srcRect.width() * (float(h) / float(w));
    if (height > srcRect.height()) {
        height = srcRect.height();
        width = srcRect.height() * (float(w) / float(h));
    }
    return QRect(0, 0, (int)width, (int)height);
}

/***
 * Calculates the actual rectangle the movie will be presented with
 **/
QRect VideoWidget::calculateDrawFrameRect() const
{
    QRect widgetRect = rect();
    QRect drawFrameRect;
    // Set m_drawFrameRect to be the size of the smallest possible
    // rect conforming to the aspect and containing the whole frame:
    switch (aspectRatio()) {

    case Phonon::VideoWidget::AspectRatioWidget:
        drawFrameRect = widgetRect;
        // No more calculations needed.
        return drawFrameRect;

    case Phonon::VideoWidget::AspectRatio4_3:
        drawFrameRect = scaleToAspect(widgetRect, 4, 3);
        break;

    case Phonon::VideoWidget::AspectRatio16_9:
        drawFrameRect = scaleToAspect(widgetRect, 16, 9);
        break;

    case Phonon::VideoWidget::AspectRatioAuto:
    default:
        drawFrameRect = QRect(0, 0, movieSize().width(), movieSize().height());
        break;
    }

    // Scale m_drawFrameRect to fill the widget
    // without breaking aspect:
    float widgetWidth = widgetRect.width();
    float widgetHeight = widgetRect.height();
    float frameWidth = widgetWidth;
    float frameHeight = drawFrameRect.height() * float(widgetWidth) / float(drawFrameRect.width());

    switch (scaleMode()) {
    case Phonon::VideoWidget::ScaleAndCrop:
        if (frameHeight < widgetHeight) {
            frameWidth *= float(widgetHeight) / float(frameHeight);
            frameHeight = widgetHeight;
        }
        break;
    case Phonon::VideoWidget::FitInView:
    default:
        if (frameHeight > widgetHeight) {
            frameWidth *= float(widgetHeight) / float(frameHeight);
            frameHeight = widgetHeight;
        }
        break;
    }
    drawFrameRect.setSize(QSize(int(frameWidth), int(frameHeight)));
    drawFrameRect.moveTo(int((widgetWidth - frameWidth) / 2.0f),
                           int((widgetHeight - frameHeight) / 2.0f));
    return drawFrameRect;
}

void VideoWidget::setScaleMode(Phonon::VideoWidget::ScaleMode scaleMode)
{
    m_scaleMode = scaleMode;
    if (m_renderer)
        m_renderer->scaleModeChanged(scaleMode);
}

qreal VideoWidget::brightness() const
{
    return m_brightness;
}

qreal clampedValue(qreal val)
{
    if (val > 1.0 )
        return 1.0;
    else if (val < -1.0)
        return -1.0;
    else return val;
}

void VideoWidget::setBrightness(qreal newValue)
{
   GstElement *videoSink = m_renderer->videoSink();

   newValue = clampedValue(newValue);

    if (newValue == m_brightness)
        return;

    m_brightness = newValue;

    QByteArray tegraEnv = qgetenv("TEGRA_GST_OPENMAX");
    if (tegraEnv.isEmpty()) {
        if (m_videoBalance)
            g_object_set(G_OBJECT(m_videoBalance), "brightness", newValue, NULL); //gstreamer range is [-1, 1]
    } else {
        if (videoSink)
            g_object_set(G_OBJECT(videoSink), "brightness", newValue, NULL); //gstreamer range is [-1, 1]
    }
}

qreal VideoWidget::contrast() const
{
    return m_contrast;
}

void VideoWidget::setContrast(qreal newValue)
{

    GstElement *videoSink = m_renderer->videoSink();
    QByteArray tegraEnv = qgetenv("TEGRA_GST_OPENMAX");

    newValue = clampedValue(newValue);

    if (newValue == m_contrast)
        return;

    m_contrast = newValue;

    if (tegraEnv.isEmpty()) {
        if (m_videoBalance)
            g_object_set(G_OBJECT(m_videoBalance), "contrast", (newValue + 1.0), NULL); //gstreamer range is [0-2]
    } else {
       if (videoSink)
           g_object_set(G_OBJECT(videoSink), "contrast", (newValue + 1.0), NULL); //gstreamer range is [0-2]
    }
}

qreal VideoWidget::hue() const
{
    return m_hue;
}

void VideoWidget::setHue(qreal newValue)
{
    if (newValue == m_hue)
        return;

    newValue = clampedValue(newValue);

    m_hue = newValue;

    if (m_videoBalance)
        g_object_set(G_OBJECT(m_videoBalance), "hue", newValue, NULL); //gstreamer range is [-1, 1]
}

qreal VideoWidget::saturation() const
{
    return m_saturation;
}

void VideoWidget::setSaturation(qreal newValue)
{

    GstElement *videoSink = m_renderer->videoSink();

    newValue = clampedValue(newValue);

    if (newValue == m_saturation)
        return;

    m_saturation = newValue;

    QByteArray tegraEnv = qgetenv("TEGRA_GST_OPENMAX");
    if (tegraEnv.isEmpty()) {
        if (m_videoBalance)
            g_object_set(G_OBJECT(m_videoBalance), "saturation", newValue + 1.0, NULL); //gstreamer range is [0, 2]
    } else {
        if (videoSink)
            g_object_set(G_OBJECT(videoSink), "saturation", newValue + 1.0, NULL); //gstreamer range is [0, 2]
    }
}


void VideoWidget::setMovieSize(const QSize &size)
{
    m_backend->logMessage(QString("New video size %0 x %1").arg(size.width()).arg(size.height()), Backend::Info);
    if (size == m_movieSize)
        return;
    m_movieSize = size;
    widget()->updateGeometry();
    widget()->update();

    if (m_renderer)
        m_renderer->movieSizeChanged(m_movieSize);
}

void VideoWidget::keyPressEvent(QKeyEvent *event)
{
    GstElement *videosink = m_renderer->videoSink();
    if (GST_IS_NAVIGATION(videosink)) {
        GstNavigation *navigation = GST_NAVIGATION(videosink);
        if (navigation) {
            // TODO key code via xlib?
            gst_navigation_send_key_event(navigation, "key-pressed",
                                          event->text().toAscii());
        }
    }
    QWidget::keyPressEvent(event);
}

void VideoWidget::keyReleaseEvent(QKeyEvent *event)
{
    GstElement *videosink = m_renderer->videoSink();
    if (GST_IS_NAVIGATION(videosink)) {
        GstNavigation *navigation = GST_NAVIGATION(videosink);
        if (navigation) {
            // TODO key code via xlib?
            gst_navigation_send_key_event(navigation, "key-released",
                                          event->text().toAscii());
        }
    }
    QWidget::keyReleaseEvent(event);
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event)
{
    QRect frameRect = calculateDrawFrameRect();
    int x = event->x() - frameRect.x();
    int y = event->y() - frameRect.y();
    GstElement *videosink = m_renderer->videoSink();
    if (GST_IS_NAVIGATION(videosink)) {
        GstNavigation *navigation = GST_NAVIGATION(videosink);
        if (navigation) {
            gst_navigation_send_mouse_event(navigation, "mouse-move",
                                            0, x, y);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void VideoWidget::mousePressEvent(QMouseEvent *event)
{
    QRect frameRect = calculateDrawFrameRect();
    int x = event->x() - frameRect.x();
    int y = event->y() - frameRect.y();
    GstElement *videosink = m_renderer->videoSink();
    if (GST_IS_NAVIGATION(videosink)) {
        GstNavigation *navigation = GST_NAVIGATION(videosink);
        if (navigation) {
            gst_navigation_send_mouse_event(navigation, "mouse-button-press",
                                            1, x, y);
        }
    }
    QWidget::mousePressEvent(event);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event)
{
    QRect frameRect = calculateDrawFrameRect();
    int x = event->x() - frameRect.x();
    int y = event->y() - frameRect.y();
    GstElement *videosink = m_renderer->videoSink();
    if (GST_IS_NAVIGATION(videosink)) {
        GstNavigation *navigation = GST_NAVIGATION(videosink);
        if (navigation) {
            gst_navigation_send_mouse_event(navigation, "mouse-button-release",
                                            1, x, y);
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void VideoWidget::cb_capsChanged(GstPad *pad, GParamSpec *spec, gpointer data)
{
    Q_UNUSED(spec)
    //TODO: Original code disconnected the signal until source was changed again. Is that needed?
    //Also, it used a signal ID, which isn't needed since we can just disconnect based on the data
    //value (see Pipeline destructor for example)
    //g_signal_handler_disconnect(pad, media->capsHandler());
    VideoWidget *that = static_cast<VideoWidget*>(data);
    if (!GST_PAD_IS_LINKED(pad))
        return;
    GstState videoState;
    gst_element_get_state(that->videoElement(), &videoState, NULL, 1000);

    gint width;
    gint height;
    //FIXME: This sometimes gives a gstreamer warning. Feels like GStreamer shouldn't, and instead
    //just quietly return false, probably a bug.
    if (gst_video_get_size(pad, &width, &height)) {
        QMetaObject::invokeMethod(that, "setMovieSize", Q_ARG(QSize, QSize(width, height)));
    }
}

void VideoWidget::mouseOverActive(bool active)
{
    if (active)
        setCursor(Qt::PointingHandCursor);
    else
        setCursor(Qt::ArrowCursor);
}

}
} //namespace Phonon::Gstreamer

QT_END_NAMESPACE
#endif //QT_NO_PHONON_VIDEO

#include "moc_videowidget.cpp"
