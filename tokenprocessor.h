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
        int64_t distance; ///< in decimeter (10th of a meter)
    };

    std::vector<struct RoadMatch> evaluteRoads(const std::vector<std::string> &word_combinations, std::vector<struct Sweden::Road> knownRoads);

private:
    class Private;
    Private *const d;
};

#endif // TOKENPROCESSOR_H
