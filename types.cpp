/***************************************************************************
 *   Copyright (C) 2016 by Thomas Fischer <thomas.fischer@his.se>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; version 3 of the License.               *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#include "types.h"

#include "idtree.h"
#include "globalobjects.h"

std::string OSMElement::name() const {
    WriteableString result;
    switch (type) {
    case Node:
        if (nodeNames->retrieve(id, result)) return result;
        break;
    case Way:
        if (wayNames->retrieve(id, result)) return result;
        break;
    case Relation:
        if (relationNames->retrieve(id, result)) return result;
        break;
    case UnknownElementType:
        Error::warn("Cannot retrieve name for an unknown element type (id=%llu)", id);
        break;
    }

    return result;
}

OSMElement::operator std::string() const {
    std::string typeString("UNSET-TYPE");
    switch (type) {
    case UnknownElementType: typeString = "Unknown "; break;
    case Node: typeString = "Node "; break;
    case Way: typeString = "Way "; break;
    case Relation: typeString = "Relation "; break;
    }
    std::string realWorldTypeString(" UNSET-RWT");
    switch (realworld_type) {
    case PlaceLargeArea: realWorldTypeString = " PlaceLargeArea"; break;
    case PlaceLarge: realWorldTypeString = " PlaceLarge"; break;
    case PlaceMedium: realWorldTypeString = " PlaceMedium"; break;
    case PlaceSmall: realWorldTypeString = " PlaceSmall"; break;
    case RoadMajor: realWorldTypeString = " RoadMajor"; break;
    case RoadMedium: realWorldTypeString = " RoadMedium"; break;
    case RoadMinor: realWorldTypeString = " RoadMinor"; break;
    case Building: realWorldTypeString = " Building"; break;
    case Island: realWorldTypeString = " Island"; break;
    case Water: realWorldTypeString = " Water"; break;
    case UnknownRealWorldType: realWorldTypeString = " Unknown-Type"; break;
    }

    return typeString + std::to_string(id) + "of type" + realWorldTypeString;
}

bool OSMElement::isValid() const {
    return id > 0 && id < UINT64_MAX && type != UnknownElementType;
}

/// Comparison operator, necessary e.g. for a std::find on a std::vector of OSMElement
bool operator==(const OSMElement &a, const OSMElement &b) {
    /// Do not compare for realworld_type, as it is not decisive and based on a heuristic only
    return a.id == b.id && a.type == b.type;
}
