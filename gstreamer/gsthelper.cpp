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
#include <gst/gst.h>
#include "gsthelper.h"
#include "mediaobject.h"
#include "backend.h"

#include <QtCore/QList>

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

/**
 * Probes a gstElement for a list of settable string-property values
 *
 * @return a QStringList containing a list of allwed string values for the given
 *           element
 */
QList<QByteArray> GstHelper::extractProperties(GstElement *elem, const QByteArray &value)
{
    Q_ASSERT(elem);
    QList<QByteArray> list;

    if (GST_IS_PROPERTY_PROBE(elem)) {
        GstPropertyProbe *probe = GST_PROPERTY_PROBE(elem);
        const GParamSpec *devspec = 0;
        GValueArray *array = NULL;

        if ((devspec = gst_property_probe_get_property (probe, value))) {
            if ((array = gst_property_probe_probe_and_get_values (probe, devspec))) {
                for (unsigned int device = 0; device < array->n_values; device++) {
                    GValue *deviceId = g_value_array_get_nth (array, device);
                    list.append(g_value_get_string(deviceId));
                }
            }
            if (array)
                g_value_array_free (array);
        }
    }
    return list;
}

/**
 * Sets the string value of a GstElement's property
 *
 * @return false if the value could not be set.
 */
bool GstHelper::setProperty(GstElement *elem, const char *propertyName, const QByteArray &propertyValue)
{
    Q_ASSERT(elem);
    Q_ASSERT(propertyName && strlen(propertyName));

    if (GST_IS_PROPERTY_PROBE(elem) && gst_property_probe_get_property( GST_PROPERTY_PROBE( elem), propertyName ) ) {
        g_object_set(G_OBJECT(elem), propertyName, propertyValue.constData(), NULL);
        return true;
    }
    return false;
}

/**
 * Queries an element for the value of an object property
 */
QByteArray GstHelper::property(GstElement *elem, const char *propertyName)
{
    Q_ASSERT(elem);
    Q_ASSERT(propertyName && strlen(propertyName));
    QByteArray retVal;

    if (GST_IS_PROPERTY_PROBE(elem) && gst_property_probe_get_property( GST_PROPERTY_PROBE(elem), propertyName)) {
        gchar *value = NULL;
        g_object_get (G_OBJECT(elem), propertyName, &value, NULL);
        retVal = QByteArray(value);
        g_free (value);
    }
    return retVal;
}

/**
 * Queries a GstObject for it's name
 */
QByteArray GstHelper::name(GstObject *obj)
{
    Q_ASSERT(obj);
    QByteArray retVal;
    gchar *value = NULL;
    if ((value = gst_object_get_name (obj))) {
        retVal = QByteArray(value);
        g_free (value);
    }
    return retVal;
}

QString GstHelper::stateName(GstState state)
{
    switch(state) {
    case GST_STATE_VOID_PENDING:
        return "void pending";
    case GST_STATE_NULL:
        return "null";
    case GST_STATE_READY:
        return "ready";
    case GST_STATE_PAUSED:
        return "paused";
    case GST_STATE_PLAYING:
        return "playing";
    }
    return "";
}

} //namespace Gstreamer
} //namespace Phonon

QT_END_NAMESPACE
