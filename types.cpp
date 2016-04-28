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


