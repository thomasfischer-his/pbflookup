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

#include "tokenprocessor.h"

#include <cmath>

#include <unordered_set>
#include <algorithm>

#include "sweden.h"
#include "swedishtexttree.h"
#include "globalobjects.h"

#define min(a,b) ((b)>(a)?(a):(b))

class TokenProcessor::Private
{
private:

    static constexpr double initialWeightPlaceLarge = 1000.0;
    static constexpr double initialWeightPlaceMedium = 100.0;
    static constexpr double initialWeightPlaceSmall = 10.0;
    static constexpr double initialWeightRoadMajor = 1000.0;
    static constexpr double initialWeightRoadMedium = 100.0;
    static constexpr double initialWeightRoadMinor = 10.0;
    static constexpr double initialWeightDefault = 1.0;

public:
    explicit Private()
    {
        /// nothing
    }

    static inline double initialWeight(OSMElement::RealWorldType node_type) {
        double weight = 0.0;

        switch (node_type) {
        case OSMElement::PlaceLarge: weight = initialWeightPlaceLarge; break;
        case OSMElement::PlaceMedium: weight = initialWeightPlaceMedium; break;
        case OSMElement::PlaceSmall: weight = initialWeightPlaceSmall; break;
        case OSMElement::RoadMajor: weight = initialWeightRoadMajor; break;
        case OSMElement::RoadMedium: weight = initialWeightRoadMedium; break;
        case OSMElement::RoadMinor: weight = initialWeightRoadMinor; break;
        default:
            weight = initialWeightDefault;
        }

        return weight;
    }

    int interIdEstimatedDistance(const std::vector<OSMElement> &id_list, unsigned int &considered_nodes, unsigned int &considered_distances) {
        considered_nodes = considered_distances = 0;

        if (id_list.empty()) return 0; ///< too few elements as input

        std::unordered_set<uint64_t> node_ids;
        for (auto it = id_list.cbegin(); it != id_list.cend(); ++it) {
            const uint64_t id = (*it).id;
            const OSMElement::ElementType type = (*it).type;
            if (type == OSMElement::Node)
                node_ids.insert(id);
            else if (type == OSMElement::Way) {
                WayNodes wn;
                const bool found = wayNodes->retrieve(id, wn);
                if (found)
                    for (size_t i = 0; i < wn.num_nodes; ++i)
                        node_ids.insert(wn.nodes[i]);
            } else if (type == OSMElement::Relation) {
                RelationMem rm;
                const bool found = relMembers->retrieve(id, rm);
                if (found)
                    for (size_t i = 0; i < rm.num_members; ++i)
                        if (rm.members[i].type == OSMElement::Node)
                            node_ids.insert(rm.members[i].id);
                        else if (rm.members[i].type == OSMElement::Way) {
                            WayNodes wn;
                            const bool found = wayNodes->retrieve(rm.members[i].id, wn);
                            if (found)
                                for (size_t i = 0; i < wn.num_nodes; ++i)
                                    node_ids.insert(wn.nodes[i]);
                        }
            }
        }

        considered_nodes = node_ids.size();
        if (node_ids.size() <= 1)
            return 0; ///< too few nodes found

        /// Whereas std::unordered_set node_ids is good for
        /// collecting unique instances of node ids, it does
        /// not provide a way for fast random access. Therefore,
        /// an array is built based on data collected in the set.
        uint64_t *node_id_array = (uint64_t *)malloc(node_ids.size() * sizeof(uint64_t));
        int i = 0;
        for (auto it = node_ids.cbegin(); it != node_ids.cend(); ++it, ++i)
            node_id_array[i] = *it;

        std::vector<int> distances;
        const int stepcount = min(7, node_ids.size() / 2 + 1);
        size_t step = node_ids.size() / stepcount + 1;
        while (node_ids.size() % step == 0) ++step;
        for (size_t a = 0; a < node_ids.size(); ++a) {
            size_t b = a;
            Coord cA;
            if (node2Coord->retrieve(node_id_array[a], cA))
                for (int s = 0; s < stepcount; ++s) {
                    b = (b + step) % node_ids.size();
                    Coord cB;
                    if (node_id_array[a] < node_id_array[b] && node2Coord->retrieve(node_id_array[b], cB)) {
                        const int d = Coord::distanceLatLon(cA, cB);
                        if (d > 2500000) ///< 2500 km
                            Error::warn("Distance btwn node %llu and %llu is very large: %d", node_id_array[a], node_id_array[b], d);
                        else
                            distances.push_back(d);
                    }
                }
        }
        free(node_id_array);
        considered_distances = distances.size();

        std::sort(distances.begin(), distances.end(), std::less<int>());
        if (distances.size() < 2) return 0; ///< too few distances computed
        Error::debug("1.quartile= %i  median= %i  considered_nodes= %i  considered_distances= %i", distances[distances.size() / 4], distances[distances.size() / 2], considered_nodes, considered_distances);
        return distances[distances.size() / 4]; ///< take first quartile
    }
};

