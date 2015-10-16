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

#include <algorithm>
#include <iostream>

#include "error.h"
#include "sweden.h"
#include "globalobjects.h"

WeightedNodeSet::WeightedNodeSet()
{
    /// nothing
}

bool WeightedNodeSet::appendNode(uint64_t id, double weight, int overwriteX, int overwriteY) {
    Coord c;
    const bool found = node2Coord->retrieve(id, c);
    if (found) {
        bool alreadyKnown = false;
        for (auto it = begin(); it != end(); ++it) {
            WeightedNode &wn = *it;
            if (wn.id == id) {
                wn.weight *= 1.2; // TODO put into configuration file
                alreadyKnown = true;
                break;
            }
        }
        if (!alreadyKnown)
            push_back(WeightedNode(id, weight, overwriteY == INT_MAX ? c.y : overwriteY, overwriteX == INT_MAX ? c.x : overwriteX));
        return true;
    } else {
        Error::err("Could not retrieve coordinates for node %llu", id);
        return false;
    }
}

bool WeightedNodeSet::appendWay(uint64_t id, double weight) {
    WayNodes wn;
    const bool found = wayNodes->retrieve(id, wn);
    if (found) {
        if (wn.num_nodes > 1 && wn.nodes[0] == wn.nodes[wn.num_nodes - 1]) {
            /// Way is closed (e.g. a shape of a building)
            /// Compute a center for this shape
            int64_t sumX = 0, sumY = 0;
            for (size_t i = 0; i < wn.num_nodes - 1 /** skip last node, identical to first one */; ++i) {
                Coord c;
                const bool foundNode = node2Coord->retrieve(wn.nodes[i], c);
                if (foundNode) {
                    sumX += c.x;
                    sumY += c.y;
                } else {
                    Error::err("Could not retrieve coordinates for node %llu", wn.nodes[i]);
                    return false;
                }
            }
            sumY /= (wn.num_nodes - 1 /** skip last node, identical to first one */);
            sumX /= (wn.num_nodes - 1);
            /// Store shape's center with way's first node id
            if (!appendNode(wn.nodes[0], weight, sumX, sumY)) return false;
#ifdef DEBUG
            Error::debug("     Way %llu is closed, center is at http://www.openstreetmap.org/#map=17/%.4f/%.4f", id, Coord::toLatitude(sumY), Coord::toLongitude(sumX));
#endif
        } else {
            /// Way is open
            const double weightPerNode = weight / wn.num_nodes;
            for (uint32_t i = 0; i < wn.num_nodes; ++i)
                if (!appendNode(wn.nodes[i], weightPerNode)) return false;
        }
        return true;
    } else {
        Error::err("Could not retrieve members for way %llu", id);
        return false;
    }
}

bool WeightedNodeSet::appendRelation(uint64_t id, double weight) {
    RelationMem rm;
    const bool found = relMembers->retrieve(id, rm);
    if (found) {
        bool result = true;
        const double weightPerMember = weight / rm.num_members;
        for (uint32_t i = 0; i < rm.num_members; ++i) {
            switch (rm.members[i].type) {
            case OSMElement::Node:
                result &= appendNode(rm.members[i].id, weightPerMember);
                break;
            case OSMElement::Way:
                if (!(rm.member_flags[i] & RelationFlags::RoleInner)) {
                    /// skip ways that are 'inner' such as a building's inner courtyard
                    result &= appendWay(rm.members[i].id, weightPerMember);
                }
                break;
            case OSMElement::Relation:
                result &= appendRelation(rm.members[i].id, weightPerMember);
                break;
            default:
                Error::debug("Can only append nodes or ways to relations (relation %llu, member %llu of type %d)", id, rm.members[i].id, rm.members[i].type);
            }
        }
        return result;
    } else {
        Error::err("Could not retrieve members for relation %llu", id);
        return false;
    }
}

void WeightedNodeSet::dump() const {
    double lat = 0.0, lon = 0.0, sumweight = 0.0;
    int i = 0;
    for (WeightedNodeSet::const_iterator it = cbegin(); it != cend() && i < 20; ++it, ++i) {
        const WeightedNode &wn = *it;
        if (wn.weight > 0.01) {
            Error::info("Node %5i, id=%8llu, weight=%5.3f, x=%i, y=%i", i, wn.id, wn.weight, wn.x, wn.y);
            Error::debug("  http://www.openstreetmap.org/node/%llu", wn.id);
            Coord nodeCoord;
            if (node2Coord->retrieve(wn.id, nodeCoord) && (nodeCoord.x != wn.x || nodeCoord.y != wn.y))
                /// A different coordinate has been set of the weighted node
                /// than the node id may imply
                Error::debug("   http://www.openstreetmap.org/#map=17/%.5f/%.5f", Coord::toLatitude(wn.y), Coord::toLongitude(wn.x));

            lat += Coord::toLatitude(wn.y) * wn.weight;
            lon += Coord::toLongitude(wn.x) * wn.weight;
            sumweight += wn.weight;
        }
    }
    if (sumweight > 0.0) {
        lat /= sumweight;
        lon /= sumweight;
        Error::info("Center location: lat= %.5f  lon= %.5f", lat, lon);
        Error::debug("  http://www.openstreetmap.org/#map=15/%.5f/%.5f", lat, lon);
    }
}

