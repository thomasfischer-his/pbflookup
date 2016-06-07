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

    struct InterIdEstimatedDistanceResult {
        unsigned int considered_nodes = 0;
        unsigned int considered_distances = 0;
        uint64_t mostCentralNodeId = 0;
        int firstQuartileDistance = 0;
    };

    Private::InterIdEstimatedDistanceResult interIdEstimatedDistance(const std::vector<OSMElement> &element_list) {
        InterIdEstimatedDistanceResult result;

        if (element_list.empty()) return InterIdEstimatedDistanceResult(); ///< too few elements as input

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
                if (relMembers->retrieve(element.id, rm)) {
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
        }

        result.considered_nodes = node_ids.size();
        if (result.considered_nodes <= 1)
            return InterIdEstimatedDistanceResult(); ///< too few nodes found

        /// Whereas std::unordered_set node_ids is good for
        /// collecting unique instances of node ids, it does
        /// not provide a way for fast random access. Therefore,
        /// an array is built based on data collected in the set.
        uint64_t *node_id_array = (uint64_t *)calloc(result.considered_nodes, sizeof(uint64_t));
        int i = 0;
        for (auto it = node_ids.cbegin(); it != node_ids.cend(); ++it, ++i)
            node_id_array[i] = *it;

        std::vector<int> distances;
        // TODO remove static const int very_few_nodes = 5;
        const int stepcount = min(result.considered_nodes - 1, min(7, max(1, result.considered_nodes / 2)));
        size_t step = result.considered_nodes / stepcount;
        /// Ensure that both numbers have no common divisor except for 1
        while (result.considered_nodes % step == 0 && step < result.considered_nodes) ++step;
        if (step >= result.considered_nodes) step = 1; ///< this should only happen for considered_nodes<=3
        step = max(1, min(result.considered_nodes - 1, step)); ///< ensure that step is within valid bounds

        int bestDistanceAverage = INT_MAX;
        for (size_t a = 0; a < result.considered_nodes; ++a) {
            size_t b = a;
            Coord cA;
            if (node2Coord->retrieve(node_id_array[a], cA)) {
                int sumDistances = 0, countDistances = 0;
                for (int s = 0; s < stepcount; ++s) {
                    b = (b + step) % result.considered_nodes;
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
                    result.mostCentralNodeId = node_id_array[a];
                }
            }
        }
        free(node_id_array);
        result.considered_distances = distances.size();
        if (result.considered_distances == 0) return InterIdEstimatedDistanceResult(); ///< too few distances computed

        std::sort(distances.begin(), distances.end(), std::less<int>());
        result.firstQuartileDistance = distances[distances.size() / 4]; ///< take first quartile

        return result;
    }

    static double qualityForRealWorldTypes(const OSMElement &element) {
        switch (element.realworld_type) {
        case OSMElement::PlaceLargeArea: return 0.8;
        case OSMElement::PlaceLarge: return 1.0;
        case OSMElement::PlaceMedium: return 0.85;
        case OSMElement::PlaceSmall: return 0.7;
        case OSMElement::Island: return 0.85;
        case OSMElement::Water: return 0.8;
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
    for (const OSMElement &element : places) {
        Coord c;
        if (getCenterOfOSMElement(element, c))
            placesToCoord.push_back(std::make_pair(element, c));
    }

    /// Go through all word combinations (usually 1 to 3 words combined)
    for (const std::string &combined : word_combinations) {
        const char *combined_cstr = combined.c_str();

        /// Retrieve all OSM elements matching a given word combination
        std::vector<OSMElement> element_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        for (const OSMElement &element : element_list) {
            int minDistance = INT_MAX;
            auto bestPlace = placesToCoord.cend();

            /// Get something like the gravitational center of the OSM element
            Coord c;
            if (!getCenterOfOSMElement(element, c)) continue;

            for (auto itP = placesToCoord.cbegin(); itP != placesToCoord.cend(); ++itP) {
                const struct OSMElement &place = itP->first;
                const struct Coord &placeCoord = itP->second;

                if (place.id == element.id) continue; ///< do not compare place with itself
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
                /// Directly accept single-element results
                result.push_back(UniqueMatch(combined, element_list.front(), Private::qualityForRealWorldTypes(element_list.front())));
            } else { /** element_list.size() > 1 */
                /// Estimate the inter-node distance. For an 'unique' location,
                /// all nodes must be close by as they are supposed to belong
                /// together, e.g. the nodes that shape a building
                Private::InterIdEstimatedDistanceResult interIdEstimatedDistanceResult = d->interIdEstimatedDistance(element_list);
                static const int innerThreshold = 1000; ///< 1km (=10^3)
                static const int outerThreshold = 31622; ///< 31.6km (=10^4.5)
                /// Check if estimated 1. quartile of inter-node distance is less than 10km (outerThreshold)
                if (interIdEstimatedDistanceResult.firstQuartileDistance > 0 && interIdEstimatedDistanceResult.firstQuartileDistance < outerThreshold) {
                    OSMElement bestElement;
                    Coord centralNodeCoord;
                    node2Coord->retrieve(interIdEstimatedDistanceResult.mostCentralNodeId, centralNodeCoord);
                    int bestElementsDistanceToCentralNode = INT_MAX;
                    for (const OSMElement &e : element_list) {
                        Coord c;
                        if (!getCenterOfOSMElement(e, c)) continue;
                        const int distance = Coord::distanceXY(centralNodeCoord, c);
                        if (distance < bestElementsDistanceToCentralNode) {
                            bestElementsDistanceToCentralNode = distance;
                            bestElement = e;
                        }
                    }

                    if (bestElementsDistanceToCentralNode < outerThreshold) {
                        double quality = Private::qualityForRealWorldTypes(bestElement);
                        if (bestElementsDistanceToCentralNode > innerThreshold)
                            /// Scale quality from 0.0 (distance 10km and larger) to 1.0 (distance 317m and less)
                            quality *= (4.5 - log10(bestElementsDistanceToCentralNode))/1.5;

                        result.push_back(UniqueMatch(combined, bestElement, quality));
                    }
                }
            }
        }
    }

    std::sort(result.begin(), result.end(), [](struct UniqueMatch & a, struct UniqueMatch & b) {
        return a.quality > b.quality;
    });

    return result;
}

std::vector<struct TokenProcessor::AdminRegionMatch> TokenProcessor::evaluateAdministrativeRegions(const std::vector<struct Sweden::KnownAdministrativeRegion> adminRegions, const std::vector<std::string> &word_combinations) const {
    std::vector<struct TokenProcessor::AdminRegionMatch> result;
    if (adminRegions.empty() || word_combinations.empty()) return result; ///< Nothing to do

#ifdef CPUTIMER
    int64_t cputime, walltime;
    int64_t retrievalTime = 0, insideTestTime = 0, sortingTime = 0;
    Timer timer;
#endif //  CPUTIMER

    for (const std::string &combined : word_combinations) {
        const char *combined_cstr = combined.c_str();

        /// Retrieve all OSM elements matching a given word combination
#ifdef CPUTIMER
        timer.start();
#endif // CPUTIMER
        const std::vector<struct OSMElement> element_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
#ifdef CPUTIMER
        timer.elapsed(&cputime, &walltime);
        retrievalTime += cputime;
#endif // CPUTIMER

        OSMElement prev_element;
        Coord prev_coord;
        for (const OSMElement &element : element_list) {
            /// Using the following heuristic: if the previous element is
            /// of the same type and has an id that is very close, then
            /// it is assumed that it describes a position very close
            /// to the previous element's location.
            /// Thus it is safe to ignore this element and to avoid costly
            /// geometric calculations.
            if (element.type == prev_element.type) {
                const int delta = element.id - prev_element.id;
                static const int delta_threshold = 4;
                if (delta <= delta_threshold && delta >= -delta_threshold) {
                    prev_element = element;
                    getCenterOfOSMElement(element, prev_coord);
                    continue;
                }
            }

            Coord coord;
            if (!getCenterOfOSMElement(element, coord)) continue;
            if (prev_coord.isValid() && Coord::distanceXYsquare(coord, prev_coord) < 9000000L /** 3km */) {
                prev_element = element;
                prev_coord = coord;
                continue;
            }

            int inside_admin_level = INT_MAX;
            for (const Sweden::KnownAdministrativeRegion &adminReg : adminRegions) {
                /// Keep track which level the latest 'inside'
                /// admin region match had for this node. A node
                /// cannot be in another admin region of the same
                /// level (this other admin region has a
                /// non-overlapping area).
                /// So if another admin region is to be tested,
                /// it can be safely skipped if it has the same
                /// admin level as the last 'inside' match.
                if (adminReg.admin_level >= inside_admin_level)
                    continue;

                if (adminReg.relationId > 0 && adminReg.relationId != element.id) {
#ifdef CPUTIMER
                    timer.start();
#endif // CPUTIMER
                    const bool inside = sweden->nodeInsideRelationRegion(coord, adminReg.relationId);
#ifdef CPUTIMER
                    timer.elapsed(&cputime, &walltime);
                    insideTestTime += cputime;
#endif // CPUTIMER
                    if (inside) {
                        result.push_back(AdminRegionMatch(combined, element, adminReg));
                        inside_admin_level = adminReg.admin_level;
                    }
                }
            }

            prev_element = element;
            prev_coord = coord;
        }
    }


#ifdef CPUTIMER
    timer.start();
#endif // CPUTIMER
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
        const std::string::size_type findAdminInCombinedA = a.combined.find(a.adminRegion.name);
        const std::string::size_type findAdminInCombinedB = b.combined.find(b.adminRegion.name);

        /// Set quality during sorting if not already set for match a
        if (a.quality < 0.0) {
            /**
             * Quality is set to 1.0 if the admin region's name does not occur in a's name.
             * Quality is set to 0.0 if the admin region's name occurs at the first position
             *  in a'sname (includes case where admin region's name is equal to a's name).
             * If the admin region's name occurs in later positions in a's name, the
             * quality value linearly increases towards 1.0.
             */
            a.quality = findAdminInCombinedA > a.combined.length() ? 1.0 : findAdminInCombinedA / (double)(a.combined.length() - findAdminInCombinedA + 1);
            if (a.quality > 0.0) {
                if (a.match.realworld_type < OSMElement::PlaceLarge || a.match.realworld_type > OSMElement::PlaceSmall)
                    /// Prefer 'places' over anything else
                    a.quality *= .9;
                /// Add a penalty for high-level administrative regions, e.g. prefer Stockholm city of Stockholm county
                const double admin_level_scaling = (/** always between 2 and 9 */ max(2, min(9, a.adminRegion.admin_level)) + 18) / 27.0;
                a.quality *= admin_level_scaling;
            }
        }
        /// Set quality during sorting if not already set for match b
        if (b.quality < 0.0) {
            b.quality = findAdminInCombinedB > b.combined.length() ? 1.0 : findAdminInCombinedB / (double)(b.combined.length() - findAdminInCombinedB + 1);
            if (b.quality > 0.0) {
                if (b.match.realworld_type < OSMElement::PlaceLarge || b.match.realworld_type > OSMElement::PlaceSmall)
                    /// Prefer 'places' over anything else
                    b.quality *= .9;
                const double admin_level_scaling = (/** always between 2 and 9 */ max(2, min(9, b.adminRegion.admin_level)) + 18) / 27.0;
                b.quality *= admin_level_scaling;
            }
        }

        /// Larger values findAdminInCombinedX, i.e. late or no hits for admin region preferred
        if (findAdminInCombinedA < findAdminInCombinedB) return false;
        else if (findAdminInCombinedA > findAdminInCombinedB) return true;
        else { /** findAdminInCombinedA == findAdminInCombinedB */
            if (a.match.realworld_type >= OSMElement::PlaceLarge && a.match.realworld_type <= OSMElement::PlaceSmall && (b.match.realworld_type < OSMElement::PlaceLarge || b.match.realworld_type > OSMElement::PlaceSmall))
                return true;
            else if (b.match.realworld_type >= OSMElement::PlaceLarge && b.match.realworld_type <= OSMElement::PlaceSmall && (a.match.realworld_type < OSMElement::PlaceLarge || a.match.realworld_type > OSMElement::PlaceSmall))
                return false;

            const size_t countSpacesA = std::count(a.combined.cbegin(), a.combined.cend(), ' ');
            const size_t countSpacesB = std::count(b.combined.cbegin(), b.combined.cend(), ' ');
            if (countSpacesA < countSpacesB) return false;
            else if (countSpacesA > countSpacesB) return true;
            else /** countSpacesA == countSpacesB */
                return a.combined.length() > b.combined.length();
        }
    });

#ifdef CPUTIMER
    timer.elapsed(&cputime, &walltime);
    sortingTime = cputime;
    Error::info("evaluateAdministrativeRegions:  retrievalTime= %.3lf  insideTestTime= %.3lf  sortingTime= %.3lf", retrievalTime / 1000.0, insideTestTime / 1000.0, sortingTime / 1000.0);
#endif //CPUTIMER

    return result;
}