TokenProcessor::TokenProcessor()
    : d(new Private())
{
    /// nothing
}

TokenProcessor::~TokenProcessor() {
    delete d;
}

void TokenProcessor::evaluteWordCombinations(const std::vector<std::string> &word_combinations, WeightedNodeSet &wns) const {
    for (auto itW = word_combinations.cbegin(); itW != word_combinations.cend(); ++itW) {
        const std::string &combined = *itW;
        const char *combined_cstr = combined.c_str();
        std::vector<OSMElement> id_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        if (id_list.empty())
            Error::info("Got no hits for word combination '%s', skipping", combined_cstr);
        else {
            Error::info("Got %i hits for word combination '%s'", id_list.size(), combined_cstr);
            unsigned int considered_nodes, considered_distances;
            const int estDist = d->interIdEstimatedDistance(id_list, considered_nodes, considered_distances);
            if (estDist > 10000) ///< 10 km
                Error::info("Estimated distance (%i km) too large for word combination '%s' (hits=%i), skipping", (estDist + 500) / 1000, combined_cstr, id_list.size());
            else if (considered_nodes == 0)
                Error::info("No node to consider");
            else {
                if (considered_nodes > 3 && considered_distances < considered_nodes)
                    Error::info("Considered more than %i nodes, but only %i distances eventually considered", considered_nodes, considered_distances);
                Error::debug("Estimated distance is %i m", estDist);
                for (std::vector<OSMElement>::const_iterator it = id_list.cbegin(); it != id_list.cend(); ++it) {
                    const uint64_t id = (*it).id;
                    const OSMElement::ElementType type = (*it).type;
                    const OSMElement::RealWorldType realworld_type = (*it).realworld_type;
                    const float weight = Private::initialWeight(realworld_type);
                    Error::debug("weight=%.3f", weight);
                    if (type == OSMElement::Node) {
#ifdef DEBUG
                        Error::debug("   https://www.openstreetmap.org/node/%llu", id);
#endif // DEBUG
                        wns.appendNode(id, weight);
                    } else if (type == OSMElement::Way) {
#ifdef DEBUG
                        Error::debug("   https://www.openstreetmap.org/way/%llu", id);
#endif // DEBUG
                        wns.appendWay(id, weight);
                    } else if (type == OSMElement::Relation) {
#ifdef DEBUG
                        Error::debug("   https://www.openstreetmap.org/relation/%llu", id);
#endif // DEBUG
                        wns.appendRelation(id, weight);
                    } else
                        Error::warn("  Neither node, way, nor relation: %llu", id);
                }
            }
        }
    }
}

std::vector<struct TokenProcessor::RoadMatch> TokenProcessor::evaluteRoads(const std::vector<std::string> &word_combinations, const std::vector<struct Sweden::Road> knownRoads) {
    std::vector<struct RoadMatch> result;
    if (knownRoads.empty()) return result; /// No roads known? Nothing to do -> return

    /// Go through all word combinations (usually 1 to 3 words combined)
    for (auto itW = word_combinations.cbegin(); itW != word_combinations.cend(); ++itW) {
        const std::string &combined = *itW;
        const char *combined_cstr = combined.c_str();

        /// Retrieve all OSM elements matching a given word combination
        std::vector<OSMElement> id_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        if (!id_list.empty()) {
            Error::debug("Got %i hits for word '%s'", id_list.size(), combined_cstr);

            /// Find shortest distance between any OSM element and any road element
            for (auto itR = knownRoads.begin(); itR != knownRoads.end(); ++itR) {
                /// For a particular road, find shortest distance to any OSM element
                uint64_t bestRoadNode = 0, bestWordNode = 0;
                int64_t minSqDistance = INT64_MAX;
                /// Go through all OSM elements
                for (auto itN = id_list.cbegin(); itN != id_list.cend(); ++itN) {
                    const uint64_t id = (*itN).id;
                    const OSMElement::ElementType type = (*itN).type;
                    const OSMElement::RealWorldType realworld_type = (*itN).realworld_type;

                    if (type != OSMElement::Node) {
                        /// Only nodes will be processed; may change in the future
                        continue;
                    }

                    Coord c;
                    const bool foundNode = node2Coord->retrieve(id, c);
                    if (!foundNode) continue;

                    /// Process only places as reference points
                    if (realworld_type == OSMElement::PlaceLarge || realworld_type == OSMElement::PlaceMedium || realworld_type == OSMElement::PlaceSmall) {
                        uint64_t node = 0;
                        int64_t sqDistance = INT64_MAX;
                        /// Given x/y coordinates and a road to process,
                        /// a node and its distance to the coordinates (in decimeter-square)
                        /// will be returned
                        /// Function closestRoadNodeToCoord() may even correct a road's type
                        /// (e.g. if it was unknown due to missing information)
                        sweden->closestRoadNodeToCoord(c.x, c.y, *itR, node, distance);

                        if (sqDistance < minSqDistance) {
                            bestRoadNode = node;
                            bestWordNode = id;
                            minSqDistance = sqDistance;
                        }
                    }
                }

                if (minSqDistance < (INT64_MAX >> 1)) {
                    Error::debug("Distance between '%s' and road %d (type %d): %.1f km (between road node %llu and word's node %llu)", combined_cstr, itR->number, itR->type, sqrt(minSqDistance) / 10000.0, bestRoadNode, bestWordNode);
                    result.push_back(RoadMatch(combined, *itR, bestRoadNode, bestWordNode, sqrt(minSqDistance) + .5));
                }
            }
        }
    }

    /// Sort found road matches using this lambda expression,
    /// closests distances go first
    std::sort(result.begin(), result.end(), [](struct TokenProcessor::RoadMatch & a, struct TokenProcessor::RoadMatch & b) {
        return a.distance < b.distance;
    });

    return result;
}

