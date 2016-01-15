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

struct OSMElement {
    enum ElementType {UnknownElementType = 0, Node, Way, Relation};
    enum RealWorldType { UnknownRealWorldType = 0, PlaceLarge, PlaceMedium, PlaceSmall, RoadMajor, RoadMedium, RoadMinor};
    uint64_t id;
    ElementType type;
    RealWorldType realworld_type;

    OSMElement(uint64_t _id, ElementType _type, RealWorldType _realworld_type) {
        id = _id;
        type = _type;
        realworld_type = _realworld_type;
    }
};

#endif // TYPES_H
