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

#ifndef Phonon_GSTREAMER_VOLUMEFADEREFFECT_H
#define Phonon_GSTREAMER_VOLUMEFADEREFFECT_H

#include "effect.h"

#include <phonon/volumefaderinterface.h>

#include <QtCore/QTime>
class QTimeLine;

QT_BEGIN_NAMESPACE
#ifndef QT_NO_PHONON_VOLUMEFADEREFFECT
namespace Phonon
{
namespace Gstreamer
{
class VolumeFaderEffect : public Effect, public VolumeFaderInterface
{
    Q_OBJECT
    Q_INTERFACES(Phonon::VolumeFaderInterface)

public:
    explicit VolumeFaderEffect(Backend *backend, QObject *parent = 0);
    ~VolumeFaderEffect();

    GstElement* createEffectBin();
    GstElement *audioElement() { return m_effectBin; }

    // VolumeFaderInterface:
    float volume() const;
    Phonon::VolumeFaderEffect::FadeCurve fadeCurve() const;
    void setFadeCurve(Phonon::VolumeFaderEffect::FadeCurve fadeCurve);
    void fadeTo(float volume, int fadeTime);
    void setVolume(float v);

private slots:
    void slotSetVolume(qreal v);

private:
    void abortFade();
    inline void setVolumeInternal(float v);

    Phonon::VolumeFaderEffect::FadeCurve m_fadeCurve;
    gdouble m_fadeFromVolume;
    gdouble m_fadeToVolume;
    QTimeLine *m_fadeTimeline;

};
}} //namespace Phonon::Gstreamer
#endif //QT_NO_PHONON_VOLUMEFADEREFFECT
QT_END_NAMESPACE

#endif // Phonon_GSTREAMER_VOLUMEFADEREFFECT_H
