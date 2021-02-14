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

#ifndef Phonon_GSTREAMER_X11RENDERER_H
#define Phonon_GSTREAMER_X11RENDERER_H

#include "abstractrenderer.h"

#ifndef Q_WS_QWS

class QString;

namespace Phonon
{
namespace Gstreamer
{

class OverlayWidget;
class VideoWidget;

class X11Renderer : public AbstractRenderer
{
public:
    X11Renderer(VideoWidget *videoWidget);
    ~X11Renderer();
    void handlePaint(QPaintEvent *event) Q_DECL_OVERRIDE;
    void aspectRatioChanged(Phonon::VideoWidget::AspectRatio aspectRatio) Q_DECL_OVERRIDE;
    void scaleModeChanged(Phonon::VideoWidget::ScaleMode scaleMode) Q_DECL_OVERRIDE;
    void movieSizeChanged(const QSize &movieSize) Q_DECL_OVERRIDE;
    bool eventFilter(QEvent *) Q_DECL_OVERRIDE;
    bool paintsOnWidget() const Q_DECL_OVERRIDE {
        return false;
    }
    bool overlaySet() const {
        return m_overlaySet;
    }
    void setOverlay();
    void windowExposed();
private:
    GstElement *createVideoSink();

    OverlayWidget *m_renderWidget;
    bool m_overlaySet = false;
};

}
} //namespace Phonon::Gstreamer

#endif // Q_WS_QWS

#endif // Phonon_GSTREAMER_X11RENDERER_H
