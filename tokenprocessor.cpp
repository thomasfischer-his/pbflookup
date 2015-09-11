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

#include "sweden.h"
#include "swedishtexttree.h"

#define min(a,b) ((b)>(a)?(a):(b))

class TokenProcessor::Private
{
public:
    SwedishText::Tree *swedishTextTree;
    IdTree<Coord> *coords;
    IdTree<WayNodes> *waynodes;
    IdTree<RelationMem> *relmem;
    Sweden *sweden;

    explicit Private(SwedishText::Tree *_swedishTextTree, IdTree<Coord> *_coords, IdTree<WayNodes> *_waynodes, IdTree<RelationMem> *_relmem, Sweden *_sweden)
        : swedishTextTree(_swedishTextTree), coords(_coords), waynodes(_waynodes), relmem(_relmem), sweden(_sweden)
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
};

TokenProcessor::TokenProcessor(SwedishText::Tree *swedishTextTree, IdTree<Coord> *coords, IdTree<WayNodes> *waynodes, IdTree<RelationMem> *relmem, Sweden *sweden)
    : d(new Private(swedishTextTree, coords, waynodes, relmem, sweden))
{
    /// nothing
}

TokenProcessor::~TokenProcessor() {
    delete d;
}

void TokenProcessor::evaluteWordCombinations(const std::vector<std::string> &words, WeightedNodeSet &wns) const {
    static const size_t combined_len = 8188;
    char combined[combined_len + 4];
    for (int s = min(3, words.size()); s >= 1; --s) {
        for (size_t i = 0; i <= words.size() - s; ++i) {
            char *p = combined;
            for (int k = 0; k < s; ++k) {
                if (k > 0)
                    p += snprintf(p, combined_len - (p - combined), " ");
                p += snprintf(p, combined_len - (p - combined), "%s", words[i + k].c_str());
            }

            const size_t wordlen = strlen(combined);
            std::vector<OSMElement> id_list = d->swedishTextTree->retrieve(combined);
            if (!id_list.empty()) {
                if (id_list.size() > 1000)
                    Error::debug("Got too many hits (%i) for word '%s' (s=%i), skipping", id_list.size(), combined, s);
                else {
                    Error::debug("Got %i hits for word '%s' (s=%i)", id_list.size(), combined, s);
                    for (std::vector<OSMElement>::const_iterator it = id_list.cbegin(); it != id_list.cend(); ++it) {
                        const uint64_t id = (*it).id;
                        const OSMElement::ElementType type = (*it).type;
                        if (type == OSMElement::Node) {
#ifdef DEBUG
                            Error::debug("   https://www.openstreetmap.org/node/%llu", id);
#endif // DEBUG
                            wns.appendNode(id, s, wordlen);
                        } else if (type == OSMElement::Way) {
#ifdef DEBUG
                            Error::debug("   https://www.openstreetmap.org/way/%llu", id);
#endif // DEBUG
                            wns.appendWay(id, s, wordlen);
                        } else if (type == OSMElement::Relation) {
#ifdef DEBUG
                            Error::debug("   https://www.openstreetmap.org/relation/%llu", id);
#endif // DEBUG
                            wns.appendRelation(id, s, wordlen);
                        } else
                            Error::warn("  Neither node, way, nor relation: %llu", id);
                    }
                }
            }
        }
    }
}

void TokenProcessor::evaluteRoads(const std::vector<std::string> &words, WeightedNodeSet &wns) const {
    static const std::string swedishWordWay("v\xc3\xa4g");
    static const std::string swedishWordTheWay("v\xc3\xa4gen");
    static const std::string swedishWordNationalWay("riksv\xc3\xa4g");

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
        } else if (i < words.size() - 1 && (swedishWordWay.compare(words[i]) == 0 || swedishWordTheWay.compare(words[i]) == 0 || swedishWordNationalWay.compare(words[i]) == 0) && words[i + 1][0] >= '1' && words[i + 1][0] <= '9') {
            roadType = Sweden::National;
            char *next;
            const char *cur = words[i + 1].c_str();
            roadNumber = (uint16_t)strtol(cur, &next, 10);
        }

        if (roadNumber > 0 && roadType != Sweden::UnknownRoadType) {
#ifdef DEBUG
            Error::info("Found road %i (type %i)", roadNumber, roadType);
#endif // DEBUG

            double weight = 2.0; ///< default weight for regional roads
            if (roadType == Sweden::National) weight = 5.0; ///< weight for national roads
            else if (roadType == Sweden::Europe) weight = 15.0; ///< weight for European roads

            std::vector<uint64_t> ways = d->sweden->waysForRoad(roadType, roadNumber);
            for (std::vector<uint64_t>::const_iterator it = ways.cbegin(); it != ways.cend(); ++it) {
                wns.appendWay(*it, weight);
            }
        }
    }
}