void WeightedNodeSet::buildRingCluster() {
    static const int maxRingCount = 6;
    /// Used to keep fixed-point rational numbers in int types
    static const int weightMultiplicationFactor = 1000;

    if (!ringClusters.empty()) {
        /// Clear any previous ring settings
        ringClusters.clear();
        for (auto it = begin(); it != end(); ++it) {
            WeightedNode &wn = *it;
            wn.usedInRingCluster = false;
        }
    }

    /// Sort by weight, heaviest first
    std::sort(begin(), end(), std::greater<WeightedNode>());
    /// Go through all nodes, start from the heaviest
    for (auto outerIt = begin(); outerIt != end(); ++outerIt) {
        WeightedNode &centerWn = *outerIt;
        if (centerWn.usedInRingCluster) continue; ///< Node is already used in some ring cluster

        /// Start a new ring cluster
        RingCluster ringCluster(centerWn.id);
        centerWn.usedInRingCluster = true;
        /// Initialize ring cluster's components
        ringCluster.sumWeight = centerWn.weight;
        ringCluster.weightedCenterX = centerWn.x * (centerWn.weight * weightMultiplicationFactor);
        ringCluster.weightedCenterY = centerWn.y * (centerWn.weight * weightMultiplicationFactor);
        ringCluster.neighbourNodeIds.push_back(&centerWn);

        /// Build a data structure of rings, containing all so-far unused nodes
        /// categorized by 'ring distance'
        std::vector<WeightedNode *> nodesInRing[maxRingCount];
        for (auto innerIt = begin(); innerIt != end(); ++innerIt) {
            WeightedNode &wn = *innerIt;
            if (wn.usedInRingCluster) continue; /// node already used, skip

            const int64_t squareDistance = (int64_t)(wn.x - centerWn.x) * (wn.x - centerWn.x) + (int64_t)(wn.y - centerWn.y) * (wn.y - centerWn.y);
            const int ring = squareDistanceToRing(squareDistance);
            /// All rings more far away that the outmost ring are placed in this outmost ring
            nodesInRing[ring < maxRingCount ? ring : maxRingCount - 1].push_back(&wn);
        }

        size_type num_members = 0;
        /// Probe which rings should be part of this cluster
        // TODO it is speculative which criteria should be applied for best results
        for (ringCluster.ringSize = 0; ringCluster.ringSize < maxRingCount - 2; ++ringCluster.ringSize) ///< Skip outermost ring, collects all far-distant nodes
        {
            num_members += nodesInRing[ringCluster.ringSize].size();
            if (ringCluster.ringSize >= 2 && num_members > nodesInRing[ringCluster.ringSize + 1].size() * 2)
                break;
        }

        /// Go through all rings considered to be part of this cluster
        for (int i = 0; i <= ringCluster.ringSize; ++i) {
            /// Go through all nodes in each ring
            for (auto innerIt = nodesInRing[i].begin(); innerIt != nodesInRing[i].end(); ++innerIt) {
                WeightedNode &wn = *(*innerIt);
                wn.usedInRingCluster = true; ///< Mark node as used in a ring cluster
                ringCluster.sumWeight += wn.weight;
                ringCluster.weightedCenterX += wn.x * (wn.weight * weightMultiplicationFactor);
                ringCluster.weightedCenterY += wn.y * (wn.weight * weightMultiplicationFactor);
            }
            /// Record nodes as belonging to this ring cluster
            ringCluster.neighbourNodeIds.insert(ringCluster.neighbourNodeIds.end(), nodesInRing[i].cbegin(), nodesInRing[i].cend());
        }

        /// Calculate cluster's weighted center
        ringCluster.weightedCenterX /= ringCluster.sumWeight * weightMultiplicationFactor;
        ringCluster.weightedCenterY /= ringCluster.sumWeight * weightMultiplicationFactor;

        ringClusters.push_back(ringCluster);
    }

    /// Sort ring clusters by weight, heaviest first
    std::sort(ringClusters.begin(), ringClusters.end(), std::greater<RingCluster>());
}

