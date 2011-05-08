/*  This file is part of the KDE project.

    Copyright (C) 2011 Trever Fischer <tdfischer@fedoraproject.org>

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

#include "pipeline.h"

QT_BEGIN_NAMESPACE
namespace Phonon
{
namespace Gstreamer
{

Pipeline::Pipeline(QObject *parent)
    : QObject(parent)
{
    m_pipeline = GST_PIPELINE(gst_pipeline_new(NULL));
    gst_object_ref(m_pipeline);
    gst_object_sink(m_pipeline);
}

Pipeline::~Pipeline()
{
    gst_element_set_state(GST_ELEMENT(m_pipeline), GST_STATE_NULL);
    gst_object_unref(m_pipeline);
}

GstElement *Pipeline::element() const
{
    return GST_ELEMENT(m_pipeline);
}

GstStateChangeReturn Pipeline::setState(GstState state)
{
    return gst_element_set_state(GST_ELEMENT(m_pipeline), state);
}

}
};

#include "moc_pipeline.cpp"
