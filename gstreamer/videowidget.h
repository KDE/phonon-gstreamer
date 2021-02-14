/*  This file is part of the KDE project.

    Copyright (C) 2    //Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).007 Nokia Corporation and/or its subsidiary(-ies).
    Copyright (C) 2012 Anssi Hannula <anssi.hannula@iki.fi>

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

#ifndef Phonon_GSTREAMER_VIDEOWIDGET_H
#define Phonon_GSTREAMER_VIDEOWIDGET_H

#include <phonon/videowidgetinterface.h>

#include "medianode.h"

#ifndef QT_NO_PHONON_VIDEO
class QString;

namespace Phonon
{
namespace Gstreamer
{

class AbstractRenderer;
class Backend;

class VideoWidget : public QWidget, public Phonon::VideoWidgetInterface44, public MediaNode
{
    Q_OBJECT
    Q_INTERFACES(Phonon::VideoWidgetInterface44 Phonon::Gstreamer::MediaNode)
public:
    explicit VideoWidget(Backend *backend, QWidget *parent = 0);
    ~VideoWidget();

    void setupVideoBin();
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void setVisible(bool) Q_DECL_OVERRIDE;

    Phonon::VideoWidget::AspectRatio aspectRatio() const Q_DECL_OVERRIDE;
    void setAspectRatio(Phonon::VideoWidget::AspectRatio aspectRatio) Q_DECL_OVERRIDE;
    Phonon::VideoWidget::ScaleMode scaleMode() const Q_DECL_OVERRIDE;
    void setScaleMode(Phonon::VideoWidget::ScaleMode) Q_DECL_OVERRIDE;
    qreal brightness() const Q_DECL_OVERRIDE;
    void setBrightness(qreal) Q_DECL_OVERRIDE;
    qreal contrast() const Q_DECL_OVERRIDE;
    void setContrast(qreal) Q_DECL_OVERRIDE;
    qreal hue() const Q_DECL_OVERRIDE;
    void setHue(qreal) Q_DECL_OVERRIDE;
    qreal saturation() const Q_DECL_OVERRIDE;
    void setSaturation(qreal) Q_DECL_OVERRIDE;
    QSize sizeHint() const Q_DECL_OVERRIDE;
    QRect scaleToAspect(QRect srcRect, int w, int h) const;
    QRect calculateDrawFrameRect() const;
    QImage snapshot() const Q_DECL_OVERRIDE;

    QSize movieSize() const {
        return m_movieSize;
    }

    GstElement* videoElement() const Q_DECL_OVERRIDE {
        return m_videoBin;
    }

    bool event(QEvent *) Q_DECL_OVERRIDE;

    QWidget *widget() Q_DECL_OVERRIDE {
        return this;
    }

    static void cb_capsChanged(GstPad *pad, GParamSpec *spec, gpointer data);

    void finalizeLink() Q_DECL_OVERRIDE;
    void prepareToUnlink() Q_DECL_OVERRIDE;

public slots:
    void setMovieSize(const QSize &size);
    void mouseOverActive(bool active);
    void syncX();

protected:
    virtual void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    virtual void keyReleaseEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    virtual void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    virtual void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    virtual void mouseReleaseEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

    GstElement *m_videoBin;
    QSize m_movieSize;
    AbstractRenderer *m_renderer;

private slots:
    void updateWindowID();

private:
    Phonon::VideoWidget::AspectRatio m_aspectRatio;
    qreal m_brightness, m_hue, m_contrast, m_saturation;
    Phonon::VideoWidget::ScaleMode m_scaleMode;

    GstElement *m_videoBalance;
    GstElement *m_colorspace;
    GstElement *m_videoplug;
};

}
} //namespace Phonon::Gstreamer
#endif //QT_NO_PHONON_VIDEO
#endif // Phonon_GSTREAMER_VIDEOWIDGET_H
