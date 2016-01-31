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
    std::vector<struct Sweden::Road> knownRoads;

    explicit Private()
    {
        /// nothing
    }

    Sweden::RoadType lettersToRoadType(const char *letters, uint16_t roadNumber) {
        if (letters[0] >= 'c' && letters[0] <= 'z' && letters[1] == '\0') {
            switch (letters[0]) {
            case 'c':
                return Sweden::LanC;
            case 'd':
                return Sweden::LanD;
            case 'e':
                return Sweden::identifyEroad(roadNumber);
            case 'f':
                return Sweden::LanF;
            case 'g':
                return Sweden::LanG;
            case 'h':
                return Sweden::LanH;
            case 'i':
                return Sweden::LanI;
            case 'k':
                return Sweden::LanK;
            case 'm':
                return Sweden::LanM;
            case 'n':
                return Sweden::LanN;
            case 'o':
                return Sweden::LanO;
            case 's':
                return Sweden::LanS;
            case 't':
                return Sweden::LanT;
            case 'u':
                return Sweden::LanU;
            case 'w':
                return Sweden::LanW;
            case 'x':
                return Sweden::LanX;
            case 'y':
                return Sweden::LanY;
            case 'z':
                return Sweden::LanZ;
            }
        } else if (letters[0] >= 'a' && letters[0] <= 'b' && letters[1] >= 'b' && letters[1] <= 'd' && letters[2] == '\0') {
            if (letters[0] == 'a' && letters[1] == 'b')
                return Sweden::LanAB;
            else if (letters[0] == 'a' && letters[1] == 'c')
                return Sweden::LanAC;
            else if (letters[0] == 'b' && letters[1] == 'd')
                return Sweden::LanBD;
        }

        return Sweden::UnknownRoadType;
    }

    static inline double initialWeight(OSMElement::RealWorldType node_type, int word_count) {
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

void TokenProcessor::evaluteWordCombinations(const std::vector<std::string> &words, WeightedNodeSet &wns) const {
    static const size_t combined_len = 8188;
    static const int max_number_words_combined = 3; // TODO put into configuration file
    char combined[combined_len + 4];
    for (int s = min(max_number_words_combined, words.size()); s >= 1; --s) {
        for (size_t i = 0; i <= words.size() - s; ++i) {
            char *p = combined;
            for (int k = 0; k < s; ++k) {
                if (k > 0)
                    p += snprintf(p, combined_len - (p - combined), " ");
                p += snprintf(p, combined_len - (p - combined), "%s", words[i + k].c_str());
            }

            const size_t wordlen = strlen(combined);
            std::vector<OSMElement> id_list = swedishTextTree->retrieve(combined, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
            if (id_list.empty())
                Error::info("Got no hits for word '%s' (s=%i), skipping", combined, s);
            else {
                Error::info("Got %i hits for word '%s' (s=%i)", id_list.size(), combined, s);
                unsigned int considered_nodes, considered_distances;
                const int estDist = d->interIdEstimatedDistance(id_list, considered_nodes, considered_distances);
                if (estDist > 10000) ///< 10 km
                    Error::info("Estimated distance (%i km) too large for word '%s' (s=%i, hits=%i), skipping", (estDist + 500) / 1000, combined, s, id_list.size());
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
                        const float weight = Private::initialWeight(realworld_type, s);
                        Error::debug("s=%d  wordlen=%d  weight=%.3f", s, wordlen, weight);
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
}

void TokenProcessor::evaluteRoads(const std::vector<std::string> &words, WeightedNodeSet &wns) const {
    static const std::string swedishWordRv("rv"); ///< as in Rv. 43
    static const std::string swedishWordWay("v\xc3\xa4g");
    static const std::string swedishWordTheWay("v\xc3\xa4gen");
    static const std::string swedishWordNationalWay("riksv\xc3\xa4g");

    d->knownRoads.clear();

    for (size_t i = 0; i < words.size(); ++i) {
        uint16_t roadNumber = 0;
        Sweden::RoadType roadType = Sweden::National;
        if (i < words.size() - 1 && (words[i][1] == '\0' || words[i][2] == '\0') && words[i + 1][0] >= '1' && words[i + 1][0] <= '9') {
            char *next;
            const char *cur = words[i + 1].c_str();
            roadNumber = (uint16_t)strtol(cur, &next, 10);
            if (roadNumber > 0 && next > cur)
                roadType = d->lettersToRoadType(words[i].c_str(), roadNumber);
            else {
                Error::debug("Not a road number:%s", cur);
                roadNumber = -1;
            }
        } else if (words[i][0] >= 'a' && words[i][0] <= 'z' && words[i][1] >= '1' && words[i][1] <= '9') {
            const char buffer[] = {words[i][0], '\0'};
            char *next;
            const char *cur = words[i].c_str() + 1;
            roadNumber = (uint16_t)strtol(cur, &next, 10);
            if (roadNumber > 0 && next > cur)
                roadType = d->lettersToRoadType(buffer, roadNumber);
            else {
                Error::debug("Not a road number:%s", cur);
                roadNumber = -1;
            }
        } else if (words[i][0] >= 'a' && words[i][0] <= 'b' && words[i][1] >= 'a' && words[i][1] <= 'd' && words[i][2] >= '1' && words[i][2] <= '9') {
            const char buffer[] = {words[i][0], words[i][1], '\0'};
            char *next;
            const char *cur = words[i].c_str() + 2;
            roadNumber = (uint16_t)strtol(cur, &next, 10);
            if (roadNumber > 0 && next > cur)
                roadType = d->lettersToRoadType(buffer, roadNumber);
            else {
                Error::debug("Not a road number:%s", cur);
                roadNumber = -1;
            }
        } else if (i < words.size() - 1 && (swedishWordRv.compare(words[i]) == 0 || swedishWordWay.compare(words[i]) == 0 || swedishWordTheWay.compare(words[i]) == 0 || swedishWordNationalWay.compare(words[i]) == 0) && words[i + 1][0] >= '1' && words[i + 1][0] <= '9') {
            roadType = Sweden::National;
            char *next;
            const char *cur = words[i + 1].c_str();
            roadNumber = (uint16_t)strtol(cur, &next, 10);
        }

        if (roadNumber > 0 && roadType != Sweden::UnknownRoadType) {
#ifdef DEBUG
            Error::info("Found road %i (type %i)", roadNumber, roadType);
#endif // DEBUG

            /*
            double weight = 2.0; ///< default weight for regional roads
            if (roadType == Sweden::National) weight = 5.0; ///< weight for national roads
            else if (roadType == Sweden::Europe) weight = 15.0; ///< weight for European roads

            std::vector<uint64_t> ways = d->sweden->waysForRoad(roadType, roadNumber);
            for (std::vector<uint64_t>::const_iterator it = ways.cbegin(); it != ways.cend(); ++it) {
                wns.appendWay(*it, weight);
            }
            */

            bool known = false;
            for (auto it = d->knownRoads.cbegin(); !known && it != d->knownRoads.cend(); ++it) {
                const Sweden::Road &road = *it;
                known = (road.type == roadType) && (road.number == roadNumber);
            }
            if (!known) d->knownRoads.push_back(Sweden::Road(roadType, roadNumber));
        }
    }
}

std::vector<struct Sweden::Road> &TokenProcessor::knownRoads() const {
    return d->knownRoads;
}
