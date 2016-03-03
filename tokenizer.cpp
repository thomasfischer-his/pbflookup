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

#include "tokenizer.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <unordered_set>

#include "error.h"
#include "config.h"

#define min(a,b) ((b)>(a)?(a):(b))

class Tokenizer::Private
{
private:
    Tokenizer *p;

    std::vector<std::string> stopwords;

    void load_stopwords(const char *stopwordfilename) {
        stopwords.clear();

        /// Stopword file has to be sorted with
        ///   LC_ALL=C sort -u ...
        std::ifstream stopwordsfile(stopwordfilename);
        if (stopwordsfile.is_open()) {
            Error::info("Reading stopword file: %s", stopwordfilename);
            std::string line;
            while (getline(stopwordsfile, line)) {
                if (line[0] == 0 || line[0] == '#') continue; ///< skip empty lines and comments
                stopwords.push_back(line);
            }
            stopwordsfile.close();
        } else
            Error::warn("Could not open stopword file: %s", stopwordfilename);
    }

public:
    std::vector<std::string> input_lines;

    Private(Tokenizer *parent)
        : p(parent) {
        load_stopwords(stopwordfilename);
    }

    ~Private() {
        /// nothing
    }

    bool is_stopword(const std::string &word) {
        int a = 0, b = stopwords.size() - 1;
        int cmp = a < b ? /** will enter while loop and set value properly */ 0 : (a == b ? /** only single word in stopword list, compare to it */ word.compare(stopwords[a]) : /** stopword list is empty */ 1);
        while (a < b) {
            const int p = (b + a) / 2;
            cmp = word.compare(stopwords[p]);
            if (a + 1 == b /** assert: a==p */)
                return cmp == 0 || word.compare(stopwords[b]) == 0;
            else if (cmp < 0)
                b = p;
            else if (cmp > 0)
                a = p;
            else /** cmp==0, i.e. word equals stopwords[p], i.e. word is a stopword */
                return 1;
        }
        return cmp == 0;
    }

    static unsigned char utf8tolower(const unsigned char &prev_c, unsigned char c) {
        if ((c >= 'A' && c <= 'Z') ||
                (prev_c == 0xc3 && c >= 0x80 && c <= 0x9e /** poor man's Latin-1 Supplement lower case */))
            c |= 0x20;
        return c;
    }

};

Tokenizer::Tokenizer()
    : d(new Tokenizer::Private(this)) {
    if (d == NULL)
        Error::err("Could not allocate memory for Tokenizer::Private");
}

Tokenizer::~Tokenizer() {
    delete d;
}

int Tokenizer::read_words(const std::string &text, std::vector<std::string> &words, Multiplicity multiplicity) {
    std::stringstream ss(text);
    return read_words(ss, words, multiplicity);
}

int Tokenizer::read_words(std::istream &input, std::vector<std::string> &words, Multiplicity multiplicity) {
    std::string line;
    static const std::string gap(" ?!\"'#%*&()=,;._\n\r\t");
    std::unordered_set<std::string> known_words;
    d->input_lines.clear();

    while (getline(input, line))
    {
        if (line[0] == '\0' || line[0] == '#') continue; ///< skip empty lines and comments
        d->input_lines.push_back(line); ///< store line for future reference

        unsigned char prev_c = '\0';
        std::string lastword;
        for (std::string::const_iterator it = line.cbegin(); it != line.cend(); ++it) {
            if (gap.find(*it) == std::string::npos) {
                /// Character is not a 'gap' character
                /// First, convert character to lower case
                const unsigned char c = Private::utf8tolower(prev_c, *it);
                /// Second, add character to current word
                lastword.append((char *)(&c), 1);
                prev_c = c;
            } else if (!lastword.empty()) {
                /// Character is a 'gap' character and the current word is not empty
                if (!d->is_stopword(lastword)) {
                    /// Current word is not a stop word
                    if (multiplicity == Duplicates)
                        /// If duplicates are allowed, memorize word
                        words.push_back(lastword);
                    else if (multiplicity == Unique && known_words.find(lastword) == known_words.end()) {
                        /// If no duplicates are allowed and word is not yet know, memorize it
                        words.push_back(lastword);
                        known_words.insert(lastword);
                    }
                }
                /// Reset current word
                lastword.clear();
                prev_c = 0;
            }
        }
        if (!lastword.empty()) {
            /// Very last word in line is not empty
            if (!d->is_stopword(lastword)) {
                /// Very last word is not a stop word
                if (multiplicity == Duplicates)
                    /// If duplicates are allowed, memorize word
                    words.push_back(lastword);
                else if (multiplicity == Unique && known_words.find(lastword) == known_words.end()) {
                    /// If no duplicates are allowed and word is not yet know, memorize it
                    words.push_back(lastword);
                    known_words.insert(lastword);
                }
            }
        }
    }

    return words.size();
}

int Tokenizer::generate_word_combinations(const std::vector<std::string> &words, std::vector<std::string> &combinations, const size_t words_per_combination, const Multiplicity multiplicity) {
    combinations.clear();
    std::unordered_set<std::string> known_combinations;

    for (int s = min(words_per_combination, words.size()); s >= 1; --s) {
        for (size_t i = 0; i <= words.size() - s; ++i) {
            std::string combined_word;
            for (int k = 0; k < s; ++k) {
                if (k > 0)
                    combined_word.append(" ", 1);
                combined_word.append(words[i + k]);
            }
            if (multiplicity == Duplicates)
                combinations.push_back(combined_word);
            else if (multiplicity == Unique)
                known_combinations.insert(combined_word);
        }
    }

    if (multiplicity == Unique && !known_combinations.empty() && combinations.empty())
        std::copy(known_combinations.cbegin(), known_combinations.cend(), std::back_inserter(combinations));

    return combinations.size();
}

std::string Tokenizer::input_text() const {
    std::string result;
    for (auto it = d->input_lines.cbegin(); it != d->input_lines.cend(); ++it) {
        if (!result.empty()) result.append("\n");
        result.append(*it);
    }
    return result;
}
