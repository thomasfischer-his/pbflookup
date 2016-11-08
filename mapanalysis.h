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

#ifndef MAPANALYSIS_H
#define MAPANALYSIS_H

#include <string>
#include <vector>
#include <unordered_set>

class Tokenizer;

class MapAnalysis
{
public:
    explicit MapAnalysis(Tokenizer *tokenizer);
    ~MapAnalysis();

    struct RoadCrossing {
        RoadCrossing(const std::string &_word_match_A, const std::string &_word_match_B, const uint64_t _overlapNodeId, size_t _word_fragment_size_squared)
            : word_match_A(_word_match_A), word_match_B(_word_match_B), overlapNodeId(_overlapNodeId), word_fragment_size_squared(_word_fragment_size_squared) {
            /// nothing
        }

        /// The two road names that led to finding this crossing
        const std::string word_match_A, word_match_B;
        /// An OSM node that is shared between both roads
        const uint64_t overlapNodeId;
        /// For each of the two road names, take the number of words in each
        /// (at most max_words_per_combination), square it, and add numbers
        /// for both roads.
        /// This value may range from (1^2 + 1^2) = 2 up to
        /// max_words_per_combination^2 + max_words_per_combination^2
        /// Useful for assessing the quality of a given result, as it has higher
        /// values for 'longer' road names, e.g. preferring 'lilla berggatan'
        /// over just 'lilla' or just 'berggatan'.
        const size_t word_fragment_size_squared;
    };

    /**
     * From a given sequence of words (taken from an input text, only small words
     * and black-listed words removed, but not reordered or deduplicated), identify
     * roads that cross. Roads are identified by their names, and road names must
     * occurr in close proximity (only a few other words in between). Crossing roads
     * by definition share at least one OSM node (its id will be part of the result).
     *
     * @param words sequence of words
     * @param max_words_per_combination how many words a street name may be composed of
     * @param max_inter_word_combo_distance how many words may at most be between two street names
     * @return list of results
     */
    std::vector<struct RoadCrossing> identifyCrossingRoads(const std::vector<std::string> &words, const size_t max_words_per_combination = 3, const size_t max_inter_word_combo_distance = 5);

private:
    class Private;
    Private *const d;
};

#endif // MAPANALYSIS_H
