/***************************************************************************
 *   Copyright (C) 2015-2016 by Thomas Fischer <thomas.fischer@his.se>     *
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
#define max(a,b) ((b)<(a)?(a):(b))

class TokenProcessor::Private
{
public:
    explicit Private()
    {
        /// nothing
    }

    int interIdEstimatedDistance(const std::vector<OSMElement> &element_list, unsigned int &considered_nodes, unsigned int &considered_distances, uint64_t &mostCentralNodeId) {
        considered_nodes = considered_distances = 0;
        mostCentralNodeId = INT64_MAX;

        if (element_list.empty()) return 0; ///< too few elements as input

        /// Collect as many nodes (identified by their ids) as referenced
        /// by the provided element_list vector. Ways and relations get
        /// resolved to nodes to some extend.
        std::unordered_set<uint64_t> node_ids;
        for (const OSMElement &element : element_list) {
            if (element.type == OSMElement::Node)
                node_ids.insert(element.id);
            else if (element.type == OSMElement::Way) {
                WayNodes wn;
                if (wayNodes->retrieve(element.id, wn))
                    for (size_t i = 0; i < wn.num_nodes; ++i)
                        node_ids.insert(wn.nodes[i]);
            } else if (element.type == OSMElement::Relation) {
                RelationMem rm;
                if (relMembers->retrieve(element.id, rm))
                    for (size_t i = 0; i < rm.num_members; ++i)
                        if (rm.members[i].type == OSMElement::Node)
                            node_ids.insert(rm.members[i].id);
                        else if (rm.members[i].type == OSMElement::Way) {
                            WayNodes wn;
                            if (wayNodes->retrieve(rm.members[i].id, wn))
                                for (size_t i = 0; i < wn.num_nodes; ++i)
                                    node_ids.insert(wn.nodes[i]);
                        }
            }
        }

        considered_nodes = node_ids.size();
        if (considered_nodes <= 1)
            return 0; ///< too few nodes found

        /// Whereas std::unordered_set node_ids is good for
        /// collecting unique instances of node ids, it does
        /// not provide a way for fast random access. Therefore,
        /// an array is built based on data collected in the set.
        uint64_t *node_id_array = (uint64_t *)malloc(considered_nodes * sizeof(uint64_t));
        int i = 0;
        for (auto it = node_ids.cbegin(); it != node_ids.cend(); ++it, ++i)
            node_id_array[i] = *it;

        std::vector<int> distances;
        // TODO remove static const int very_few_nodes = 5;
        const int stepcount = min(considered_nodes - 1, min(7, max(1, considered_nodes / 2)));
        size_t step = considered_nodes / stepcount;
        /// Ensure that both numbers have no common divisor except for 1
        while (considered_nodes % step == 0 && step < considered_nodes) ++step;
        if (step >= considered_nodes) step = 1; ///< this should only happen for considered_nodes<=3
        step = max(1, min(considered_nodes - 1, step)); ///< ensure that step is within valid bounds

        int bestDistanceAverage = INT_MAX;
        for (size_t a = 0; a < considered_nodes; ++a) {
            size_t b = a;
            Coord cA;
            if (node2Coord->retrieve(node_id_array[a], cA)) {
                int sumDistances = 0, countDistances = 0;
                for (int s = 0; s < stepcount; ++s) {
                    b = (b + step) % considered_nodes;
                    Coord cB;
                    if (node2Coord->retrieve(node_id_array[b], cB)) {
                        const int d = Coord::distanceLatLon(cA, cB);
                        if (a < b) ///< record each distance only once
                            distances.push_back(d);
                        sumDistances += d;
                        ++countDistances;
                    }
                }

                const int averageDistance = countDistances > 0 ? sumDistances / countDistances : 0;
                if (countDistances > 0 && averageDistance < bestDistanceAverage) {
                    bestDistanceAverage = averageDistance;
                    mostCentralNodeId = node_id_array[a];
                }
            }
        }
        free(node_id_array);
        considered_distances = distances.size();

        std::sort(distances.begin(), distances.end(), std::less<int>());
        if (distances.size() == 0) return 0; ///< too few distances computed
        return distances[distances.size() / 4]; ///< take first quartile
    }

    static double qualityForRealWorldTypes(const OSMElement &element) {
        switch (element.realworld_type) {
        case OSMElement::PlaceLargeArea: return 0.8;
        case OSMElement::PlaceLarge: return 1.0;
        case OSMElement::PlaceMedium: return 0.85;
        case OSMElement::PlaceSmall: return 0.7;
        case OSMElement::Island: return 0.6;
        case OSMElement::Building: return 0.9;
        default: return 0.5;
        }
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

std::vector<struct TokenProcessor::RoadMatch> TokenProcessor::evaluteRoads(const std::vector<std::string> &word_combinations, const std::vector<struct Sweden::Road> knownRoads) {
    std::vector<struct RoadMatch> result;
    if (knownRoads.empty()) return result; /// No roads known? Nothing to do -> return

    /// Go through all word combinations (usually 1 to 3 words combined)
    for (auto itW = word_combinations.cbegin(); itW != word_combinations.cend(); ++itW) {
        const std::string &combined = *itW;
        const char *combined_cstr = combined.c_str();

        /// Retrieve all OSM elements matching a given word combination
        std::vector<OSMElement> element_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        if (!element_list.empty()) {
            Error::debug("Got %i hits for word '%s'", element_list.size(), combined_cstr);

            /// Find shortest distance between any OSM element and any road element
            for (auto itR = knownRoads.begin(); itR != knownRoads.end(); ++itR) {
                /// For a particular road, find shortest distance to any OSM element
                uint64_t bestRoadNode = 0, bestWordNode = 0;
                Sweden::Road bestRoad(*itR);
                int minDistance = INT_MAX;
                /// Go through all OSM elements
                for (auto itN = element_list.cbegin(); itN != element_list.cend(); ++itN) {
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
                        Sweden::RoadType roadType = sweden->closestRoadNodeToCoord(c.x, c.y, *itR, node, distance);

                        if (distance < minDistance) {
                            bestRoadNode = node;
                            bestWordNode = id;
                            minDistance = distance;
                            bestRoad.type = roadType;
                        }
                    }
                }

                if (minDistance < (INT_MAX >> 1)) {
                    Error::debug("Distance between '%s' and road %s: %.1f km (between road node %llu and word's node %llu)", combined_cstr, itR->operator std::string().c_str(), minDistance / 1000.0, bestRoadNode, bestWordNode);
                    result.push_back(RoadMatch(combined, bestRoad, bestRoadNode, bestWordNode, minDistance));
                }
            }
        }
    }

    /// Sort found road matches using this lambda expression,
    /// closests distances go first
    std::sort(result.begin(), result.end(), [](struct TokenProcessor::RoadMatch & a, struct TokenProcessor::RoadMatch & b) {
        return a.distance < b.distance;
    });

    /// Set quality for results as follows based on distance:
    ///   1 km or less -> 1.0
    ///  10 km         -> 0.5
    /// 100 km or more -> 0.0
    /// quality = 1 - (log10(d) - 3) / 2
    for (struct TokenProcessor::RoadMatch &roadMatch : result) {
        roadMatch.quality = 1.0 - (log10(roadMatch.distance) - 3) / 2.0;
        /// Normalize into range 0.0..1.0
        if (roadMatch.quality < 0.0) roadMatch.quality = 0.0;
        else if (roadMatch.quality > 1.0) roadMatch.quality = 1.0;
    }

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
        std::vector<OSMElement> element_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        for (const OSMElement &element : element_list) {
            const OSMElement eNode = element.type == OSMElement::Node ? element : getNodeInOSMElement(element);
            if (eNode.type != OSMElement::Node)
                /// Resolving relations or ways to a node failed
                continue;

            int minDistance = INT_MAX;
            auto bestPlace = placesToCoord.cend();
            Coord c;
            const bool foundNode = node2Coord->retrieve(eNode.id, c);
            if (!foundNode) continue;

            for (auto itP = placesToCoord.cbegin(); itP != placesToCoord.cend(); ++itP) {
                const struct OSMElement &place = itP->first;
                const struct Coord &placeCoord = itP->second;

                if (place.id == eNode.id || place.id == element.id) continue; ///< do not compare place with itself
                const int distance = Coord::distanceLatLon(c, placeCoord);
                if (distance < minDistance) {
                    minDistance = distance;
                    bestPlace = itP;
                }
            }

            static const int limitDistance = 20000; ///< 20km
            if (minDistance <= limitDistance && bestPlace != placesToCoord.cend())
                result.push_back(NearPlaceMatch(combined, bestPlace->first /** struct OSMElement */, element, minDistance));
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
        std::string globalNameA = a.global.name(), globalNameB;
        utf8tolower(globalNameA);
        if (a.global.id == b.global.id)
            /// Avoid looking up same id twice
            globalNameB = globalNameA;
        else {
            globalNameB = b.global.name();
            utf8tolower(globalNameB);
        }
        /// std::string::find(..) will return the largest positive value for
        /// std::string::size_type if the argument was not found (std::string::npos),
        /// otherwise the position where found in the string (starting at 0).
        const std::string::size_type findGlobalNameInCombinedA = a.word_combination.find(globalNameA);
        const std::string::size_type findGlobalNameInCombinedB = b.word_combination.find(globalNameB);

        /// Set quality during sorting if not already set for match a
        if (a.quality < 0.0) {
            /**
             * Quality is set to 1.0 if the global name does not occur in a's word combination.
             * Quality is set to 0.0 if the global name occurs at the first position in a's word
             * combination (includes case where global name is equal to a's word combination).
             * If the global name occurs in later positions in a's word combination, the
             * quality value linearly increases towards 1.0.
             */
            a.quality = findGlobalNameInCombinedA > a.word_combination.length() ? 1.0 : findGlobalNameInCombinedA / (double)(a.word_combination.length() - findGlobalNameInCombinedA + 1);

            /// Reduce quality for certain types of places, like small hamlets
            a.quality *= Private::qualityForRealWorldTypes(a.global);
        }
        /// Set quality during sorting if not already set for match b
        if (b.quality < 0.0) {
            b.quality = findGlobalNameInCombinedB > b.word_combination.length() ? 1.0 : findGlobalNameInCombinedB / (double)(b.word_combination.length() - findGlobalNameInCombinedB + 1);
            b.quality *= Private::qualityForRealWorldTypes(b.global);
        }

        /// Larger values findGlobalNameInCombinedX, i.e. late or no hits for global name preferred
        if (findGlobalNameInCombinedA < findGlobalNameInCombinedB) return false;
        else if (findGlobalNameInCombinedA > findGlobalNameInCombinedB) return true;
        else {
            /// Prefer local places to be near its global place
            return a.distance < b.distance;
        }
    });

