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
#include "debug.h"
#include "phonon-config-gstreamer.h"

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/gstutils.h>

namespace Phonon
{
namespace Gstreamer
{

MediaNode::MediaNode(Backend *backend, NodeDescription description) :
        m_isValid(false),
        m_root(0),
        m_audioTee(0),
        m_videoTee(0),
        m_backend(backend),
        m_description(description),
        m_finalized(false)
{
    if ((description & AudioSink) && (description & VideoSink)) {
        Q_ASSERT(0); // A node cannot accept both audio and video
    }

    if (description & AudioSource) {
        m_audioTee = gst_element_factory_make("tee", NULL);
        Q_ASSERT(m_audioTee); // Must not ever be null.
        gst_object_ref_sink(GST_OBJECT(m_audioTee));
    }

    if (description & VideoSource) {
        m_videoTee = gst_element_factory_make("tee", NULL);
        Q_ASSERT(m_videoTee); // Must not ever be null.
        gst_object_ref_sink(GST_OBJECT(m_videoTee));
    }
}

MediaNode::~MediaNode()
{
    if (m_videoTee) {
        gst_element_set_state(m_videoTee, GST_STATE_NULL);
        gst_object_unref(m_videoTee);
        m_videoTee = 0;
    }

    if (m_audioTee) {
        gst_element_set_state(m_audioTee, GST_STATE_NULL);
        gst_object_unref(m_audioTee);
        m_audioTee = 0;
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
        for (int i = 0; i < m_audioSinkList.size(); ++i) {
            if (MediaNode *node = qobject_cast<MediaNode*>(m_audioSinkList[i])) {
                node->setRoot(root());
                if (!node->buildGraph()) {
                    success = false;
                }
            }
        }

        for (int i = 0; i < m_videoSinkList.size(); ++i) {
            if (MediaNode *node = qobject_cast<MediaNode*>(m_videoSinkList[i])) {
                node->setRoot(root());
                if (!node->buildGraph()) {
                    success = false;
                }
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
    for (int i = 0; i < m_audioSinkList.size(); ++i) {
        MediaNode *node = qobject_cast<MediaNode*>(m_audioSinkList[i]);
        if (!node || !node->breakGraph()) {
            return false;
        }
        node->setRoot(0);
    }

    for (int i = 0; i < m_videoSinkList.size(); ++i) {
        MediaNode *node = qobject_cast<MediaNode*>(m_videoSinkList[i]);
        if (!node || !node->breakGraph()) {
            return false;
        }
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
            warning() << "Trying to link to an invalid node" << sink->name();
            return false;
        }

        if (sink->root()) {
            warning() << "Trying to link a node that is already linked to a different mediasource";
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
            if (GST_ELEMENT_PARENT(sink->audioElement())) {
                gst_bin_remove(GST_BIN(root()->audioGraph()), sink->audioElement());
            }
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
            if (GST_ELEMENT_PARENT(sink->videoElement())) {
                gst_bin_remove(GST_BIN(root()->videoGraph()), sink->videoElement());
            }
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
    if (output->description() & AudioSink) {
        sinkElement = output->audioElement();
    } else if (output->description() & VideoSink) {
        sinkElement = output->videoElement();
    }

    Q_ASSERT(sinkElement);

    if (!sinkElement) {
        return false;
    }

    GstState state = root()->pipeline()->state();
    GstPadTemplate* tee_src_pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS (tee), "src_%u");
    GstPad *srcPad = gst_element_request_pad(tee, tee_src_pad_template, NULL, NULL);
    GstPad *sinkPad = gst_element_get_static_pad(sinkElement, "sink");

    if (!sinkPad) {
        success = false;
    } else if (gst_pad_is_linked(sinkPad)) {
        gst_object_unref(GST_OBJECT(sinkPad));
        gst_object_unref(GST_OBJECT(srcPad));
        return true;
    }

    if (success) {
        if (output->description() & AudioSink) {
            gst_bin_add(GST_BIN(root()->audioGraph()), sinkElement);
        } else if (output->description() & VideoSink) {
            gst_bin_add(GST_BIN(root()->videoGraph()), sinkElement);
        }
    }

    if (success) {
        gst_pad_link(srcPad, sinkPad);
        gst_element_set_state(sinkElement, state);
    } else {
        gst_element_release_request_pad(tee, srcPad);
    }

    gst_object_unref(GST_OBJECT(srcPad));
    gst_object_unref(GST_OBJECT(sinkPad));

    return success;
}

bool MediaNode::linkMediaNodeList(QList<QObject *> &list, GstElement *bin, GstElement *tee, GstElement *src)
{
    if (!GST_ELEMENT_PARENT(tee)) {
        gst_bin_add(GST_BIN(bin), tee);
        if (!gst_element_link_pads(src, "src", tee, "sink")) {
            return false;
        }
        gst_element_set_state(tee, GST_STATE(bin));
    }
    for (int i = 0 ; i < list.size() ; ++i) {
        QObject *sink = list[i];
        if (MediaNode *output = qobject_cast<MediaNode*>(sink)) {
            if (!addOutput(output, tee)) {
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
        Q_ASSERT(m_audioTee);
        if (!linkMediaNodeList(m_audioSinkList, root()->audioGraph(), m_audioTee, audioElement())) {
            return false;
        }
    }

    if ((description() & VideoSource)) {
        Q_ASSERT(m_videoTee);
        if (!linkMediaNodeList(m_videoSinkList, root()->videoGraph(), m_videoTee, videoElement())) {
            return false;
        }
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
        for (int i = 0; i < m_audioSinkList.size(); ++i) {
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
        for (int i = 0; i < m_videoSinkList.size(); ++i) {
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
