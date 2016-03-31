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
#include "helper.h"

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
        case OSMElement::PlaceLargeArea: weight = initialWeightPlaceLarge; break;
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

    int interIdEstimatedDistance(const std::vector<OSMElement> &id_list, unsigned int &considered_nodes, unsigned int &considered_distances, uint64_t &mostCentralNodeId) {
        considered_nodes = considered_distances = 0;
        mostCentralNodeId = 0;

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

        int bestDistance = INT_MAX;
        for (size_t a = 0; a < node_ids.size(); ++a) {
            size_t b = a;
            Coord cA;
            if (node2Coord->retrieve(node_id_array[a], cA)) {
                int sumDistances = 0;
                for (int s = 0; s < stepcount; ++s) {
                    b = (b + step) % node_ids.size();
                    Coord cB;
                    if (node_id_array[a] < node_id_array[b] && node2Coord->retrieve(node_id_array[b], cB)) {
                        const int d = Coord::distanceLatLon(cA, cB);
                        distances.push_back(d);
                        sumDistances += d;
                    }
                }

                if (sumDistances < bestDistance) {
                    bestDistance = sumDistances;
                    mostCentralNodeId = node_id_array[a];
                }
            }
        }
        free(node_id_array);
        considered_distances = distances.size();

        std::sort(distances.begin(), distances.end(), std::less<int>());
        if (distances.size() < 2) return 0; ///< too few distances computed
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
            uint64_t mostCentralNodeId;
            const int estDist = d->interIdEstimatedDistance(id_list, considered_nodes, considered_distances, mostCentralNodeId);
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
                int minDistance = INT_MAX;
                /// Go through all OSM elements
                for (auto itN = id_list.cbegin(); itN != id_list.cend(); ++itN) {
                    const uint64_t id = (*itN).id;
                    const OSMElement::ElementType type = (*itN).type;
                    const OSMElement::RealWorldType realworld_type = (*itN).realworld_type;

                    if (type != OSMElement::Node) {
                        /// Only nodes will be processed; may change in the future
                        continue;
                    }

                    /// Process only places as reference points
                    Coord c;
                    if ((realworld_type == OSMElement::PlaceLargeArea || realworld_type == OSMElement::PlaceLarge || realworld_type == OSMElement::PlaceMedium || realworld_type == OSMElement::PlaceSmall) && node2Coord->retrieve(id, c)) {
                        uint64_t node = 0;
                        int distance = INT_MAX;
                        /// Given x/y coordinates and a road to process,
                        /// a node and its distance to the coordinates (in decimeter-square)
                        /// will be returned
                        /// Function closestRoadNodeToCoord() may even correct a road's type
                        /// (e.g. if it was unknown due to missing information)
                        sweden->closestRoadNodeToCoord(c.x, c.y, *itR, node, distance);

                        if (distance < minDistance) {
                            bestRoadNode = node;
                            bestWordNode = id;
                            minDistance = distance;
                        }
                    }
                }

                if (minDistance < (INT_MAX >> 1)) {
                    Error::debug("Distance between '%s' and road %s %d: %.1f km (between road node %llu and word's node %llu)", combined_cstr, Sweden::roadTypeToString(itR->type).c_str(), itR->number, minDistance / 1000.0, bestRoadNode, bestWordNode);
                    result.push_back(RoadMatch(combined, *itR, bestRoadNode, bestWordNode, minDistance));
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
    if (places.empty()) return result; /// No places known? Nothing to do -> return

    /// Retrieve coordinates for all known places
    std::vector<std::pair<struct OSMElement, struct Coord> > placesToCoord;
    for (auto itP = places.cbegin(); itP != places.cend(); ++itP) {
        Coord c;
        if (node2Coord->retrieve(itP->id, c))
            placesToCoord.push_back(std::pair<struct OSMElement, struct Coord>(*itP, c));
    }

    /// Go through all word combinations (usually 1 to 3 words combined)
    for (const std::string &combined : word_combinations) {
        const char *combined_cstr = combined.c_str();

        /// Retrieve all OSM elements matching a given word combination
        std::vector<OSMElement> id_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        for (auto itN = id_list.cbegin(); itN != id_list.cend(); ++itN) {
            const OSMElement element = itN->type == OSMElement::Node ? *itN : getNodeInOSMElement(*itN);
            if (element.type != OSMElement::Node)
                /// Resolving relations or ways to a node failed
                continue;

            int minDistance = INT_MAX;
            auto bestPlace = placesToCoord.cend();
            Coord c;
            const bool foundNode = node2Coord->retrieve(element.id, c);
            if (!foundNode) continue;

            for (auto itP = placesToCoord.cbegin(); itP != placesToCoord.cend(); ++itP) {
                const struct OSMElement &place = itP->first;
                const struct Coord &placeCoord = itP->second;

                if (place.id == element.id || place.id == itN->id) continue; ///< do not compare place with itself
                const int distance = Coord::distanceLatLon(c, placeCoord);
                if (distance < minDistance) {
                    minDistance = distance;
                    bestPlace = itP;
                }
            }

            static const int limitDistance = 20000; ///< 20km
            if (minDistance <= limitDistance && bestPlace != placesToCoord.cend())
                result.push_back(NearPlaceMatch(combined, bestPlace->first /** struct OSMElement */, *itN, minDistance));
        }
    }

    /// Sort results using this lambda expression
    std::sort(result.begin(), result.end(), [](struct TokenProcessor::NearPlaceMatch & a, struct TokenProcessor::NearPlaceMatch & b) {
        /**
         * The following sorting criteria will be apply, top down. If there
         * is a tie, the next following criteria will be applied.
         * 1. Matches (i.e. local places) which do not contain the global
         *    places's name will be preferred over matches which contain
         *    the global places's name towards its end which will be
         *    preferred over matches which contain the global places's name
         *    at its beginning.
         * 2. Prefer local places that are closer to their global places'
         *    location.
         */
        WriteableString globalNameA, globalNameB;
        nodeNames->retrieve(a.global.id, globalNameA);
        utf8tolower(globalNameA);
        if (a.global.id == b.global.id)
            /// Avoid looking up same id twice
            globalNameB = globalNameA;
        else {
            nodeNames->retrieve(b.global.id, globalNameB);
            utf8tolower(globalNameB);
        }
        /// std::string::find(..) will return the largest positive value for
        /// std::string::size_type if the argument was not found (std::string::npos),
        /// otherwise the position where found in the string (starting at 0).
        const std::string::size_type findGlobalNameInCombinedA = a.word_combination.find(globalNameA);
        const std::string::size_type findGlobalNameInCombinedB = b.word_combination.find(globalNameB);
        /// Larger values findGlobalNameInCombinedX, i.e. late or no hits for global name preferred
        if (findGlobalNameInCombinedA < findGlobalNameInCombinedB) return false;
        else if (findGlobalNameInCombinedA > findGlobalNameInCombinedB) return true;
        else {
            /// Prefer local places to be near its global place
            return a.distance < b.distance;
        }
    });

#ifdef DEBUG
    for (auto it = result.cbegin(); it != result.cend(); ++it) {
        WriteableString localName, globalName;
        nodeNames->retrieve(it->local.id, localName);
        nodeNames->retrieve(it->global.id, globalName);
        Error::debug("Found node %llu (%s) near place %llu (%s) with distance %.1fkm", it->local.id, localName.c_str(), it->global.id, globalName.c_str(), it->distance / 1000.0);
    }
#endif

    return result;
}

std::vector<struct TokenProcessor::UniqueMatch> TokenProcessor::evaluateUniqueMatches(const std::vector<std::string> &word_combinations) const {
    std::vector<struct TokenProcessor::UniqueMatch> result;

    /// Go through all word combinations (usually 1 to 3 words combined)
    for (auto itW = word_combinations.cbegin(); itW != word_combinations.cend(); ++itW) {
        const std::string &combined = *itW;
        const char *combined_cstr = combined.c_str();

        /// Retrieve all OSM elements matching a given word combination
        std::vector<OSMElement> id_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        /// Even 'unique' locations may consist of multiple nodes or ways,
        /// such as the shape of a single building
        if (id_list.size() > 0 && id_list.size() < 30 /** arbitrarily chosen value */) {
            unsigned int considered_nodes = 0, considered_distances = 0;
            uint64_t mostCentralId = 0;
            int internodeDistanceMeter = 0;
            if (id_list.size() == 1) {
                /// For single id results, set inter-node distance to
                /// 1m (distance==0 is interpreted as error)
                internodeDistanceMeter = 1;
                /// If the single id is a way or a relation, resolve it to
                /// a node; a node is needed to retrieve a coordinate from it
                const OSMElement element = id_list.front().type == OSMElement::Node ? id_list.front() : getNodeInOSMElement(id_list.front());
                if (element.type == OSMElement::Node)
                    /// Resolving relations or ways to a node succeeded
                    mostCentralId = element.id;
                else
                    continue;
            } else { /** id_list.size() > 1 */
                /// Estimate the inter-node distance. For an 'unique' location,
                /// all nodes must be close by as they are supposed to belong
                /// together, e.g. the nodes that shape a building
                internodeDistanceMeter = d->interIdEstimatedDistance(id_list, considered_nodes, considered_distances, mostCentralId);
            }

            if (internodeDistanceMeter > 0 && internodeDistanceMeter < 1000) {
                /// Estimated 1. quartile of inter-node distance is 1km
                result.push_back(UniqueMatch(combined, mostCentralId));
            }
        }
    }

    std::sort(result.begin(), result.end(), [](struct UniqueMatch & a, struct UniqueMatch & b) {
        /**
         * Sort unique matches first by number of spaces (tells from
         * how many words a word combination was composed from; it is
         * assumed that more words in a combination make the combination
         * more specific and as such a better hit), and as secondary
         * criterion by names' length (weak assumption: longer names are
         * more specific than shorter names).
         */
        const size_t countSpacesA = std::count(a.name.cbegin(), a.name.cend(), ' ');
        const size_t countSpacesB = std::count(b.name.cbegin(), b.name.cend(), ' ');
        if (countSpacesA < countSpacesB) return false;
        else if (countSpacesA > countSpacesB) return true;
        else /** countSpacesA == countSpacesB */
            return a.name.length() > b.name.length();
    });

    return result;
}

std::vector<struct TokenProcessor::AdminRegionMatch> TokenProcessor::evaluateAdministrativeRegions(const std::vector<struct Sweden::KnownAdministrativeRegion> adminRegions, const std::vector<std::string> &word_combinations) const {
    std::vector<struct TokenProcessor::AdminRegionMatch> result;
    if (adminRegions.empty() || word_combinations.empty()) return result; ///< Nothing to do

    for (auto itW = word_combinations.cbegin(); itW != word_combinations.cend(); ++itW) {
        const std::string &combined = *itW;
        const char *combined_cstr = combined.c_str();

        /// Retrieve all OSM elements matching a given word combination
        const std::vector<struct OSMElement> id_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        for (auto itId = id_list.cbegin(); itId != id_list.cend(); ++itId) {
            const OSMElement element = itId->type == OSMElement::Node ? *itId : getNodeInOSMElement(*itId);
            if (element.type != OSMElement::Node) continue; ///< Not a node

            for (auto itAR = adminRegions.cbegin(); itAR != adminRegions.cend(); ++itAR) {
                const uint64_t adminRegRelId = itAR->relationId;
                const std::string adminRegName = itAR->name;
                if (adminRegRelId > 0 && adminRegRelId != element.id && sweden->nodeInsideRelationRegion(element.id, adminRegRelId))
                    result.push_back(AdminRegionMatch(combined, *itId, adminRegRelId, adminRegName));
            }
        }
    }


    std::sort(result.begin(), result.end(), [](struct AdminRegionMatch & a, struct AdminRegionMatch & b) {
        /**
         * The following sorting criteria will be apply, top down. If there
         * is a tie, the next following criteria will be applied.
         * 1. Matches which do not contain the admin region's name will
         *    be preferred over matches which contain the admin region's
         *    name towards its end which will be preferred over matches
         *    which contain the admin region's at its beginning.
         * 2. Matches with more spaces will be preferred over matches
         *    with fewer spaces (tells from how many words a word combination
         *    was composed from; it is assumed that more words in a
         *    combination make the combination more specific and as such a
         *    better hit).
         * 3. Matches with longer names will be preferred over matches with
         *    shorter names. Weak criteria, used only to break ties.
         */
        /// std::string::find(..) will return the largest positive value for
        /// std::string::size_type if the argument was not found (std::string::npos),
        /// otherwise the position where found in the string (starting at 0).
        const std::string::size_type findAdminInCombinedA = a.name.find(a.adminRegionName);
        const std::string::size_type findAdminInCombinedB = b.name.find(b.adminRegionName);
        /// Larger values findAdminInCombinedX, i.e. late or no hits for admin region preferred
        if (findAdminInCombinedA < findAdminInCombinedB) return false;
        else if (findAdminInCombinedA > findAdminInCombinedB) return true;
        else { /** findAdminInCombinedA == findAdminInCombinedB */
            const size_t countSpacesA = std::count(a.name.cbegin(), a.name.cend(), ' ');
            const size_t countSpacesB = std::count(b.name.cbegin(), b.name.cend(), ' ');
            if (countSpacesA < countSpacesB) return false;
            else if (countSpacesA > countSpacesB) return true;
            else /** countSpacesA == countSpacesB */
                return a.name.length() > b.name.length();
        }
    });

    return result;
}
