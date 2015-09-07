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

#include "weightednodeset.h"

#include <cmath>

#include <algorithm>

#include "error.h"
#include "sweden.h"

WeightedNodeSet::WeightedNodeSet(IdTree<Coord> *_n2c, IdTree<WayNodes> *_w2n, IdTree<RelationMem> *_relmem, Sweden *_sweden)
    : n2c(_n2c), w2n(_w2n), relmem(_relmem), sweden(_sweden), m_minlat(1000.0), m_maxlat(-1000.0), m_minlon(1000.0), m_maxlon(1000)
{
    /// nothing
}

bool WeightedNodeSet::appendNode(uint64_t id, int s, size_t wordlen) {
    const double weight = 1.0 * exp(log(s) * 3) * exp(log(wordlen) * 0.5);
    return appendNode(id, weight);
}

bool WeightedNodeSet::appendNode(uint64_t id, double weight) {
    Coord c;
    const bool found = n2c->retrieve(id, c);
    if (found) {
        bool alreadyKnown = false;
        for (int i = size() - 1; !alreadyKnown && i >= 0; --i)
            if (at(i).id == id) {
                at(i).weight *= 1.2;
                alreadyKnown = true;
            }
        if (!alreadyKnown)
            push_back(WeightedNode(id, weight, c.y, c.x));
        return true;
    } else
        return false;
}

bool WeightedNodeSet::appendWay(uint64_t id, int s, size_t wordlen) {
    const double weight = 1.0 * exp(log(s) * 3) * exp(log(wordlen) * 0.5);
    return appendWay(id, weight);
}

bool WeightedNodeSet::appendWay(uint64_t id, double weight) {
    WayNodes wn;
    const bool found = w2n->retrieve(id, wn);
    if (found) {
        const double weightPerNode = weight / wn.num_nodes;
        for (uint32_t i = 0; i < wn.num_nodes; ++i)
            if (!appendNode(wn.nodes[i], weightPerNode)) break;
        return true;
    } else
        return false;
}

bool WeightedNodeSet::appendRelation(uint64_t id, int s, size_t wordlen) {
    const double weight = 1.0 * exp(log(s) * 3) * exp(log(wordlen) * 0.5);
    return appendRelation(id, weight);
}

bool WeightedNodeSet::appendRelation(uint64_t id, double weight) {
    RelationMem rm;
    const bool found = relmem->retrieve(id, rm);
    if (found) {
        const double weightPerMember = weight / rm.num_members;
        for (uint32_t i = 0; i < rm.num_members; ++i) {
            if (appendNode(rm.member_ids[i], weightPerMember)) continue;
            if (appendWay(rm.member_ids[i], weightPerMember)) continue;
            break;
        }
        return true;
    } else
        return false;
}

void WeightedNodeSet::dump() {
    int i = 0;
    for (WeightedNodeSet::const_iterator it = begin(); it != end() && i < 10; ++it, ++i) {
        const WeightedNode &wn = *it;
        if (wn.weight > 0.01) {
            Error::info("Node %5i, id=%8llu, weight=%5.3f, x=%i, y=%i", i, wn.id, wn.weight, wn.x, wn.y);
            Error::debug("  http://www.openstreetmap.org/node/%llu", wn.id);
        }
    }
}

void WeightedNodeSet::normalize() {
    if (begin() == end()) return; ///< empty vector

    /// Sort by weight, heaviest first
    std::sort(begin(), end(), std::greater<WeightedNode>());

    const double max_weight = (*begin()).weight;
    for (iterator it = begin(); it != end(); ++it) {
        (*it).weight /= max_weight;
    }
}

void WeightedNodeSet::setMinMaxLatLon(double minlat, double maxlat, double minlon, double maxlon) {
    m_minlat = minlat;
    m_maxlat = maxlat;
    m_minlon = minlon;
    m_maxlon = maxlon;
}

void WeightedNodeSet::powerCluster(double alpha, double p) {
    if (empty()) return;

    double change[size()];
    for (int i = size() - 1; i >= 0; --i)
        change[i] = 0;

    Error::info("alpha=%.7f  p=%.7f", alpha, p);
    const double delta_ybound = (71.2 - 53.8) * 111412.24; ///< latitude
    const double delta_xbound = (31.2 - 4.4) * 55799.98; ///< longitude
    const double max_dist = sqrt(delta_ybound * delta_ybound + delta_xbound * delta_xbound);
    for (int i = size() - 2; i >= 0; --i) {
        for (unsigned int j = i + 1; j < size(); ++j) {
            const int delta_y = at(i).y - at(j).y;
            const int delta_x = at(i).x - at(j).x;
            const double dist = sqrt(delta_x * delta_x + delta_y * delta_y);
            if (dist >= max_dist) {
                Error::warn("Distance is larger than max_dist");
            }
            const double reldist = (max_dist - dist) / max_dist;
            const double poweredreldist = exp(log(reldist) * alpha) * p;
            change[i] += poweredreldist;
            change[j] += poweredreldist;
        }
    }

    for (int i = size() - 1; i >= 0; --i) {
        WeightedNode &wn = at(i);
        wn.weight += change[i];
    }
}

void WeightedNodeSet::powerMunicipalityCluster(double p) {
    if (empty()) return;

    double change[size()];
    int scbcode[size()];
    for (int i = size() - 1; i >= 0; --i) {
        change[i] = 0;
        scbcode[i] = sweden->insideSCBarea(at(i).id);
    }

    for (int i = size() - 2; i >= 0; --i)
        for (unsigned int j = i + 1; j < size(); ++j)
            if (scbcode[i] == scbcode[j]) {
                change[i] += at(i).weight * p;
                change[j] += at(j).weight * p;
            }


    for (int i = size() - 1; i >= 0; --i) {
        WeightedNode &wn = at(i);
        wn.weight += change[i];
    }
}