void WeightedNodeSet::dumpRingCluster() const {
    Error::info("Number of Ring Clusters: %d    Number of nodes= %d", ringClusters.size(), size());
    for (auto it = ringClusters.cbegin(); it != ringClusters.cend(); ++it) {
        const RingCluster &ringCluster = *it;
        Error::debug(" Center node= %llu", ringCluster.centerNodeId);
        Error::debug("  Num nodes= %d", ringCluster.neighbourNodeIds.size());
        Error::debug("  Ring size= %d", ringCluster.ringSize);
        Error::debug("  Weight= %.2f", ringCluster.sumWeight);
        Error::debug("  weightedCenter  lon= %.4f  lat= %.4f", Coord::toLongitude(ringCluster.weightedCenterX), Coord::toLatitude(ringCluster.weightedCenterY));
    }
}

int WeightedNodeSet::squareDistanceToRing(int64_t sqDistInDecimeter) const {
    int remaining = sqDistInDecimeter / 100000000; ///< to square-kilometers
    if (remaining == 0) return 0; ///< less than 1km radius
    int result = 1; ///< at least 1km radius

    while (remaining >= 100) {
        remaining /= 100; ///< steps of 10^2
        result += 3; ///< three rings per power of 10
    }

    if (remaining >= 44) ///< approx (10/3*2)^2
        return result + 2;
    else if (remaining >= 11) ///< approx (10/3)^2
        return result + 1;
    else
        return result;
}

void WeightedNodeSet::dumpGpx() const {
    std::cout << "<?xml version=\"1.0\"?>" << std::endl;
    std::cout << "<gpx creator=\"pbflookup\" version=\"1.1\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:ogr=\"http://osgeo.org/gdal\" xmlns=\"http://www.topografix.com/GPX/1/1\" xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd\">" << std::endl;
    int i = 0;
    for (WeightedNodeSet::const_iterator it = cbegin(); it != cend() && i < 10; ++it, ++i) {
        const WeightedNode &wn = *it;
        std::cout << "<wpt lat=\"" << Coord::toLatitude(wn.y) << "\" lon=\"" <<  Coord::toLongitude(wn.x) << "\">" << std::endl;
        std::cout << "</wpt>" << std::endl;
    }
    std::cout << "</gpx>" << std::endl;
}

void WeightedNodeSet::normalize() {
    if (cbegin() == cend()) return; ///< empty vector

    /// Sort by weight, heaviest first
    std::sort(begin(), end(), std::greater<WeightedNode>());

    const double max_weight = (*begin()).weight;
    for (iterator it = begin(); it != end(); ++it) {
        (*it).weight /= max_weight;
    }
}

void WeightedNodeSet::powerCluster(double alpha, double p) {
    if (empty()) return;

    double change[size()];
    for (int i = size() - 1; i >= 0; --i)
        change[i] = 0;

    Error::info("alpha=%.7f  p=%.7f", alpha, p);
    static const int delta_ybound = (71.2 - 53.8) * decimeterDegreeLatitude + 0.5; ///< latitude
    static const int delta_xbound = (31.2 - 4.4) * decimeterDegreeLongitude + 0.5; ///< longitude
    static const int64_t max_distsq = (int64_t)delta_ybound * delta_ybound + (int64_t)delta_xbound * delta_xbound;
    static const double max_dist = sqrt(max_distsq);
    for (int i = size() - 2; i >= 0; --i) {
        for (unsigned int j = i + 1; j < size(); ++j) {
            const int delta_y = at(i).y - at(j).y;
            const int delta_x = at(i).x - at(j).x;
            const int64_t distsq = (int64_t)delta_x * delta_x + (int64_t)delta_y * delta_y;
            if (distsq >= max_distsq) {
                Error::warn("Distance is larger than max_dist");
            }
            const double dist = sqrt(distsq);
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
    std::vector<int> scbcode[size()];
    for (int i = size() - 1; i >= 0; --i) {
        change[i] = 0;
        scbcode[i] = sweden->insideSCBarea(at(i).id);
        if (scbcode[i].empty()) scbcode[i].push_back(-i - 1);
    }

    for (int i = size() - 2; i >= 0; --i)
        for (unsigned int j = i + 1; j < size(); ++j) {
            if (scbcode[i].front() == scbcode[j].front()) {
                change[i] += at(i).weight * p;
                change[j] += at(j).weight * p;
            }
        }

    for (int i = size() - 1; i >= 0; --i) {
        WeightedNode &wn = at(i);
        wn.weight += change[i];
    }
}
