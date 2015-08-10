#include "weightednodeset.h"

#include "cmath"

#include "error.h"

WeightedNodeSet::WeightedNodeSet(IdTree<Coord> *n2c, IdTree<WayNodes> *w2n, IdTree<RelationMem> *relmem)
    : m_n2c(n2c), m_w2n(w2n), m_relmem(relmem)
{
    /// nothing
}

bool WeightedNodeSet::appendNode(uint64_t id, int s) {
    const double weight = 1.0 * exp(log(s) * 1.2);
    return appendNode(id, weight);
}

bool WeightedNodeSet::appendNode(uint64_t id, double weight) {
    Coord c;
    const bool found = m_n2c->retrieve(id, c);
    if (found) {
        bool alreadyKnown = false;
        for (int i = size() - 1; !alreadyKnown && i >= 0; --i)
            if (at(i).id == id) {
                at(i).weight *= 1.2;
                alreadyKnown = true;
            }
        if (!alreadyKnown)
            push_back(WeightedNode(id, weight, c.lat, c.lon));
        return true;
    } else
        return false;
}

bool WeightedNodeSet::appendWay(uint64_t id, int s) {
    const double weight = 1.0 * exp(log(s) * 1.2);
    return appendWay(id, weight);
}

bool WeightedNodeSet::appendWay(uint64_t id, double weight) {
    WayNodes wn;
    const bool found = m_w2n->retrieve(id, wn);
    if (found) {
        const double weightPerNode = weight / wn.num_nodes;
        for (uint32_t i = 0; i < wn.num_nodes; ++i)
            if (!appendNode(wn.nodes[i], weightPerNode)) break;
        return true;
    } else
        return false;
}

bool WeightedNodeSet::appendRelation(uint64_t id, int s) {
    const double weight = 1.0 * exp(log(s) * 1.2);
    return appendRelation(id, weight);
}

bool WeightedNodeSet::appendRelation(uint64_t id, double weight) {
    RelationMem rm;
    const bool found = m_relmem->retrieve(id, rm);
    if (found) {
        const double weightPerMember = weight / rm.num_members;
        for (uint32_t i = 0; i < rm.num_members; ++i) {
            if (appendNode(rm.members[i], weightPerMember)) continue;
            if (appendWay(rm.members[i], weightPerMember)) continue;
            break;
        }
        return true;
    } else
        return false;
}

void WeightedNodeSet::dump() {
    int i = 0;
    for (WeightedNodeSet::const_iterator it = begin(); it != end() && i < 30; ++it, ++i) {
        const WeightedNode &wn = *it;
        if (wn.weight > 0.01)
            Error::info("Node %5i, id=%8llu, weight=%5.3f, lat=%8.4f, lon=%8.4f", i, wn.id, wn.weight, wn.lat, wn.lon);
    }
}
