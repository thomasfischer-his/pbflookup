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

#ifndef TOKENPROCESSOR_H
#define TOKENPROCESSOR_H

#include "idtree.h"
#include "weightednodeset.h"

namespace SwedishText {
class Tree;
}

class Sweden;

class TokenProcessor
{
public:
    explicit TokenProcessor(SwedishText::Tree *swedishTextTree, IdTree<Coord> *coords, IdTree<WayNodes> *waynodes, IdTree<RelationMem> *relmem, Sweden *sweden);
    ~TokenProcessor();

    void evaluteWordCombinations(const std::vector<std::string> &words, WeightedNodeSet &wns) const;
    void evaluteRoads(const std::vector<std::string> &words, WeightedNodeSet &wns) const;

private:
    class Private;
    Private *const d;
};

#endif // TOKENPROCESSOR_H