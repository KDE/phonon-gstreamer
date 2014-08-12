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

#ifndef Phonon_GSTREAMER_DEVICEMANAGER_H
#define Phonon_GSTREAMER_DEVICEMANAGER_H

#include <phonon/audiooutputinterface.h>

#include <QtCore/QObject>
#include <QtCore/QTimer>

#include <gst/gstelement.h>

namespace Phonon {
namespace Gstreamer {

class Backend;
class DeviceManager;
class AbstractRenderer;
class VideoWidget;

/** \brief Container for information about devices supported by Gstreamer
 *
 * It includes an unique device identifier, a name identifier, a
 * description, a hardware identifier (may be a platform dependent device name),
 * and other relevant info.
 */
class DeviceInfo
{
public:
    enum Capability {
        None            = 0x0000,
        AudioOutput     = 0x0001,
        AudioCapture    = 0x0002,  // TODO
        VideoCapture    = 0x0004
    };
public:
    /**
     * Constructs a device info object and sets it's device identifiers.
     */
    explicit DeviceInfo(DeviceManager *, const QByteArray &deviceId,
                        quint16 caps, bool isAdvanced = true);

    int id() const;
    const QString& name() const;
    const QString& description() const;
    bool isAdvanced() const;
    void setAdvanced(bool advanced);
    const DeviceAccessList& accessList() const;
    void addAccess(const DeviceAccess &access);
    quint16 capabilities() const;
    void setCapabilities(quint16 cap);

private:
    void useGstElement(GstElement *element, const QByteArray &deviceId);

    int m_id;
    QString m_name;           // the preferred name for the device
    QString m_description;    // describes how to access the device (factory name, gst id)
    bool m_isAdvanced;
    DeviceAccessList m_accessList;
    quint16 m_capabilities;
};

/** \brief Keeps track of audio/video devices
 *
 * The device manager provides information about devices usable
 * with Gstreamer.
 *
 * It also provides methods for creating specific objects depending on
 * categories (audo sink, video renderer)
 */
class DeviceManager : public QObject {
    Q_OBJECT
public:
    DeviceManager(Backend *parent);
    virtual ~DeviceManager();

    /**
     * @returns a GstElement with a floating reference
     */
    GstElement *createAudioSink(Category category = NoCategory);

    AbstractRenderer *createVideoRenderer(VideoWidget *parent);
    QList<int> deviceIds(ObjectDescriptionType type) const;
    QHash<QByteArray, QVariant> deviceProperties(int id) const;
    const DeviceInfo *device(int id) const;

signals:
    void deviceAdded(int);
    void deviceRemoved(int);

public slots:
    void updateDeviceList();

private:
    static bool listContainsDevice(const QList<DeviceInfo> &list, int id);
    GstElement *createGNOMEAudioSink(Category category);
    bool canOpenDevice(GstElement *element) const;

private:
    Backend *m_backend;
    QList<DeviceInfo> m_devices;
    QTimer m_devicePollTimer;
    QByteArray m_audioSink;
    QByteArray m_videoSinkWidget;
};
}
} // namespace Phonon::Gstreamer

#endif // Phonon_GSTREAMER_DEVICEMANAGER_H
