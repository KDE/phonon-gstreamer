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

#ifndef Phonon_GSTREAMER_BACKEND_H
#define Phonon_GSTREAMER_BACKEND_H

#include <phonon/objectdescription.h>
#include <phonon/backendinterface.h>

#include <QtCore/QList>
#include <QtCore/QStringList>

namespace Phonon
{
namespace Gstreamer
{

class AudioOutput;
class DeviceManager;
class EffectManager;
class MediaObject;

class Backend : public QObject, public BackendInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.phonon.gstreamer" FILE "phonon-gstreamer.json")
    Q_INTERFACES(Phonon::BackendInterface)

public:
    explicit Backend(QObject *parent = 0, const QVariantList & = QVariantList());
    virtual ~Backend();

    DeviceManager* deviceManager() const;
    EffectManager* effectManager() const;

    QObject *createObject(BackendInterface::Class, QObject *parent, const QList<QVariant> &args) Q_DECL_OVERRIDE;

    QStringList availableMimeTypes() const Q_DECL_OVERRIDE;

    QList<int> objectDescriptionIndexes(ObjectDescriptionType type) const Q_DECL_OVERRIDE;
    QHash<QByteArray, QVariant> objectDescriptionProperties(ObjectDescriptionType type, int index) const Q_DECL_OVERRIDE;

    bool startConnectionChange(QSet<QObject *>) Q_DECL_OVERRIDE;
    bool connectNodes(QObject *, QObject *) Q_DECL_OVERRIDE;
    bool disconnectNodes(QObject *, QObject *) Q_DECL_OVERRIDE;
    bool endConnectionChange(QSet<QObject *>) Q_DECL_OVERRIDE;

    // 'retry' indicates that we'd like to check the deps after a registry rebuild
    bool checkDependencies(bool retry = false) const;

Q_SIGNALS:
    void objectDescriptionChanged(ObjectDescriptionType);

private:
    bool isValid() const;
    bool supportsVideo() const;

    DeviceManager *m_deviceManager;
    EffectManager *m_effectManager;
    bool m_isValid;
};

}
} // namespace Phonon::Gstreamer

#endif // Phonon_GSTREAMER_BACKEND_H
