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

#ifndef GLOBAL_OBJECTS_H
#define GLOBAL_OBJECTS_H

#include "idtree.h"
#include "swedishtexttree.h"
#include "sweden.h"

extern IdTree<WayNodes> *wayNodes; ///< defined in 'main.cpp'
extern IdTree<Coord> *node2Coord; ///< defined in 'main.cpp'
extern IdTree<RelationMem> *relMembers; ///< defined in 'main.cpp'
extern IdTree<WriteableString> *nodeNames; ///< defined in 'main.cpp'
extern SwedishTextTree *swedishTextTree; ///< defined in 'main.cpp'
extern Sweden *sweden; ///< defined in 'main.cpp'

#endif // GLOBAL_OBJECTS_H