#ifdef DEBUG
    for (const TokenProcessor::NearPlaceMatch &npm : result)
        Error::debug("Found %s (%s) near place %s (%s) with distance %.1fkm", npm.local.operator std::string().c_str(), npm.local.name().c_str(), npm.global.operator std::string().c_str(), npm.global.name().c_str(), npm.distance / 1000.0);
#endif

    return result;
}

std::vector<struct TokenProcessor::UniqueMatch> TokenProcessor::evaluateUniqueMatches(const std::vector<std::string> &word_combinations) const {
    std::vector<struct TokenProcessor::UniqueMatch> result;

    /// Go through all word combinations (usually 1 to 3 words combined)
    for (const std::string &combined : word_combinations) {
        const char *combined_cstr = combined.c_str();

        /// Retrieve all OSM elements matching a given word combination
        std::vector<OSMElement> element_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        /// Even 'unique' locations may consist of multiple nodes or ways,
        /// such as the shape of a single building
        if (element_list.size() > 0 && element_list.size() < 30 /** arbitrarily chosen value */) {
            if (element_list.size() == 1) {
                /// For single-element results, set inter-node distance to
                /// 1m as distance==0 is interpreted as error
                result.push_back(UniqueMatch(combined, element_list.front(), Private::qualityForRealWorldTypes(element_list.front())));
            } else { /** element_list.size() > 1 */
                /// Estimate the inter-node distance. For an 'unique' location,
                /// all nodes must be close by as they are supposed to belong
                /// together, e.g. the nodes that shape a building
                unsigned int considered_nodes = 0, considered_distances = 0;
                uint64_t centralNodeId;
                int internodeDistanceMeter = d->interIdEstimatedDistance(element_list, considered_nodes, considered_distances, centralNodeId);
                /// Check if estimated 1. quartile of inter-node distance is less than 500m
                if (internodeDistanceMeter > 0 && internodeDistanceMeter < 500) {
                    OSMElement bestElement;
                    Coord centralNodeCoord, bestElementCoord;
                    node2Coord->retrieve(centralNodeId, centralNodeCoord);
                    int bestElementsDistanceToCentralNode = INT_MAX;
                    for (const OSMElement &e : element_list) {
                        OSMElement eNode = getNodeInOSMElement(e);
                        node2Coord->retrieve(eNode.id, bestElementCoord);
                        const int distance = Coord::distanceXY(centralNodeCoord, bestElementCoord);
                        if (distance < bestElementsDistanceToCentralNode) {
                            bestElementsDistanceToCentralNode = distance;
                            bestElement = e;
                        }
                    }

                    if (bestElementsDistanceToCentralNode < 1000)
                        result.push_back(UniqueMatch(combined, bestElement, Private::qualityForRealWorldTypes(bestElement)));
                }
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

        /// Set quality during sorting if not already set for match a
        if (a.quality < 0.0) {
            a.quality = Private::qualityForRealWorldTypes(a.element);

            if (countSpacesA >= 3) a.quality *= 1.0;
            else if (countSpacesA == 2) a.quality *= 0.9;
            else if (countSpacesA == 1) a.quality *= 0.75;
            else /** countSpacesA == 0 */ a.quality *= 0.5;
        }
        /// Set quality during sorting if not already set for match b
        if (b.quality < 0.0) {
            b.quality = Private::qualityForRealWorldTypes(b.element);

            if (countSpacesB >= 3) b.quality *= 1.0;
            else if (countSpacesB == 2) b.quality *= 0.9;
            else if (countSpacesB == 1) b.quality *= 0.75;
            else /** countSpacesB == 0 */ b.quality *= 0.5;
        }

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

    for (const std::string &combined : word_combinations) {
        const char *combined_cstr = combined.c_str();

        /// Retrieve all OSM elements matching a given word combination
        const std::vector<struct OSMElement> element_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        for (const OSMElement &element : element_list) {
            const OSMElement &eNode = element.type == OSMElement::Node ? element : getNodeInOSMElement(element);
            if (eNode.type != OSMElement::Node) continue; ///< Not a node

            for (const Sweden::KnownAdministrativeRegion &adminReg : adminRegions) {
                const uint64_t adminRegRelId = adminReg.relationId;
                const std::string adminRegName = adminReg.name;
                if (adminRegRelId > 0 && adminRegRelId != element.id && adminRegRelId != eNode.id && sweden->nodeInsideRelationRegion(eNode.id, adminRegRelId))
                    result.push_back(AdminRegionMatch(combined, element, adminRegRelId, adminRegName));
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

        /// Set quality during sorting if not already set for match a
        if (a.quality < 0.0) {
            /**
             * Quality is set to 1.0 if the admin region's name does not occur in a's name.
             * Quality is set to 0.0 if the admin region's name occurs at the first position
             *  in a'sname (includes case where admin region's name is equal to a's name).
             * If the admin region's name occurs in later positions in a's name, the
             * quality value linearly increases towards 1.0.
             */
            a.quality = findAdminInCombinedA > a.name.length() ? 1.0 : findAdminInCombinedA / (double)(a.name.length() - findAdminInCombinedA + 1);
            if (a.match.realworld_type < OSMElement::PlaceLarge || a.match.realworld_type > OSMElement::PlaceSmall)
                /// Prefer 'places' over anything else
                a.quality *= .9;
        }
        /// Set quality during sorting if not already set for match b
        if (b.quality < 0.0) {
            b.quality = findAdminInCombinedB > b.name.length() ? 1.0 : findAdminInCombinedB / (double)(b.name.length() - findAdminInCombinedB + 1);
            if (b.match.realworld_type < OSMElement::PlaceLarge || b.match.realworld_type > OSMElement::PlaceSmall)
                /// Prefer 'places' over anything else
                b.quality *= .9;
        }

        /// Larger values findAdminInCombinedX, i.e. late or no hits for admin region preferred
        if (findAdminInCombinedA < findAdminInCombinedB) return false;
        else if (findAdminInCombinedA > findAdminInCombinedB) return true;
        else { /** findAdminInCombinedA == findAdminInCombinedB */
            if (a.match.realworld_type >= OSMElement::PlaceLarge && a.match.realworld_type <= OSMElement::PlaceSmall && (b.match.realworld_type < OSMElement::PlaceLarge || b.match.realworld_type > OSMElement::PlaceSmall))
                return true;
            else if (b.match.realworld_type >= OSMElement::PlaceLarge && b.match.realworld_type <= OSMElement::PlaceSmall && (a.match.realworld_type < OSMElement::PlaceLarge || a.match.realworld_type > OSMElement::PlaceSmall))
                return false;

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
