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
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "mapanalysis.h"

#include <algorithm>

#include "tokenizer.h"
#include "swedishtexttree.h"
#include "globalobjects.h"
#include "error.h"

#define max(a,b) ((b)<(a)?(a):(b))

class MapAnalysis::Private
{
public:
    Tokenizer *tokenizer;

    explicit Private(Tokenizer *_tokenizer)
        : tokenizer(_tokenizer)
    {
        /// nothing
    }
};

MapAnalysis::MapAnalysis(Tokenizer *tokenizer)
    : d(new Private(tokenizer))
{
    /// nothing
}

MapAnalysis::~MapAnalysis() {
    delete d;
}

std::vector<struct MapAnalysis::RoadCrossing> MapAnalysis::identifyCrossingRoads(const std::vector<std::string> &words, const size_t max_words_per_combination, const size_t max_inter_word_combo_distance) {
    std::vector<struct RoadCrossing> result;
    std::unordered_set<uint64_t> knownCrossings; ///< keep track of identified OSM nodes of crossing do avoid duplicates in the result

    /// Iterate over the number of words that may be between the word combinations each representing a road name
    /// Distance between road names goes from zero to up to and including 'max_inter_word_combo_distance'
    for (size_t inter_word_combo_distance = 0; inter_word_combo_distance <= max_inter_word_combo_distance; ++inter_word_combo_distance)
        /// How many words may be part of word combination representing the first road name?
        /// Number of words goes from 1 up to and including 'max_words_per_combination'
        for (size_t word_combo_A_len = 1; word_combo_A_len <= max(1, max_words_per_combination); ++word_combo_A_len)
            /// How many words may be part of word combination representing the second road name?
            /// Number of words goes from 1 up to and including 'max_words_per_combination'
            for (size_t word_combo_B_len = 1; word_combo_B_len <= max(1, max_words_per_combination); ++word_combo_B_len) {
                /// How many position may a have a 'sliding window' over the vector of words have,
                /// where the window's size is the sum of both word combinations' lengths and the
                /// distance between both word combinations
                const signed long int max_i = (signed long int)words.size() - (signed long int)word_combo_A_len - (signed long int)word_combo_B_len - (signed long int)inter_word_combo_distance;
                for (signed long int i = 0; i <= max_i; ++i) {
                    /// Initialize both word combinations with subvectors of 'words'
                    std::vector<std::string> word_combo_A_seq(word_combo_A_len), word_combo_B_seq(word_combo_B_len);
                    std::copy(words.begin() + i, words.begin() + i + word_combo_A_len, word_combo_A_seq.begin());
                    std::copy(words.begin() + i + word_combo_A_len + inter_word_combo_distance, words.begin() + i + word_combo_A_len + inter_word_combo_distance + word_combo_B_len, word_combo_B_seq.begin());

                    /// Combine word combinations into single strings and generate grammatical variations.
                    /// Example: Input word combo sequence {'gröna', 'vägen'} may result in a list of
                    /// {'gröna vägen', 'gröna väg'}
                    const std::vector<std::string> word_combo_A_list = d->tokenizer->generate_word_combinations(word_combo_A_seq, word_combo_A_len, word_combo_A_len);
                    if (word_combo_A_list.empty()) continue; ///< may happen if single input word is blacklisted, e.g. 'norra'
                    const std::vector<std::string> word_combo_B_list = d->tokenizer->generate_word_combinations(word_combo_B_seq, word_combo_B_len, word_combo_B_len);
                    if (word_combo_B_list.empty()) continue; ///< may happen if single input word is blacklisted, e.g. 'norra'

                    /// For each generate grammatical variation (e.g. 'gröna vägen' and 'gröna väg')
                    for (const std::string &word_combo_A : word_combo_A_list) {
                        std::unordered_set<uint64_t> node_ids_A;
                        /// Determine a vector of all nodes, ways, and relations matching a potential road name
                        const std::vector<OSMElement> &element_list_A = swedishTextTree->retrieve(word_combo_A.c_str(), (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
                        if (element_list_A.empty()) continue; ///< No OSM element matches the search term in word_combo_A
                        /// Resolve ways into individual nodes, skip relations
                        for (const OSMElement &element_A : element_list_A) {
                            if (element_A.type == OSMElement::Node)
                                node_ids_A.insert(element_A.id);
                            else if (element_A.type == OSMElement::Way) {
                                WayNodes wn;
                                if (wayNodes->retrieve(element_A.id, wn))
                                    for (size_t i = 0; i < wn.num_nodes; ++i)
                                        node_ids_A.insert(wn.nodes[i]);
                            }
                        }
                        if (node_ids_A.empty()) continue; ///< No OSM nodes are referred to the list of elements as found when searching for word_combo_A

                        /// For each generate grammatical variation (e.g. 'gröna vägen' and 'gröna väg')
                        for (const std::string &word_combo_B : word_combo_B_list) {
                            /// Skip word combo B if it equals word combo A or if either word combo is part (substring) of the other word combo
                            if (word_combo_A == word_combo_B || word_combo_A.find(word_combo_B) != std::string::npos || word_combo_B.find(word_combo_A) != std::string::npos)
                                continue;

                            std::unordered_set<uint64_t> node_ids_B;
                            /// Determine a vector of all nodes, ways, and relations matching a potential road name
                            const std::vector<OSMElement> &element_list_B = swedishTextTree->retrieve(word_combo_B.c_str(), (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
                            if (element_list_B.empty()) continue; ///< No OSM element matches the search term in word_combo_B
                            /// Resolve ways into individual nodes, skip relations
                            for (const OSMElement &element_B : element_list_B) {
                                if (element_B.type == OSMElement::Node)
                                    node_ids_B.insert(element_B.id);
                                else if (element_B.type == OSMElement::Way) {
                                    WayNodes wn;
                                    if (wayNodes->retrieve(element_B.id, wn))
                                        for (size_t i = 0; i < wn.num_nodes; ++i)
                                            node_ids_B.insert(wn.nodes[i]);
                                }
                            }
                            if (node_ids_B.empty()) continue; ///< No OSM nodes are referred to the list of elements as found when searching for word_combo_B

                            /// Identify at least one OSM node shared between both roads
                            uint64_t overlapNodeId = 0;
                            for (const uint64_t id_A : node_ids_A)
                                for (const uint64_t id_B : node_ids_B)
                                    if (id_A == id_B) {
                                        overlapNodeId = id_A;
                                        break;
                                    }
                            if (overlapNodeId == 0) continue; ///< Not a single OSM node shared between the set of nodes as found for word_combo_A and word_combo_B

                            if (knownCrossings.find(overlapNodeId) != knownCrossings.end()) continue; ///< already known crossing
                            knownCrossings.insert(overlapNodeId);

                            RoadCrossing rc(word_combo_A, word_combo_B, overlapNodeId, word_combo_A_len * word_combo_A_len + word_combo_B_len * word_combo_B_len);
                            result.push_back(rc);
                        }
                    }
                }
            }

    return result;
}
