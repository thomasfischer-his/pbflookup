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
    enum NodeType { UnknownNodeType = 0, PlaceLarge, PlaceMedium, PlaceSmall};
    uint64_t id;
    ElementType type;
    NodeType nodeType;

    OSMElement(uint64_t _id, ElementType _type, NodeType _nodeType) {
        id = _id;
        type = _type;
        nodeType = _nodeType;
    }
};

#endif // TYPES_H
