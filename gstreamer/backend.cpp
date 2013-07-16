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

#include "backend.h"
#include "audiooutput.h"
#include "audiodataoutput.h"
#ifdef PHONON_EXPERIMENTAL
#include "videodataoutput.h"
#endif
#include "audioeffect.h"
#include "debug.h"
#include "mediaobject.h"
#ifndef PHONON_NO_GRAPHICSVIEW
#include "videographicsobject.h"
#endif
#include "videowidget.h"
#include "devicemanager.h"
#include "effectmanager.h"
#include "volumefadereffect.h"
#include <gst/interfaces/propertyprobe.h>
#include <phonon/pulsesupport.h>
#include <phonon/GlobalDescriptionContainer>

#include <QtCore/QCoreApplication>
#include <QtCore/QSet>
#include <QtCore/QVariant>
#include <QtCore/QtPlugin>

#include <cstring>

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(phonon_gstreamer, Phonon::Gstreamer::Backend)
#endif

namespace Phonon
{
namespace Gstreamer
{

class MediaNode;

Backend::Backend(QObject *parent, const QVariantList &)
        : QObject(parent)
        , m_deviceManager(0)
        , m_effectManager(0)
        , m_isValid(false)
{
    // Initialise PulseAudio support
    PulseSupport *pulse = PulseSupport::getInstance();
    pulse->enable();
    connect(pulse, SIGNAL(objectDescriptionChanged(ObjectDescriptionType)), SIGNAL(objectDescriptionChanged(ObjectDescriptionType)));

    // In order to support reloading, we only set the app name once...
    static bool first = true;
    if (first) {
        first = false;
        g_set_application_name(qApp->applicationName().toUtf8());
    }

    QByteArray appFilePath = qApp->applicationFilePath().toUtf8();
    QByteArray gstDebugLevel("--gst-debug-level=");
    gstDebugLevel.append(qgetenv("PHONON_SUBSYSTEM_DEBUG"));

    const char *args[] = {
        appFilePath.constData(),
        gstDebugLevel.constData(),
        "--gst-debug-no-color"
    };

    int argc = sizeof(args) / sizeof(*args);
    char **argv = const_cast<char**>(args);
    GError *err = 0;
    bool wasInit = gst_init_check(&argc, &argv, &err); //init gstreamer: must be called before any gst-related functions

    if (err)
        g_error_free(err);

#ifndef QT_NO_PROPERTIES
    setProperty("identifier",     QLatin1String("phonon_gstreamer"));
    setProperty("backendName",    QLatin1String("Gstreamer"));
    setProperty("backendComment", QLatin1String("Gstreamer plugin for Phonon"));
    setProperty("backendVersion", QLatin1String(PHONON_GST_VERSION));
    setProperty("backendWebsite", QLatin1String("http://phonon.kde.org/"));
#endif //QT_NO_PROPERTIES

    // Check if we should enable debug output
    int debugLevel = qgetenv("PHONON_BACKEND_DEBUG").toInt();
    if (debugLevel > 3) // 3 is maximum
        debugLevel = 3;
    Debug::setMinimumDebugLevel((Debug::DebugLevel)((int) Debug::DEBUG_NONE - 1 - debugLevel));

    if (wasInit) {
        m_isValid = checkDependencies();
        gchar *versionString = gst_version_string();
        debug() << "Using" << versionString;
        g_free(versionString);
    }

    if (!isValid()) {
        qWarning("Phonon::GStreamer::Backend: Failed to initialize GStreamer");
    } else {
        m_deviceManager = new DeviceManager(this);
        m_effectManager = new EffectManager(this);
    }
}

Backend::~Backend()
{
    if (GlobalSubtitles::self)
        delete GlobalSubtitles::self;
    if (GlobalAudioChannels::self)
        delete GlobalAudioChannels::self;
    delete m_deviceManager;
    PulseSupport::shutdown();
    gst_deinit();
}

/***
 * !reimp
 */
QObject *Backend::createObject(BackendInterface::Class c, QObject *parent, const QList<QVariant> &args)
{
    // Return nothing if dependencies are not met
    if (!isValid()) {
        warning() << "Backend class" << c << "is not going to be created because GStreamer init failed.";
        return 0;
    }

    switch (c) {
    case MediaObjectClass:
        return new MediaObject(parent);

    case AudioOutputClass:
        return new AudioOutput(parent);

#ifndef QT_NO_PHONON_EFFECT
    case EffectClass:
        if (args[0].toInt() < m_effectManager->audioEffects().size()) {
          return new AudioEffect(m_effectManager->audioEffects()[args[0].toInt()], parent);
        } else {
          qWarning() << Q_FUNC_INFO << ": Effect ID (" << args[0].toInt() << ") out of range (" << m_effectManager->audioEffects().size() << ")!";
          return new AudioEffect(NULL, parent);
        }
#endif //QT_NO_PHONON_EFFECT
    case AudioDataOutputClass:
        return new AudioDataOutput(parent);

#ifndef QT_NO_PHONON_VIDEO
#ifdef PHONON_EXPERIMENTAL
    case VideoDataOutputClass:
        return new VideoDataOutput(parent);
        break;
#endif

    case VideoWidgetClass: {
            QWidget *widget =  qobject_cast<QWidget*>(parent);
            return new VideoWidget(widget);
        }
#ifndef PHONON_NO_GRAPHICSVIEW
    case VideoGraphicsObjectClass:
        return new VideoGraphicsObject(parent);
#endif // PHONON_NO_GRAPHICSVIEW
#endif // QT_NO_PHONON_VIDEO
#ifndef QT_NO_PHONON_VOLUMEFADEREFFECT
    case VolumeFaderEffectClass:
        return new VolumeFaderEffect(parent);
#endif // QT_NO_PHONON_VOLUMEFADEREFFECT

    case VisualizationClass:  //Fall through
    default:
        warning() << "Backend class" << c << "is not supported by Phonon GST :(";
    }
    return 0;
}

// Returns true if all dependencies are met
// and gstreamer is usable, otherwise false
bool Backend::isValid() const
{
    return m_isValid;
}

bool Backend::supportsVideo() const
{
    return isValid();
}

bool Backend::checkDependencies(bool retry) const
{
    bool success = false;
    // Verify that gst-plugins-base is installed
    GstElementFactory *acFactory = gst_element_factory_find ("audioconvert");
    if (acFactory) {
        gst_object_unref(acFactory);
        success = true;
        // Check if gst-plugins-good is installed
        GstElementFactory *csFactory = gst_element_factory_find ("videobalance");
        if (csFactory) {
            gst_object_unref(csFactory);
        } else {
            if (!retry) {
                gst_update_registry();
                checkDependencies(true);
            }
            warning() << tr("Warning: You do not seem to have the package gstreamer0.10-plugins-good installed.\n"
                            "          Some video features have been disabled.");
        }
    } else {
        if (!retry) {
            gst_update_registry();
            checkDependencies(true);
        }
        warning() << tr("Warning: You do not seem to have the base GStreamer plugins installed.\n"
                         "          All audio and video support has been disabled");
    }
    return success;
}

/***
 * !reimp
 */
QStringList Backend::availableMimeTypes() const
{
    QStringList availableMimeTypes;

    if (!isValid())
        return availableMimeTypes;

    GstElementFactory *mpegFactory;
    // Add mp3 as a separate mime type as people are likely to look for it.
    if ((mpegFactory = gst_element_factory_find ("ffmpeg")) ||
        (mpegFactory = gst_element_factory_find ("mad")) ||
        (mpegFactory = gst_element_factory_find ("flump3dec"))) {
        availableMimeTypes << QLatin1String("audio/x-mp3");
        availableMimeTypes << QLatin1String("audio/x-ape");// ape is available from ffmpeg
        gst_object_unref(GST_OBJECT(mpegFactory));
    }

    // Iterate over all audio and video decoders and extract mime types from sink caps
    GList* factoryList = gst_registry_get_feature_list(gst_registry_get_default (), GST_TYPE_ELEMENT_FACTORY);
    for (GList* iter = g_list_first(factoryList) ; iter != NULL ; iter = g_list_next(iter)) {
        GstPluginFeature *feature = GST_PLUGIN_FEATURE(iter->data);
        QString klass = gst_element_factory_get_klass(GST_ELEMENT_FACTORY(feature));

        if (klass == QLatin1String("Codec/Decoder") ||
            klass == QLatin1String("Codec/Decoder/Audio") ||
            klass == QLatin1String("Codec/Decoder/Video") ||
            klass == QLatin1String("Codec/Demuxer") ||
            klass == QLatin1String("Codec/Demuxer/Audio") ||
            klass == QLatin1String("Codec/Demuxer/Video") ||
            klass == QLatin1String("Codec/Parser") ||
            klass == QLatin1String("Codec/Parser/Audio") ||
            klass == QLatin1String("Codec/Parser/Video")) {

            const GList *static_templates;
            GstElementFactory *factory = GST_ELEMENT_FACTORY(feature);
            static_templates = gst_element_factory_get_static_pad_templates(factory);

            for (; static_templates != NULL ; static_templates = static_templates->next) {
                GstStaticPadTemplate *padTemplate = (GstStaticPadTemplate *) static_templates->data;
                if (padTemplate && padTemplate->direction == GST_PAD_SINK) {
                    GstCaps *caps = gst_static_pad_template_get_caps (padTemplate);

                    if (caps) {
                        for (unsigned int struct_idx = 0; struct_idx < gst_caps_get_size (caps); struct_idx++) {

                            const GstStructure* capsStruct = gst_caps_get_structure (caps, struct_idx);
                            QString mime = QString::fromUtf8(gst_structure_get_name (capsStruct));
                            if (!availableMimeTypes.contains(mime))
                                availableMimeTypes.append(mime);
                        }
                    }
                }
            }
        }
    }
    g_list_free(factoryList);
    if (availableMimeTypes.contains("audio/x-vorbis")
        && availableMimeTypes.contains("application/x-ogm-audio")) {
        if (!availableMimeTypes.contains("audio/x-vorbis+ogg"))
            availableMimeTypes.append("audio/x-vorbis+ogg");
        if (!availableMimeTypes.contains("application/ogg"))  /* *.ogg */
            availableMimeTypes.append("application/ogg");
        if (!availableMimeTypes.contains("audio/ogg")) /* *.oga */
            availableMimeTypes.append("audio/ogg");
    }
    availableMimeTypes.sort();
    return availableMimeTypes;
}

/***
 * !reimp
 */
QList<int> Backend::objectDescriptionIndexes(ObjectDescriptionType type) const
{
    QList<int> list;

    if (!isValid())
        return list;

    switch (type) {
    case Phonon::AudioOutputDeviceType:
    case Phonon::AudioCaptureDeviceType:
    case Phonon::VideoCaptureDeviceType:
        list = deviceManager()->deviceIds(type);
        break;
    case Phonon::EffectType: {
            QList<EffectInfo*> effectList = effectManager()->audioEffects();
            for (int eff = 0 ; eff < effectList.size() ; ++eff)
                list.append(eff);
            break;
        }
        break;
    case Phonon::SubtitleType:
        list << GlobalSubtitles::instance()->globalIndexes();
        break;
    case Phonon::AudioChannelType:
        list << GlobalAudioChannels::instance()->globalIndexes();
        break;
    default:
        break;
    }
    return list;
}

/***
 * !reimp
 */
QHash<QByteArray, QVariant> Backend::objectDescriptionProperties(ObjectDescriptionType type, int index) const
{
    QHash<QByteArray, QVariant> ret;

    if (!isValid())
        return ret;

    switch (type) {
    case Phonon::AudioOutputDeviceType:
    case Phonon::AudioCaptureDeviceType:
    case Phonon::VideoCaptureDeviceType:
        // Index should be unique, even for different categories
        ret = deviceManager()->deviceProperties(index);
        break;

    case Phonon::EffectType: {
            QList<EffectInfo*> effectList = effectManager()->audioEffects();
            if (index >= 0 && index <= effectList.size()) {
                const EffectInfo *effect = effectList[index];
                ret.insert("name", effect->name());
                ret.insert("description", effect->description());
                ret.insert("author", effect->author());
            } else
                Q_ASSERT(1); // Since we use list position as ID, this should not happen
        }
        break;

    case Phonon::SubtitleType: {
            const SubtitleDescription description = GlobalSubtitles::instance()->fromIndex(index);
            ret.insert("name", description.name());
            ret.insert("description", description.description());
            ret.insert("type", description.property("type"));
        }
        break;

    case Phonon::AudioChannelType: {
            const AudioChannelDescription description = GlobalAudioChannels::instance()->fromIndex(index);
            ret.insert("name", description.name());
            ret.insert("description", description.description());
            ret.insert("type", description.property("type"));
        }
        break;

    default:
        break;
    }
    return ret;
}

/***
 * !reimp
 */
bool Backend::startConnectionChange(QSet<QObject *> objects)
{
    foreach (QObject *object, objects) {
        MediaNode *sourceNode = qobject_cast<MediaNode *>(object);
        MediaObject *media = sourceNode->root();
        if (media) {
            media->saveState();
        }
    }
    return true;
}

/***
 * !reimp
 */
bool Backend::connectNodes(QObject *source, QObject *sink)
{
    if (isValid()) {
        MediaNode *sourceNode = qobject_cast<MediaNode *>(source);
        MediaNode *sinkNode = qobject_cast<MediaNode *>(sink);
        if (sourceNode && sinkNode) {
            if (sourceNode->connectNode(sink)) {
                debug() << "Backend connected" << source->metaObject()->className() << "to" << sink->metaObject()->className();
                return true;
            }
        }
    }
    warning() << "Linking" << source->metaObject()->className() << "to" << sink->metaObject()->className() << "failed";
    return false;
}

/***
 * !reimp
 */
bool Backend::disconnectNodes(QObject *source, QObject *sink)
{
    MediaNode *sourceNode = qobject_cast<MediaNode *>(source);
    MediaNode *sinkNode = qobject_cast<MediaNode *>(sink);

    if (sourceNode && sinkNode)
        return sourceNode->disconnectNode(sink);
    else
        return false;
}

/***
 * !reimp
 */
bool Backend::endConnectionChange(QSet<QObject *> objects)
{
    foreach (QObject *object, objects) {
        MediaNode *sourceNode = qobject_cast<MediaNode *>(object);
        MediaObject *media = sourceNode->root();
        if (media) {
            media->resumeState();
        }
    }
    return true;
}

DeviceManager* Backend::deviceManager() const
{
    return m_deviceManager;
}

EffectManager *Backend::effectManager() const
{
    return m_effectManager;
}

}
}

#include "moc_backend.cpp"
