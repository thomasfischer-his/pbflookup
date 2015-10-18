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

#include <climits>

#include "idtree.h"

struct WeightedNode {
    explicit WeightedNode() {
        id = 0;
        weight = 0.0;
        x = y = 0;
        usedInRingCluster = false;
    }

    explicit WeightedNode(uint64_t _id, double _weight, int _y, int _x)
        : id(_id), weight(_weight), x(_x), y(_y)
    {
        usedInRingCluster = false;
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
    bool usedInRingCluster;
};

struct RingCluster {
    explicit RingCluster(uint64_t _centerNodeId)
        : centerNodeId(_centerNodeId) {
        ringSize = -1;
        sumWeight = 0.0;
        weightedCenterX = weightedCenterY = 0;
    }

    bool operator< (const RingCluster &other) const
    {
        return sumWeight < other.sumWeight;
    }

    inline bool operator> (const RingCluster &other) const
    {
        return !operator<(other);
    }

    uint64_t centerNodeId;
    std::vector<WeightedNode *> neighbourNodeIds;
    int ringSize;
    double sumWeight;
    int64_t weightedCenterX, weightedCenterY;
};

class WeightedNodeSet : public std::vector<WeightedNode> {
public:
    WeightedNodeSet();
    bool appendNode(uint64_t id, double weight, int overwriteX = INT_MAX, int overwriteY = INT_MAX);
    bool appendWay(uint64_t id, double weight);
    bool appendRelation(uint64_t id, double weight);

    Coord weightedCenter() const;
    void dump() const;
    void dumpGpx() const;
    void normalize();

    void powerCluster(double alpha, double p);
    void powerMunicipalityCluster(double p);

    /**
     * Put nodes into ring-based clusters.
     * Starting from a heavy, central node, add nodes to a grown cluster
     * until the number of nearby nodes drops off.
     * Every node can only be in one cluster, i.e. becomes tabu for any
     * other cluster.
     */
    void buildRingCluster();
    void dumpRingCluster() const;

    std::vector<RingCluster> ringClusters;

private:
    int squareDistanceToRing(int64_t sqDist) const;
};

#endif // WEIGHTED_NODE_SET_H
