/***************************************************************************
 *   Copyright (C) 2015-2016 by Thomas Fischer <thomas.fischer@his.se>     *
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
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef GLOBAL_OBJECTS_H
#define GLOBAL_OBJECTS_H

#include "idtree.h"
#include "swedishtexttree.h"
#include "sweden.h"
#include "timer.h"

extern IdTree<WayNodes> *wayNodes; ///< defined in 'globalobjects.cpp'
extern IdTree<Coord> *node2Coord; ///< defined in 'globalobjects.cpp'
extern IdTree<RelationMem> *relMembers; ///< defined in 'globalobjects.cpp'
extern IdTree<WriteableString> *nodeNames; ///< defined in 'globalobjects.cpp'
extern IdTree<WriteableString> *wayNames; ///< defined in 'globalobjects.cpp'
extern IdTree<WriteableString> *relationNames; ///< defined in 'globalobjects.cpp'
extern SwedishTextTree *swedishTextTree; ///< defined in 'globalobjects.cpp'
extern Sweden *sweden; ///< defined in 'globalobjects.cpp'


class GlobalObjectManager {
public:
    GlobalObjectManager();
    ~GlobalObjectManager();

protected:
    void load();
    void save() const;

    /**
     * Test if a file can be read and is not empty
     * @param filename
     * @return
     */
    static bool testNonEmptyFile(const std::string &filename, unsigned int minimumSize = 16);

private:
    Timer timer;
};

class PidFile {
public:
    PidFile();
    ~PidFile();
};

#endif // GLOBAL_OBJECTS_H
