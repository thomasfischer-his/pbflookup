/***************************************************************************
 *   Copyright (C) 2015 by Thomas Fischer <thomas.fischer@his.se>          *
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

#ifndef TYPES_H
#define TYPES_H

#include <cstdint>

struct OSMElement {
    enum ElementType {UnknownElementType = 0, Node, Way, Relation};
    enum RealWorldType { UnknownRealWorldType = 0, PlaceLargeArea = 100, PlaceLarge = 105, PlaceMedium = 110, PlaceSmall = 115, RoadMajor = 200, RoadMedium = 205, RoadMinor = 210};
    uint64_t id;
    ElementType type;
    RealWorldType realworld_type;

    OSMElement()
        : id(UINT64_MAX), type(UnknownElementType), realworld_type(UnknownRealWorldType)
    {
        /// nothing -> invalid
    }

    OSMElement(uint64_t _id, ElementType _type, RealWorldType _realworld_type) {
        id = _id;
        type = _type;
        realworld_type = _realworld_type;
    }

    operator std::string() const {
        std::string typeString;
        switch (type) {
        case UnknownElementType: typeString = "Unknown "; break;
        case Node: typeString = "Node "; break;
        case Way: typeString = "Way "; break;
        case Relation: typeString = "Relation "; break;
        }

        return typeString + std::to_string(id);
    }
};

#endif // TYPES_H
