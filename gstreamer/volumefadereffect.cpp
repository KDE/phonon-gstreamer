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

#include "volumefadereffect.h"
#include "debug.h"

#include <gst/gstbin.h>
#include <gst/gstghostpad.h>
#include <gst/gstutils.h>

#include <QtCore/QTimeLine>

QT_BEGIN_NAMESPACE

#ifndef QT_NO_PHONON_VOLUMEFADEREFFECT
namespace Phonon
{
namespace Gstreamer
{
VolumeFaderEffect::VolumeFaderEffect(Backend *backend, QObject *parent)
    : Effect(backend, parent, AudioSource | AudioSink)
    , m_fadeCurve(Phonon::VolumeFaderEffect::Fade3Decibel)
    , m_fadeFromVolume(0)
    , m_fadeToVolume(0)
{
    m_effectElement = gst_element_factory_make ("volume", NULL);
    if (m_effectElement)
        init();
    m_fadeTimeline = new QTimeLine(1000, this);
    connect(m_fadeTimeline, SIGNAL(valueChanged(qreal)), this, SLOT(slotSetVolume(qreal)));
}

VolumeFaderEffect::~VolumeFaderEffect()
{
}

GstElement* VolumeFaderEffect::createEffectBin()
{
    GstElement *audioBin = gst_bin_new(NULL);

    // We need a queue to handle tee-connections from parent node
    GstElement *queue= gst_element_factory_make ("queue", NULL);
    gst_bin_add(GST_BIN(audioBin), queue);

    GstElement *mconv= gst_element_factory_make ("audioconvert", NULL);
    gst_bin_add(GST_BIN(audioBin), mconv);
    gst_bin_add(GST_BIN(audioBin), m_effectElement);

    // Link src pad
    GstPad *srcPad= gst_element_get_static_pad (m_effectElement, "src");
    gst_element_add_pad (audioBin, gst_ghost_pad_new ("src", srcPad));
    gst_object_unref (srcPad);

    // Link sink pad
    gst_element_link_many(queue, mconv, m_effectElement, NULL);
    GstPad *sinkpad = gst_element_get_static_pad (queue, "sink");
    gst_element_add_pad (audioBin, gst_ghost_pad_new ("sink", sinkpad));
    gst_object_unref (sinkpad);
    return audioBin;
}

float VolumeFaderEffect::volume() const
{
    gdouble val = 1.0;
    if (m_effectElement)
        g_object_get(G_OBJECT(m_effectElement), "volume", &val, NULL);
    return (float)val;
}

void VolumeFaderEffect::slotSetVolume(qreal volume)
{
    float gstVolume = m_fadeFromVolume + (volume * (m_fadeToVolume - m_fadeFromVolume));
    setVolumeInternal(gstVolume);
}

Phonon::VolumeFaderEffect::FadeCurve VolumeFaderEffect::fadeCurve() const
{
    return m_fadeCurve;
}

void VolumeFaderEffect::setFadeCurve(Phonon::VolumeFaderEffect::FadeCurve pFadeCurve)
{
    m_fadeCurve = pFadeCurve;
    QEasingCurve fadeCurve;
    switch(pFadeCurve) {
        case Phonon::VolumeFaderEffect::Fade3Decibel:
            fadeCurve = QEasingCurve::InQuad;
            break;
        case Phonon::VolumeFaderEffect::Fade6Decibel:
            fadeCurve = QEasingCurve::Linear;
            break;
        case Phonon::VolumeFaderEffect::Fade9Decibel:
            fadeCurve = QEasingCurve::OutCubic;
            break;
        case Phonon::VolumeFaderEffect::Fade12Decibel:
            fadeCurve = QEasingCurve::OutQuart;
            break;
    }
    m_fadeTimeline->setEasingCurve(fadeCurve);
}

void VolumeFaderEffect::fadeTo(float targetVolume, int fadeTime)
{
    abortFade();
    m_fadeToVolume = targetVolume;
    g_object_get(G_OBJECT(m_effectElement), "volume", &m_fadeFromVolume, NULL);

    // Don't call QTimeLine::setDuration() with zero.
    // It is not supported and breaks fading.
    if (fadeTime <= 0) {
        setVolumeInternal(targetVolume);
        return;
    }

    m_fadeTimeline->setDuration(fadeTime);
    m_fadeTimeline->start();
}

void VolumeFaderEffect::setVolume(float v)
{
    abortFade();
    setVolumeInternal(v);
}

void VolumeFaderEffect::abortFade()
{
    m_fadeTimeline->stop();
}

void VolumeFaderEffect::setVolumeInternal(float v)
{
    g_object_set(G_OBJECT(m_effectElement), "volume", (gdouble)v, NULL);
    debug() << "Fading to" << v;
}

}} //namespace Phonon::Gstreamer
#endif //QT_NO_PHONON_VOLUMEFADEREFFECT
QT_END_NAMESPACE

#include "moc_volumefadereffect.cpp"
