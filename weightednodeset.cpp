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

#include "cmath"

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
            push_back(WeightedNode(id, weight, c.lat, c.lon));
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
            Error::info("Node %5i, id=%8llu, weight=%5.3f, lat=%8.4f, lon=%8.4f", i, wn.id, wn.weight, wn.lat, wn.lon);
            Error::debug("  http://www.openstreetmap.org/node/%llu", wn.id);
        }
    }
}

void WeightedNodeSet::normalize() {
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
    double change[size()];
    for (int i = size(); i >= 0; --i)
        change[i] = 0;

    static const double relationlatlonlen = 0.5;
    Error::info("alpha=%.7f  p=%.7f", alpha, p);
    const double delta_latbound = (m_maxlat - m_minlat) * relationlatlonlen;
    const double delta_lonbound = (m_maxlon - m_minlon);
    const double max_dist = sqrt(delta_latbound * delta_latbound + delta_lonbound * delta_lonbound);
    for (int i = size() - 2; i >= 0; --i) {
        for (unsigned int j = i + 1; j < size(); ++j) {
            const double delta_lat = (at(i).lat - at(j).lat) * relationlatlonlen;
            const double delta_lon = at(i).lon - at(j).lon;
            const double dist = sqrt(delta_lat * delta_lat + delta_lon * delta_lon);
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
    double change[size()];
    int scbcode[size()];
    for (int i = size(); i >= 0; --i) {
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
