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

#ifndef WEIGHTED_NODE_SET_H
#define WEIGHTED_NODE_SET_H

#include "idtree.h"

class Sweden;

struct WeightedNode {
    explicit WeightedNode() {
        id = 0;
        weight = 0.0;
        x = y = 0;
    }

    explicit WeightedNode(uint64_t _id, double _weight, int _y, int _x)
        : id(_id), weight(_weight), x(_x), y(_y)
    {
        /// nothing
    }

    bool operator< (const WeightedNode &other) const
    {
        return weight < other.weight;
    }

    bool operator> (const WeightedNode &other) const
    {
        return weight > other.weight;
    }

    uint64_t id;
    double weight;
    int x, y;
};

class WeightedNodeSet : public std::vector<WeightedNode> {
public:
    WeightedNodeSet(IdTree<Coord> *n2c, IdTree<WayNodes> *w2n, IdTree<RelationMem> *relmem, Sweden *sweden);
    bool appendNode(uint64_t id, int s, size_t wordlen);
    bool appendNode(uint64_t id, double weight = 1.0);
    bool appendWay(uint64_t id, int s, size_t wordlen);
    bool appendWay(uint64_t id, double weight = 1.0);
    bool appendRelation(uint64_t id, int s, size_t wordlen);
    bool appendRelation(uint64_t id, double weight = 1.0);

    void dump();
    void normalize();

    void powerCluster(double alpha, double p);
    void powerMunicipalityCluster(double p);

private:
    IdTree<Coord> *n2c;
    IdTree<WayNodes> *w2n;
    IdTree<RelationMem> *relmem;
    Sweden *sweden;
};

#endif // WEIGHTED_NODE_SET_H
