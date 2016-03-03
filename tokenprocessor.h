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

#ifndef TOKENPROCESSOR_H
#define TOKENPROCESSOR_H

#include "weightednodeset.h"
#include "sweden.h"

class TokenProcessor
{
public:
    explicit TokenProcessor();
    ~TokenProcessor();

    void evaluteWordCombinations(const std::vector<std::string> &word_combinations, WeightedNodeSet &wns) const;

    struct RoadMatch {
        RoadMatch(const std::string &_word_combination, const Sweden::Road &_road, uint64_t _bestRoadNode, uint64_t _bestWordNode, int64_t _distance)
            : word_combination(_word_combination), road(_road), bestRoadNode(_bestRoadNode), bestWordNode(_bestWordNode), distance(_distance) {
            /// nothing
        }

        std::string word_combination;
        Sweden::Road road;
        uint64_t bestRoadNode, bestWordNode;
        int64_t distance; ///< in meter
    };

    std::vector<struct RoadMatch> evaluteRoads(const std::vector<std::string> &word_combinations, const std::vector<struct Sweden::Road> knownRoads);

    struct NearPlaceMatch {
        NearPlaceMatch(struct OSMElement _place, uint64_t _node, int64_t _distance)
            : place(_place), node(_node), distance(_distance) {
            /// nothing
        }

        struct OSMElement place;
        uint64_t node;
        int64_t distance; ///< in meter
    };

    /**
     * Based on a list of word combinations and a list of places,
     * determine a list of place-word combo pairs that match
     * local-scope locations (as described by word combinations)
     * and global-scope locations (places).
     * The resulting list will be sorted by distance between place
     * and word combination's closest location, shorter distances
     * first.
     * @param word_combinations Word combinations describing local-scope locations
     * @param places Places of cities, towns, hamlets, ...
     * @return List of matching pairs of place-word combinations
     */
    std::vector<struct NearPlaceMatch> evaluateNearPlaces(const std::vector<std::string> &word_combinations, const std::vector<struct OSMElement> &places);

    struct UniqueMatch {
        UniqueMatch(std::string _name, uint64_t _id)
            : name(_name), id(_id) {
            /// nothing
        }

        std::string name;
        uint64_t id;
    };

    std::vector<struct UniqueMatch> evaluateUniqueMatches(const std::vector<std::string> &word_combinations) const;

private:
    class Private;
    Private *const d;
};

#endif // TOKENPROCESSOR_H