std::vector<struct TokenProcessor::NearPlaceMatch> TokenProcessor::evaluateNearPlaces(const std::vector<std::string> &word_combinations, const std::vector<struct OSMElement> &places) {
    std::vector<struct TokenProcessor::NearPlaceMatch> result;
    if (places.empty()) return result;; /// No places known? Nothing to do -> return

    /// Retrieve coordinates for all known places
    std::vector<std::pair<struct OSMElement, struct Coord> > placesToCoord;
    for (auto itP = places.cbegin(); itP != places.cend(); ++itP) {
        Coord c;
        if (node2Coord->retrieve(itP->id, c))
            placesToCoord.push_back(std::pair<struct OSMElement, struct Coord>(*itP, c));
    }

    /// Go through all word combinations (usually 1 to 3 words combined)
    for (auto itW = word_combinations.cbegin(); itW != word_combinations.cend(); ++itW) {
        const std::string &combined = *itW;
        const char *combined_cstr = combined.c_str();


        /// Retrieve all OSM elements matching a given word combination
        std::vector<OSMElement> id_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        int64_t minSqDistance = INT64_MAX;
        auto bestPlace = placesToCoord.cend();
        uint64_t bestNode = 0;
        for (auto itN = id_list.cbegin(); itN != id_list.cend(); ++itN) {
            uint64_t id = (*itN).id;
            OSMElement::ElementType type = (*itN).type;

            if (type == OSMElement::Way) {
                /// If current element is a way, simply pick the
                /// way's center node as a representative for this way
                WayNodes wn;
                if (wayNodes->retrieve(id, wn) && wn.num_nodes > 0) {
                    id = wn.nodes[wn.num_nodes / 2];
                    type = OSMElement::Node;
                }
            }

            if (type != OSMElement::Node) {
                /// Only nodes will be processed
                continue;
            }

            Coord c;
            const bool foundNode = node2Coord->retrieve(id, c);
            if (!foundNode) continue;

            for (auto itP = placesToCoord.cbegin(); itP != placesToCoord.cend(); ++itP) {
                const struct OSMElement &place = itP->first;
                const struct Coord &placeCoord = itP->second;

                if (place.id == id) continue; ///< do not compare place with itself
                const int distance = Coord::distanceLatLon(c, placeCoord);
                if (distance < minSqDistance) {
                    minSqDistance = distance;
                    bestPlace = itP;
                    bestNode = id;
                }
            }
        }

        if (minSqDistance < INT64_MAX && bestPlace != placesToCoord.cend() && bestNode > 0)
            result.push_back(NearPlaceMatch(bestPlace->first, bestNode, minSqDistance));
    }

    /// Sort found places-word combinations using this lambda expression,
    /// closests distances go first
    std::sort(result.begin(), result.end(), [](struct TokenProcessor::NearPlaceMatch & a, struct TokenProcessor::NearPlaceMatch & b) {
        return a.distance < b.distance;
    });

#ifdef DEBUG
    for (auto it = result.cbegin(); it != result.cend(); ++it) {
        WriteableString nodeName, placeName;
        nodeNames->retrieve(it->node, nodeName);
        nodeNames->retrieve(it->place.id, placeName);
        Error::debug("Found node %llu (%s) near place %llu (%s) with distance %.1fkm", it->node, nodeName.c_str(), it->place.id, placeName.c_str(), it->distance / 1000.0);
    }
#endif

    return result;
}
