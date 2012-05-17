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

#include <gst/interfaces/propertyprobe.h>
#include "phonon-config-gstreamer.h"
#include "devicemanager.h"
#include "backend.h"
#include "debug.h"
#include "gsthelper.h"
#include "videowidget.h"
#ifdef OPENGL_FOUND
#include "glrenderer.h"
#endif
#include "widgetrenderer.h"
#include "x11renderer.h"
#include <phonon/pulsesupport.h>

#include <QtCore/QSettings>

/*
 * This class manages the list of currently
 * active output devices
 */

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

/*
 * Device Info
 */

DeviceInfo::DeviceInfo(DeviceManager *manager, const QByteArray &deviceId,
                       quint16 caps, bool isAdvanced)
        : m_isAdvanced(isAdvanced), m_capabilities(caps)
{
    // Get an unique integer id for each device
    static int deviceCounter = 0;
    m_id = deviceCounter ++;

    if (caps & VideoCapture) {
        // Get a preferred name from the device
        if (deviceId == "default") {
            m_name = "Default";
            m_description = "Default video capture device";
        } else {
            GstElement *dev = gst_element_factory_make("v4l2src", NULL);
            useGstElement(dev, deviceId);
        }
    }

    if (caps & AudioOutput) {
        // This should never be called when PulseAudio is active.
        Q_ASSERT(!PulseSupport::getInstance()->isActive());

        // Get a preferred name from the device
        if (deviceId == "default") {
            m_name = "Default";
            m_description = "Default audio device";
        } else {
            GstElement *aSink = manager->createAudioSink();
            useGstElement(aSink, deviceId);
        }
    }

    // A default device should never be advanced
    if (deviceId == "default")
        m_isAdvanced = false;
}

void DeviceInfo::useGstElement(GstElement *element, const QByteArray &deviceId)
{
    if (!element)
        return;

    gchar *deviceName = NULL;
    if (GST_IS_PROPERTY_PROBE(element) && gst_property_probe_get_property(GST_PROPERTY_PROBE(element), "device")) {
        g_object_set(G_OBJECT(element), "device", deviceId.constData(), NULL);
        g_object_get(G_OBJECT(element), "device-name", &deviceName, NULL);
        m_name = QString(deviceName);

        if (m_description.isEmpty()) {
            // Construct a description by using the factory name and the device id
            GstElementFactory *factory = gst_element_get_factory(element);
            const gchar *factoryName = gst_element_factory_get_longname(factory);
            m_description = QString(factoryName) + ": " + deviceId;
        }

        g_free(deviceName);
        gst_element_set_state(element, GST_STATE_NULL);
        gst_object_unref(element);
    }
}

int DeviceInfo::id() const
{
    return m_id;
}

const QString& DeviceInfo::name() const
{
    return m_name;
}

const QString& DeviceInfo::description() const
{
    return m_description;
}

bool DeviceInfo::isAdvanced() const
{
    return m_isAdvanced;
}

void DeviceInfo::setAdvanced(bool advanced)
{
    m_isAdvanced = advanced;
}

const DeviceAccessList& DeviceInfo::accessList() const
{
    return m_accessList;
}

void DeviceInfo::addAccess(const DeviceAccess& access)
{
    m_accessList.append(access);
}

quint16 DeviceInfo::capabilities() const
{
    return m_capabilities;
}

void DeviceInfo::setCapabilities(quint16 cap)
{
    m_capabilities = cap;
}


/*
 * Device Manager
 */

DeviceManager::DeviceManager(Backend *backend)
        : QObject(backend)
        , m_backend(backend)
{
    QSettings settings(QLatin1String("Trolltech"));
    settings.beginGroup(QLatin1String("Qt"));

    PulseSupport *pulse = PulseSupport::getInstance();

    m_videoSinkWidget = qgetenv("PHONON_GST_VIDEOMODE");
    if (m_videoSinkWidget.isEmpty()) {
        m_videoSinkWidget = settings.value(QLatin1String("videomode"), "Auto").toByteArray().toLower();
    }

    if (m_backend->isValid())
        updateDeviceList();
}

DeviceManager::~DeviceManager()
{
}

