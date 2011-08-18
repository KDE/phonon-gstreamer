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

#include "medianode.h"
#include "mediaobject.h"
#include "backend.h"

#include <gst/gstbin.h>
#include <gst/gstutils.h>

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

MediaNode::MediaNode(Backend *backend, NodeDescription description) :
        m_isValid(false),
        m_root(0),
        m_audioTee(0),
        m_videoTee(0),
        m_fakeAudioSink(0),
        m_fakeVideoSink(0),
        m_backend(backend),
        m_description(description),
        m_finalized(false)
{
    if ((description & AudioSink) && (description & VideoSink)) {
        Q_ASSERT(0); // A node cannot accept both audio and video
    }

    if (description & AudioSource) {
        m_audioTee = gst_element_factory_make("tee", NULL);
        gst_object_ref (GST_OBJECT (m_audioTee));
        gst_object_sink (GST_OBJECT (m_audioTee));

        // Fake audio sink to swallow unconnected audio pads
        m_fakeAudioSink = gst_element_factory_make("fakesink", NULL);
        g_object_set (G_OBJECT (m_fakeAudioSink), "sync", TRUE, NULL);
        gst_object_ref (GST_OBJECT (m_fakeAudioSink));
        gst_object_sink (GST_OBJECT (m_fakeAudioSink));
    }

    if (description & VideoSource) {
        m_videoTee = gst_element_factory_make("tee", NULL);
        gst_object_ref (GST_OBJECT (m_videoTee));
        gst_object_sink (GST_OBJECT (m_videoTee));

        // Fake video sink to swallow unconnected video pads
        m_fakeVideoSink = gst_element_factory_make("fakesink", NULL);
        g_object_set (G_OBJECT (m_fakeVideoSink), "sync", TRUE, NULL);
        gst_object_ref (GST_OBJECT (m_fakeVideoSink));
        gst_object_sink (GST_OBJECT (m_fakeVideoSink));
    }
}

MediaNode::~MediaNode()
{
    if (m_videoTee) {
        gst_element_set_state(m_videoTee, GST_STATE_NULL);
        gst_object_unref(m_videoTee);
    }

    if (m_audioTee) {
        gst_element_set_state(m_audioTee, GST_STATE_NULL);
        gst_object_unref(m_audioTee);
    }

    if (m_fakeAudioSink) {
        gst_element_set_state(m_fakeAudioSink, GST_STATE_NULL);
        gst_object_unref(m_fakeAudioSink);
    }

    if (m_fakeVideoSink) {
        gst_element_set_state(m_fakeVideoSink, GST_STATE_NULL);
        gst_object_unref(m_fakeVideoSink);
    }
}


/**
 * Connects children recursively from a mediaobject root
 */
bool MediaNode::buildGraph()
{
    Q_ASSERT(root()); //We cannot build the graph without a root element source

    bool success = link();

    if (success) {
        // connect children recursively
        for (int i=0; i< m_audioSinkList.size(); ++i) {
            if (MediaNode *node = qobject_cast<MediaNode*>(m_audioSinkList[i])) {
                node->setRoot(root());
                if (!node->buildGraph())
                    success = false;
            }
        }

        for (int i=0; i < m_videoSinkList.size(); ++i) {
            if (MediaNode *node = qobject_cast<MediaNode*>(m_videoSinkList[i])) {
                node->setRoot(root());
                if (!node->buildGraph())
                    success = false;
            }
        }
    }

    if (!success) {
        unlink();
    } else if (!m_finalized) {
        finalizeLink();
        m_finalized = true;
    }

    return success;
}

/**
 *  Disconnects children recursively
 */
bool MediaNode::breakGraph()
{
    if (m_finalized) {
        prepareToUnlink();
        m_finalized = false;
    }
    for (int i=0; i<m_audioSinkList.size(); ++i) {
        MediaNode *node = qobject_cast<MediaNode*>(m_audioSinkList[i]);
        if (!node || !node->breakGraph())
            return false;
        node->setRoot(0);
    }

    for (int i=0; i <m_videoSinkList.size(); ++i) {
        MediaNode *node = qobject_cast<MediaNode*>(m_videoSinkList[i]);
        if (!node || !node->breakGraph())
            return false;
        node->setRoot(0);
    }
    unlink();
    return true;
}

