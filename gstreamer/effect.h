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

#ifndef Phonon_GSTREAMER_EFFECT_H
#define Phonon_GSTREAMER_EFFECT_H

#include "medianode.h"

#include <phonon/effectparameter.h>
#include <phonon/effectinterface.h>

#include <QtCore/QObject>

#include <gst/gstelement.h>

#ifndef QT_NO_PHONON_EFFECT
namespace Phonon
{
namespace Gstreamer
{
    class EffectInfo;

    class Effect : public QObject,
                   public Phonon::EffectInterface,
                   public MediaNode
    {
        Q_OBJECT
        Q_INTERFACES(Phonon::EffectInterface Phonon::Gstreamer::MediaNode)
        public:
            Effect (Backend *backend, QObject *parent, NodeDescription description);
            virtual ~Effect ();

            virtual QList<EffectParameter> parameters() const Q_DECL_OVERRIDE;
            virtual QVariant parameterValue(const EffectParameter &) const Q_DECL_OVERRIDE;
            virtual void setParameterValue(const EffectParameter &, const QVariant &) Q_DECL_OVERRIDE;

            virtual void init();
            virtual void setupEffectParams();

        protected:
            virtual GstElement* createEffectBin() = 0;

            void setEffectElement(GstElement *effectElement);

            GstElement *effectElement() const {
                return m_effectElement;
            }

            GstElement *effectBin() const {
                return m_effectBin;
            }

        private:
            GstElement *m_effectBin;
            GstElement *m_effectElement;
            QList<Phonon::EffectParameter> m_parameterList;
    };
}
} //namespace Phonon::Gstreamer
#endif //QT_NO_PHONON_EFFECT

#endif // Phonon_GSTREAMER_EFFECT_H