bool DeviceManager::canOpenDevice(GstElement *element) const
{
    if (!element)
        return false;

    if (gst_element_set_state(element, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS)
        return true;

    const QList<QByteArray> &list = GstHelper::extractProperties(element, "device");
    foreach (const QByteArray &gstId, list) {
        GstHelper::setProperty(element, "device", gstId);
        if (gst_element_set_state(element, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS) {
            return true;
        }
    }

    gst_element_set_state(element, GST_STATE_NULL);
    return false;
}

/*
*
* Returns an audio sink.
*
* If pulse is active, we use pulse. Otherwise, we find something else suitable.
*
*/
GstElement *DeviceManager::createAudioSink(Category category)
{
    GstElement *sink = 0;

    if (m_backend && m_backend->isValid())
    {
      if (PulseSupport::getInstance()->isActive()) {
        sink = gst_element_factory_make("pulsesink", NULL);
        qDebug() << "Congrats! You're using pulseaudio!";
      } else {
        // FIXME: Implement audio sink discovery through registry searching
        sink = gst_element_factory_make("alsasink", NULL);
        qDebug() << "Well, you're stuck with ALSA.";
      }
    }
    Q_ASSERT(sink);
    return sink;
}

#ifndef QT_NO_PHONON_VIDEO
AbstractRenderer *DeviceManager::createVideoRenderer(VideoWidget *parent)
{
#if !defined(QT_NO_OPENGL) && !defined(QT_OPENGL_ES) && defined(OPENGL_FOUND)
    if (m_videoSinkWidget == "opengl") {
        return new GLRenderer(parent);
    } else
 #endif
    if (m_videoSinkWidget == "software") {
        return new WidgetRenderer(parent);
    }
#ifndef Q_WS_QWS
    else if (m_videoSinkWidget == "xwindow") {
        return new X11Renderer(parent);
    } else {
        GstElementFactory *srcfactory = gst_element_factory_find("ximagesink");
        if (srcfactory) {
            return new X11Renderer(parent);
        }
    }
#endif
    return new WidgetRenderer(parent);
}
#endif //QT_NO_PHONON_VIDEO

QList<int> DeviceManager::deviceIds(ObjectDescriptionType type)
{
    DeviceInfo::Capability capability = DeviceInfo::None;
    switch (type) {
    case Phonon::AudioOutputDeviceType:
        capability = DeviceInfo::AudioOutput;
        break;
    case Phonon::AudioCaptureDeviceType:
        capability = DeviceInfo::AudioCapture;
        break;
    case Phonon::VideoCaptureDeviceType:
        capability = DeviceInfo::VideoCapture;
        break;
    default: ;
    }

    QList<int> ids;
    foreach (const DeviceInfo &device, m_devices) {
        if (device.capabilities() & capability)
            ids.append(device.id());
    }

    return ids;
}

QHash<QByteArray, QVariant> DeviceManager::deviceProperties(int id)
{
    QHash<QByteArray, QVariant> properties;

    foreach (const DeviceInfo &device, m_devices) {
        if (device.id() == id) {
            properties.insert("name", device.name());
            properties.insert("description", device.description());
            properties.insert("isAdvanced", device.isAdvanced());
            properties.insert("deviceAccessList", QVariant::fromValue<Phonon::DeviceAccessList>(device.accessList()));
            properties.insert("discovererIcon", QLatin1String("phonon-gstreamer"));

            if (device.capabilities() & DeviceInfo::AudioOutput) {
                properties.insert("icon", QLatin1String("audio-card"));
            }

            if (device.capabilities() & DeviceInfo::AudioCapture) {
                properties.insert("hasaudio", true);
                properties.insert("icon", QLatin1String("audio-input-microphone"));
            }

            if (device.capabilities() & DeviceInfo::VideoCapture) {
                properties.insert("hasvideo", true);
                properties.insert("icon", QLatin1String("camera-web"));
            }
            break;
        }
    }

    return properties;
}

/**
 * \param id The identifier for the device
 * \return Pointer to DeviceInfo, or NULL if the id is invalid
 */
const DeviceInfo *DeviceManager::device(int id) const
{
    for (int i = 0; i < m_devices.size(); i ++) {
        if (m_devices[i].id() == id)
            return &m_devices[i];
    }

    return NULL;
}

/**
 * Updates the current list of active devices
 */
void DeviceManager::updateDeviceList()
{
    QList<DeviceInfo> newDeviceList;
    QList<QByteArray> names;

    /*
     * Audio output
     */
    GstElement *audioSink = createAudioSink();
    if (audioSink) {
        if (!PulseSupport::getInstance()->isActive()) {
            // If we're using pulse, the PulseSupport class takes care of things for us.
            names = GstHelper::extractProperties(audioSink, "device");
            names.prepend("default");
        }

        /* Determine what factory was used to create the sink, to know what to put in the
         * device access list */
        GstElementFactory *factory = gst_element_get_factory(audioSink);
        const gchar *factoryName = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
        QByteArray driver; // means sound system

        for (int i = 0; i < names.size(); ++i) {
            DeviceInfo deviceInfo(this, names[i], DeviceInfo::AudioOutput);
            deviceInfo.addAccess(DeviceAccess("pulse", names[i]));
            newDeviceList.append(deviceInfo);
        }

        gst_element_set_state(audioSink, GST_STATE_NULL);
        gst_object_unref(audioSink);
    }

    /*
     * Video capture
     */
    GstElement *captureDevice = gst_element_factory_make("v4l2src", NULL);
    if (captureDevice) {
        names = GstHelper::extractProperties(captureDevice, "device");

        for (int i = 0; i < names.size(); ++i) {
            DeviceInfo deviceInfo(this, names[i], DeviceInfo::VideoCapture);
            deviceInfo.addAccess(DeviceAccess("v4l2", names[i]));
            newDeviceList.append(deviceInfo);
        }

        gst_element_set_state(captureDevice, GST_STATE_NULL);
        gst_object_unref(captureDevice);
    }

    /*
     * Compares the list with the devices available at the moment with the last list. If
     * a new device is seen, a signal is emitted. If a device dissapeared, another signal
     * is emitted.
     */

    // Search for added devices
    for (int i = 0; i < newDeviceList.count(); ++i) {
        int id = newDeviceList[i].id();
        if (!listContainsDevice(m_devices, id)) {
            // This is a new device, add it
            m_devices.append(newDeviceList[i]);
            emit deviceAdded(id);

            debug() << "Found new device" << newDeviceList[i].name();
        }
    }

    // Search for removed devices
    for (int i = m_devices.count() - 1; i >= 0; --i) {
        int id = m_devices[i].id();
        if (!listContainsDevice(newDeviceList, id)) {
            debug() << "Lost device" << m_devices[i].name();

            emit deviceRemoved(id);
            m_devices.removeAt(i);
        }
    }
}

bool DeviceManager::listContainsDevice(const QList<DeviceInfo> &list, int id)
{
    foreach (const DeviceInfo &d, list) {
        if (d.id() == id)
            return true;
    }
    return false;
}

}
}

QT_END_NAMESPACE