bool MediaNode::connectNode(QObject *obj)
{
    MediaNode *sink = qobject_cast<MediaNode*>(obj);

    bool success = false;

    if (sink) {

        if (!sink->isValid()) {
            m_backend->logMessage(QString("Trying to link to an invalid node (%0)").arg(sink->name()), Backend::Warning);
            return false;
        }

        if (sink->root()) {
            m_backend->logMessage("Trying to link a node that is already linked to a different mediasource ", Backend::Warning);
            return false;
        }

        if ((m_description & AudioSource) && (sink->m_description & AudioSink)) {
            m_audioSinkList << obj;
            success = true;
        }

        if ((m_description & VideoSource) && (sink->m_description & VideoSink)) {
            m_videoSinkList << obj;
            success = true;
        }

        // If we have a root source, and we are connected
        // try to link the gstreamer elements
        if (success && root()) {
            root()->buildGraph();
        }
    }
    return success;
}

bool MediaNode::disconnectNode(QObject *obj)
{
    MediaNode *sink = qobject_cast<MediaNode*>(obj);
    if (root()) {
        // Disconnecting elements while playing or paused seems to cause
        // potential deadlock. Hence we force the pipeline into ready state
        // before any nodes are disconnected.
        root()->pipeline()->setState(GST_STATE_READY);

        Q_ASSERT(sink->root()); //sink has to have a root since it is connected

        if (sink->description() & (AudioSink)) {
            GstPad *sinkPad = gst_element_get_static_pad(sink->audioElement(), "sink");
            // Release requested src pad from tee
            GstPad *requestedPad = gst_pad_get_peer(sinkPad);
            if (requestedPad) {
                gst_element_release_request_pad(m_audioTee, requestedPad);
                gst_object_unref(requestedPad);
            }
            if (GST_ELEMENT_PARENT(sink->audioElement()))
                gst_bin_remove(GST_BIN(root()->audioGraph()), sink->audioElement());
            gst_object_unref(sinkPad);
        }

        if (sink->description() & (VideoSink)) {
            GstPad *sinkPad = gst_element_get_static_pad(sink->videoElement(), "sink");
            // Release requested src pad from tee
            GstPad *requestedPad = gst_pad_get_peer(sinkPad);
            if (requestedPad) {
                gst_element_release_request_pad(m_videoTee, requestedPad);
                gst_object_unref(requestedPad);
            }
            if (GST_ELEMENT_PARENT(sink->videoElement()))
                gst_bin_remove(GST_BIN(root()->videoGraph()), sink->videoElement());
            gst_object_unref(sinkPad);
        }

        sink->breakGraph();
        sink->setRoot(0);
    }

    m_videoSinkList.removeAll(obj);
    m_audioSinkList.removeAll(obj);

    if (sink->m_description & AudioSink) {
        // Remove sink from graph
        return true;
    }

    if ((m_description & VideoSource) && (sink->m_description & VideoSink)) {
        // Remove sink from graph
        return true;
    }

    return false;
}

/*
 * Requests a new tee pad and connects a node to it
 */
bool MediaNode::addOutput(MediaNode *output, GstElement *tee)
{
    Q_ASSERT(root());

    bool success = true;

    GstElement *sinkElement = 0;
    if (output->description() & AudioSink)
        sinkElement = output->audioElement();
    else if (output->description() & VideoSink)
        sinkElement = output->videoElement();

    Q_ASSERT(sinkElement);

    if (!sinkElement)
        return false;

    GstState state = root()->pipeline()->state();
    GstPad *srcPad = gst_element_get_request_pad (tee, "src%d");
    GstPad *sinkPad = gst_element_get_static_pad (sinkElement, "sink");

    if (!sinkPad) {
        success = false;
    } else if (gst_pad_is_linked(sinkPad)) {
        gst_object_unref (GST_OBJECT (sinkPad));
        gst_object_unref (GST_OBJECT (srcPad));
        return true;
    }

    if (success) {
        if (output->description() & AudioSink)
            gst_bin_add(GST_BIN(root()->audioGraph()), sinkElement);
        else if (output->description() & VideoSink)
            gst_bin_add(GST_BIN(root()->videoGraph()), sinkElement);
    }

    if (success) {
        gst_pad_link(srcPad, sinkPad);
        gst_element_set_state(sinkElement, state);
    } else {
        gst_element_release_request_pad(tee, srcPad);
    }

    gst_object_unref (GST_OBJECT (srcPad));
    gst_object_unref (GST_OBJECT (sinkPad));

    return success;
}

// Used to seal up unconnected source nodes by connecting unconnected src pads to fake sinks
bool MediaNode::connectToFakeSink(GstElement *tee, GstElement *sink, GstElement *bin)
{
    bool success = true;
    GstPad *sinkPad = gst_element_get_static_pad (sink, "sink");

    if (GST_PAD_IS_LINKED (sinkPad)) {
        //This fakesink is already connected
        gst_object_unref (sinkPad);
        return true;
    }

    GstPad *srcPad = gst_element_get_request_pad (tee, "src%d");
    gst_bin_add(GST_BIN(bin), sink);
    if (success)
        success = (gst_pad_link (srcPad, sinkPad) == GST_PAD_LINK_OK);
    if (success)
        success = (gst_element_set_state(sink, GST_STATE(bin)) != GST_STATE_CHANGE_FAILURE);
    gst_object_unref (srcPad);
    gst_object_unref (sinkPad);
    return success;
}

