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

struct WeightedNode {
    explicit WeightedNode() {
        id = 0;
        weight = lat = lon = 0.0;
    }

    explicit WeightedNode(uint64_t _id, double _weight, double _lat, double _lon)
        : id(_id), weight(_weight), lat(_lat), lon(_lon)
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
    double lat, lon;
};

class WeightedNodeSet : public std::vector<WeightedNode> {
public:
    WeightedNodeSet(IdTree<Coord> *n2c, IdTree<WayNodes> *w2n, IdTree<RelationMem> *relmem);
    bool appendNode(uint64_t id, int s);
    bool appendNode(uint64_t id, double weight = 1.0);
    bool appendWay(uint64_t id, int s);
    bool appendWay(uint64_t id, double weight = 1.0);
    bool appendRelation(uint64_t id, int s);
    bool appendRelation(uint64_t id, double weight = 1.0);

    void dump();

    void setMinMaxLatLon(double minlat, double maxlat, double minlon, double maxlon);

    void powerCluster(double alpha, double p);

private:
    IdTree<Coord> *m_n2c;
    IdTree<WayNodes> *m_w2n;
    IdTree<RelationMem> *m_relmem;

    double m_minlat;
    double m_maxlat;
    double m_minlon;
    double m_maxlon;
};

#endif // WEIGHTED_NODE_SET_H
