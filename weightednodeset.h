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

private:
    IdTree<Coord> *m_n2c;
    IdTree<WayNodes> *m_w2n;
    IdTree<RelationMem> *m_relmem;
};

#endif // WEIGHTED_NODE_SET_H
