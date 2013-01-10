/*  This file is part of the KDE project.

    Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
    Copyright (C) 2008 Matthias Kretz <kretz@kde.org>

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

#include "audiooutput.h"
#include "backend.h"
#include "debug.h"
#include "devicemanager.h"
#include "mediaobject.h"
#include "gsthelper.h"
#include <phonon/audiooutput.h>

#include <QtCore/QStringBuilder>

#include <gst/gstbin.h>
#include <gst/gstghostpad.h>
#include <gst/gstutils.h>

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{
AudioOutput::AudioOutput(Backend *backend, QObject *parent)
        : QObject(parent)
        , MediaNode(backend, AudioSink)
        , m_volumeLevel(1.0)
        , m_device(0) // ### get from backend
        , m_volumeElement(0)
        , m_audioBin(0)
        , m_audioSink(0)
        , m_conv(0)
{
    static int count = 0;
    m_name = "AudioOutput" + QString::number(count++);

    m_audioBin = gst_bin_new (NULL);
    gst_object_ref (GST_OBJECT (m_audioBin));
    gst_object_sink (GST_OBJECT (m_audioBin));

    m_conv = gst_element_factory_make ("audioconvert", NULL);

    // Get category from parent
    Phonon::Category category = Phonon::NoCategory;
    if (Phonon::AudioOutput *audioOutput = qobject_cast<Phonon::AudioOutput *>(parent))
        category = audioOutput->category();

    m_audioSink = m_backend->deviceManager()->createAudioSink(category);
    m_volumeElement = gst_element_factory_make ("volume", NULL);
    GstElement *queue = gst_element_factory_make ("queue", NULL);
    GstElement *audioresample = gst_element_factory_make ("audioresample", NULL);

    if (queue && m_audioBin && m_conv && audioresample && m_audioSink && m_volumeElement) {
        gst_bin_add_many(GST_BIN(m_audioBin), queue, m_conv,
                         audioresample, m_volumeElement, m_audioSink, NULL);

        if (gst_element_link_many(queue, m_conv, audioresample, m_volumeElement,
                                  m_audioSink, NULL)) {
            // Add ghost sink for audiobin
            GstPad *audiopad = gst_element_get_static_pad (queue, "sink");
            gst_element_add_pad (m_audioBin, gst_ghost_pad_new ("sink", audiopad));
            gst_object_unref (audiopad);
            m_isValid = true; // Initialization ok, accept input
        }
    }
}

AudioOutput::~AudioOutput()
{
    if (m_audioBin) {
        gst_element_set_state (m_audioBin, GST_STATE_NULL);
        gst_object_unref (m_audioBin);
    }
}

qreal AudioOutput::volume() const
{
    return m_volumeLevel;
}

int AudioOutput::outputDevice() const
{
    return m_device;
}

void AudioOutput::setVolume(qreal newVolume)
{
    if (newVolume > 2.0 )
        newVolume = 2.0;
    else if (newVolume < 0.0)
        newVolume = 0.0;

    if (newVolume == m_volumeLevel)
        return;

    m_volumeLevel = newVolume;

    if (m_volumeElement) {
        g_object_set(G_OBJECT(m_volumeElement), "volume", newVolume, NULL);
    }

    emit volumeChanged(newVolume);
}

/*
 * Reimp
 */
bool AudioOutput::setOutputDevice(int newDevice)
{
    const AudioOutputDevice device = AudioOutputDevice::fromIndex(newDevice);
    if (!device.isValid()) {
        error() << Q_FUNC_INFO << "Unable to find the output device with index" << newDevice;
        return false;
    }
    return setOutputDevice(device);
}

#if (PHONON_VERSION >= PHONON_VERSION_CHECK(4, 2, 0))
bool AudioOutput::setOutputDevice(const AudioOutputDevice &newDevice)
{
    debug() << Q_FUNC_INFO;
    if (!m_audioSink || !newDevice.isValid()) {
        return false;
    }

    const QVariant dalProperty = newDevice.property("deviceAccessList");
    if (!dalProperty.isValid())
        return false;
    const DeviceAccessList deviceAccessList = dalProperty.value<DeviceAccessList>();
    if (deviceAccessList.isEmpty())
        return false;

    if (newDevice.index() == m_device)
        return true;

    if (root()) {
        root()->saveState();
        if (root()->pipeline()->setState(GST_STATE_READY) == GST_STATE_CHANGE_FAILURE)
            return false;
    }

    // Save previous state
    const GstState oldState = GST_STATE(m_audioSink);
    const QByteArray oldDeviceValue = GstHelper::property(m_audioSink, "device");

    foreach (const DeviceAccess &deviceAccess, deviceAccessList) {
        if (setOutputDevice(deviceAccess.first, deviceAccess.second, oldState)) {
            m_device = newDevice.index();
            return true;
        }
    }

    // Revert state
    GstHelper::setProperty(m_audioSink, "device", oldDeviceValue);
    gst_element_set_state(m_audioSink, oldState);

    if (root()) {
        QMetaObject::invokeMethod(root(), "setState",
                                  Qt::QueuedConnection, Q_ARG(State, StoppedState));
        root()->resumeState();
    }

    return false;
}

bool AudioOutput::setOutputDevice(const QByteArray &driver, const QString &deviceId, const GstState oldState)
{
    const QByteArray sinkName = GstHelper::property(m_audioSink, "name");
    if (sinkName == QByteArray("alsasink")) {
        if (driver != QByteArray("alsa")) {
            return false;
        }
    }

    // We test if the device can be opened by checking if it can go from NULL to READY state
    gst_element_set_state(m_audioSink, GST_STATE_NULL);
    if (GstHelper::setProperty(m_audioSink, "device", deviceId.toUtf8())) {
        debug() << Q_FUNC_INFO << "setProperty( device," << deviceId << ") succeeded";
        if (gst_element_set_state(m_audioSink, oldState) == GST_STATE_CHANGE_SUCCESS) {
            debug() << Q_FUNC_INFO << "go to old state on device" << deviceId << "succeeded";
            if (root()) {
                QMetaObject::invokeMethod(root(), "setState",
                                            Qt::QueuedConnection,
                                            Q_ARG(State, StoppedState));
                root()->resumeState();
            }
            return true;
        } else {
            error() << Q_FUNC_INFO << "go to old state on device" << deviceId << "failed";
        }
    } else {
        error() << Q_FUNC_INFO << "setProperty( device," << deviceId << ") failed";
    }

    return false;
}
#endif

}
} //namespace Phonon::Gstreamer

QT_END_NAMESPACE
#include "moc_audiooutput.cpp"
