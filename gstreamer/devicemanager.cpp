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

#include "devicemanager.h"

#include "phonon-config-gstreamer.h" // krazy:exclude=includes
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

#include <gst/gst.h>

#include <QtCore/QSettings>

/*
 * This class manages the list of currently
 * active output devices
 */

namespace Phonon
{
namespace Gstreamer
{

/*
 * Device Info
 */

DeviceInfo::DeviceInfo(DeviceManager *manager, const QByteArray &deviceId,
                       quint16 caps, bool isAdvanced)
        : m_isAdvanced(isAdvanced)
        , m_capabilities(caps)
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
    if (deviceId == "default") {
        m_isAdvanced = false;
    }
}

void DeviceInfo::useGstElement(GstElement *element, const QByteArray &deviceId)
{
    if (!element) {
        return;
    }

    gchar *deviceName = NULL;
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(element), "device")){
        g_object_set(G_OBJECT(element), "device", deviceId.constData(), NULL);
        g_object_get(G_OBJECT(element), "device-name", &deviceName, NULL);
        m_name = QString(deviceName);

        if (m_description.isEmpty()) {
            // Construct a description by using the factory name and the device id
            GstElementFactory *factory = gst_element_get_factory(element);
            const gchar *factoryName = gst_element_factory_get_metadata(factory, "Long-name");
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
    m_audioSink = qgetenv("PHONON_GST_AUDIOSINK");
    if (m_audioSink.isEmpty())
        m_audioSink = settings.value(QLatin1String("audiosink"), "Auto").toByteArray().toLower();

    if ("pulsesink" == m_audioSink && !pulse->isActive()) {
        // If pulsesink is specifically requested, but not active, then
        // fall back to auto.
        m_audioSink = "auto";
    } else if (m_audioSink == "auto" && pulse->isActive()) {
        // We favour specific PA support if it's active and we're in 'auto' mode
        // (although it may still be disabled if the pipeline cannot be made)
        m_audioSink = "pulsesink";
    } else if (m_audioSink != "pulsesink") {
        // Otherwise, PA should not be used.
        pulse->enable(false);
    }

    m_videoSinkWidget = qgetenv("PHONON_GST_VIDEOMODE");
    if (m_videoSinkWidget.isEmpty()) {
        m_videoSinkWidget = settings.value(QLatin1String("videomode"), "Auto").toByteArray().toLower();
    }

    updateDeviceList();
}

DeviceManager::~DeviceManager()
{
}

/***
* Returns a Gst Audiosink based on GNOME configuration settings,
* or 0 if the element is not available.
*/
GstElement *DeviceManager::createGNOMEAudioSink(Category category)
{
    GstElement *sink = gst_element_factory_make("gconfaudiosink", NULL);

    if (sink) {

        // set profile property on the gconfaudiosink to "music and movies"
        if (g_object_class_find_property(G_OBJECT_GET_CLASS (sink), "profile")) {
            switch (category) {
            case NotificationCategory:
                g_object_set(G_OBJECT (sink), "profile", 0, NULL); // 0 = 'sounds'
                break;
            case CommunicationCategory:
                g_object_set(G_OBJECT (sink), "profile", 2, NULL); // 2 = 'chat'
                break;
            default:
                g_object_set(G_OBJECT (sink), "profile", 1, NULL); // 1 = 'music and movies'
                break;
            }
        }
    }
    return sink;
}


bool DeviceManager::canOpenDevice(GstElement *element) const
{
    if (!element) {
        return false;
    }

    if (gst_element_set_state(element, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS) {
        return true;
    }

    const QList<QByteArray> &list = GstHelper::extractProperties(element, "device");
    foreach (const QByteArray &gstId, list) {
        GstHelper::setProperty(element, "device", gstId);
        if (gst_element_set_state(element, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS) {
            return true;
        }
    }
    // FIXME: the above can still fail for a valid alsasink because list only contains entries of
    // the form "hw:X,Y". Would be better to use "default:X" or "dmix:X,Y"

    gst_element_set_state(element, GST_STATE_NULL);
    return false;
}

/*
*
* Returns a GstElement with a valid audio sink
* based on the current value of PHONON_GSTREAMER_DRIVER
*
* Allowed values are auto (default), alsa, oss and ess
* does not exist
*
* If no real sound sink is available a fakesink will be returned
*/
GstElement *DeviceManager::createAudioSink(Category category)
{
    GstElement *sink = 0;

    if (m_audioSink == "auto") { //this is the default value
        //### TODO : get equivalent KDE settings here

        if (!qgetenv("GNOME_DESKTOP_SESSION_ID").isEmpty()) {
            sink = createGNOMEAudioSink(category);
            if (canOpenDevice(sink)) {
                debug() << "AudioOutput using gconf audio sink";
            } else if (sink) {
                gst_object_unref(sink);
                sink = 0;
            }
        }

        if (!sink) {
            sink = gst_element_factory_make("alsasink", NULL);
            if (canOpenDevice(sink)) {
                debug() << "AudioOutput using alsa audio sink";
            } else if (sink) {
                gst_object_unref(sink);
                sink = 0;
            }
        }

        if (!sink) {
            sink = gst_element_factory_make("autoaudiosink", NULL);
            if (canOpenDevice(sink)) {
                debug() << "AudioOutput using auto audio sink";
            } else if (sink) {
                gst_object_unref(sink);
                sink = 0;
            }
        }

        if (!sink) {
            sink = gst_element_factory_make("osssink", NULL);
            if (canOpenDevice(sink)) {
                debug() << "AudioOutput using oss audio sink";
            } else if (sink) {
                gst_object_unref(sink);
                sink = 0;
            }
        }
    } else if (m_audioSink == "fake") {
        //do nothing as a fakesink will be created by default
    } else if (!m_audioSink.isEmpty()) { //Use a custom sink
        sink = gst_element_factory_make(m_audioSink, NULL);
        if (canOpenDevice(sink)) {
            debug() << "AudioOutput using" << QString::fromUtf8(m_audioSink);
        } else {
            if (sink) {
                gst_object_unref(sink);
                sink = 0;
            }
            if ("pulsesink" == m_audioSink) {
                // We've tried to use PulseAudio support, but the GST plugin
                // doesn't exits. Let's try again, but not use PA support this time.
                warning() << "PulseAudio support failed. Falling back to 'auto'";
                PulseSupport::getInstance()->enable(false);
                m_audioSink = "auto";
                sink = createAudioSink();
            }
        }
    }

    if (!sink) { //no suitable sink found so we'll make a fake one
        sink = gst_element_factory_make("fakesink", NULL);
        if (sink) {
            warning() << "AudioOutput Using fake audio sink";
            //without sync the sink will pull the pipeline as fast as the CPU allows
            g_object_set(G_OBJECT(sink), "sync", TRUE, NULL);
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
        if (device.capabilities() & capability) {
            ids.append(device.id());
        }
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
        if (m_devices[i].id() == id) {
            return &m_devices[i];
        }
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
        if (g_strcmp0(factoryName, "alsasink") == 0) {
            driver = "alsa";
        } else if (g_strcmp0(factoryName, "pulsesink") == 0) {
            driver = "pulse";
        } else if (g_strcmp0(factoryName, "osssink") == 0) {
            driver = "oss";
        } else if (g_strcmp0(factoryName, "fakesink") == 0) {
            driver = "fake";
        }
        if (driver.isEmpty() && !names.isEmpty()) {
            warning() << "Unknown sound system for device" << names.first();
        }

        for (int i = 0; i < names.size(); ++i) {
            DeviceInfo deviceInfo(this, names[i], DeviceInfo::AudioOutput);
            deviceInfo.addAccess(DeviceAccess(driver, names[i]));
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
        const int id = newDeviceList[i].id();
        if (!listContainsDevice(m_devices, id)) {
            // This is a new device, add it
            m_devices.append(newDeviceList[i]);
            emit deviceAdded(id);

            debug() << "Found new device" << newDeviceList[i].name();
        }
    }

    // Search for removed devices
    for (int i = m_devices.count() - 1; i >= 0; --i) {
        const int id = m_devices[i].id();
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
        if (d.id() == id) {
            return true;
        }
    }
    return false;
}

}
}