// Used to seal up unconnected source nodes by connecting unconnected src pads to fake sinks
bool MediaNode::releaseFakeSinkIfConnected(GstElement *tee, GstElement *fakesink, GstElement *bin)
{
    if (GST_ELEMENT_PARENT(fakesink) == GST_ELEMENT(bin)) {
        GstPad *sinkPad = gst_element_get_static_pad(fakesink, "sink");

        // Release requested src pad from tee
        GstPad *requestedPad = gst_pad_get_peer(sinkPad);
        if (requestedPad) {
            gst_element_release_request_pad(tee, requestedPad);
            gst_object_unref(requestedPad);
        }
        gst_object_unref(sinkPad);

        gst_element_set_state(fakesink, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(bin), fakesink);
        Q_ASSERT(!GST_ELEMENT_PARENT(fakesink));
    }
    return true;
}

bool MediaNode::linkMediaNodeList(QList<QObject *> &list, GstElement *bin, GstElement *tee, GstElement *fakesink, GstElement *src)
{
    if (!GST_ELEMENT_PARENT(tee)) {
        gst_bin_add(GST_BIN(bin), tee);
        if (!gst_element_link_pads(src, "src", tee, "sink"))
            return false;
        gst_element_set_state(tee, GST_STATE(bin));
    }
    if (list.isEmpty()) {
        //connect node to a fake sink to avoid clogging the pipeline
        if (!connectToFakeSink(tee, fakesink, bin))
            return false;
    } else {
        // Remove fake sink if previously connected
        if (!releaseFakeSinkIfConnected(tee, fakesink, bin))
            return false;

        for (int i = 0 ; i < list.size() ; ++i) {
            QObject *sink = list[i];
            if (MediaNode *output = qobject_cast<MediaNode*>(sink)) {
                if (!addOutput(output, tee))
                    return false;
            }
        }
    }
    return true;
}

bool MediaNode::link()
{
    // Rewire everything
    if ((description() & AudioSource)) {
        if (!linkMediaNodeList(m_audioSinkList, root()->audioGraph(), m_audioTee, m_fakeAudioSink, audioElement()))
            return false;
    }

    if ((description() & VideoSource)) {
        if (!linkMediaNodeList(m_videoSinkList, root()->videoGraph(), m_videoTee, m_fakeVideoSink, videoElement()))
            return false;
    }
    return true;
}

bool MediaNode::unlink()
{
    Q_ASSERT(root());
    if (description() & AudioSource) {
        if (GST_ELEMENT_PARENT(m_audioTee) == GST_ELEMENT(root()->audioGraph())) {
           gst_element_set_state(m_audioTee, GST_STATE_NULL);
           gst_bin_remove(GST_BIN(root()->audioGraph()), m_audioTee);
       }
        for (int i=0; i<m_audioSinkList.size(); ++i) {
            QObject *audioSink = m_audioSinkList[i];
            if (MediaNode *output = qobject_cast<MediaNode*>(audioSink)) {
                GstElement *element = output->audioElement();
                if (GST_ELEMENT_PARENT(element) == GST_ELEMENT(root()->audioGraph())) {
                    gst_element_set_state(element, GST_STATE_NULL);
                    gst_bin_remove(GST_BIN(root()->audioGraph()), element);
                }
            }
        }
    } else if (description() & VideoSource) {
        if (GST_ELEMENT_PARENT(m_videoTee) == GST_ELEMENT(root()->videoGraph())) {
           gst_element_set_state(m_videoTee, GST_STATE_NULL);
           gst_bin_remove(GST_BIN(root()->videoGraph()), m_videoTee);
        }
        for (int i=0; i <m_videoSinkList.size(); ++i) {
            QObject *videoSink = m_videoSinkList[i];
            if (MediaNode *vw = qobject_cast<MediaNode*>(videoSink)) {
                GstElement *element = vw->videoElement();
                if (GST_ELEMENT_PARENT(element) == GST_ELEMENT(root()->videoGraph())) {
                    gst_element_set_state(element, GST_STATE_NULL);
                    gst_bin_remove(GST_BIN(root()->videoGraph()), element);
                }
            }
        }
    }
    return true;
}

void MediaNode::prepareToUnlink()
{
}

void MediaNode::finalizeLink()
{
}


} // ns Gstreamer
} // ns Phonon

QT_END_NAMESPACE
