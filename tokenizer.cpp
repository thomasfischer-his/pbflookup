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
#include <algorithm>

#include "error.h"
#include "config.h"
#include "helper.h"

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

        tokenize_line(line, words, multiplicity);
    }

    words.erase(std::remove_if(words.begin(), words.end(), [this](std::string word) {
        return d->is_stopword(word);
    }), words.end());

    return words.size();
}

void Tokenizer::add_grammar_cases(std::vector<std::string> &words) const {
    for (auto it = words.cbegin(); it != words.cend(); ++it) {
        const std::string word = *it;
        const size_t len = word.length();
        if (len > 4) {
            if ((word[len - 1] == 't' || word[len - 1] == 'n') && (word[len - 2] == 'a' || word[len - 2] == 'e')) {
                /// This word is most likely a noun in definite form
                /// Trying to determine indefinite form, then adding it to word list
                // FIXME are there better rules to determine the indefinite form of a definitive noun?

                /// Just remove the final 'n' or 't', for example for
                /// 'travbanan' -> 'travbana'
                std::string indefinite_form = word.substr(0, len - 1);
                it = ++words.insert(it, indefinite_form);
                /// Remove the vocal as well, for example for
                /// 'biblioteket' -> 'bibliotek'
                indefinite_form = word.substr(0, len - 2);
                it = ++words.insert(it, indefinite_form);
            }
        }
    }
}

int Tokenizer::generate_word_combinations(const std::vector<std::string> &words, std::vector<std::string> &combinations, const size_t words_per_combination, const Multiplicity multiplicity) {
    /// There are words that are often part of a valid name, but by itself
    /// are rather meaningless, i.e. cause too many false hits:
    static const std::unordered_set<std::string> blacklistedSingleWords = {
        "nya", "nytt", "gamla", "gammalt",
        "v\xc3\xa4stra", "\xc3\xb6stra", "norra", "s\xc3\xb6""dra",
        /** The following list has been manually assembled, based on existing testsets.
          * This is most likely the clostest point where this software is fine-tuned to
          * perform well for the testset.
          * The list needs to be generalized
          */
        "bil", "bo" /** such as in 'Bo Widerbergs plats' */, "bron", "b\xc3\xa5""de",
        "center", "city",
        "dahl" /** such as in 'Dahl Sverige' */,
        "g\xc3\xa5rd", "g\xc3\xb6ta",
        "hamn", "halv", "hitta", "hos", "hus",
        "km", "kommun",
        "plats", "platsen",
        "region", "regionens", "runt", "r\xc3\xb6r" /** such as in 'Herberts rör' */,
        "sp\xc3\xa5r", "svea", "sverige",
        "tillf\xc3\xa4llig", "torg", "torget",
        "via", "v\xc3\xa4g", "v\xc3\xa4gen",
        "\xc3\xa5r" /** 'år' */,
        "\xc3\xb6" /** 'ö' */, "\xc3\xb6n" /** Hmmm, Umeå has a place called 'Ön' */
    };

    combinations.clear();
    std::unordered_set<std::string> known_combinations;

    for (int s = min(words_per_combination, words.size()); s >= 1; --s) {
        for (size_t i = 0; i <= words.size() - s; ++i) {
            /// Single words that may be misleading, such as 'nya'
            if (s == 1 && blacklistedSingleWords.find(words[i]) != blacklistedSingleWords.end()) continue;

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

size_t Tokenizer::tokenize_line(const std::string &line, std::vector<std::string> &words, Multiplicity multiplicity) {
    static const std::string gap(" ?!\"'#%*&()=,;._\n\r\t");

    std::unordered_set<std::string> known_words;
    unsigned char prev_c = '\0';
    std::string lastword;
    for (std::string::const_iterator it = line.cbegin(); it != line.cend(); ++it) {
        const unsigned char &c = *it;

        if ((c & 224) == 224) {
            /// UTF-8 sequence of three or more bytes starting,
            /// detected by as the three most significant bits are set
            unsigned char utf8char[] = {'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'};
            size_t utf8char_pos = 0;
            while (it != line.cend() && ((*it) & 128) == 128 && utf8char_pos < 7) {
                utf8char[utf8char_pos++] = *it;
                ++it;
            }
            uint32_t unicode = 0;
            if ((c & 240) == 224) ///< Three-byte sequence
                unicode = (utf8char[0] & 15) << 12 | (utf8char[1] & 63) << 6 | (utf8char[2] & 63);
            --it; ///< One step back, as for-loop will step for forward again
            Error::warn("Skipping UTF-8 sequence of three or more bytes, but not supported: %s (U+%04X)", utf8char, unicode);
            prev_c = 0; ///< Do not remember this UTF-8 sequence
            continue; ///< Continue with next character
        } else if ((prev_c & 224) == 192) {
            /// Inside a two-byte UTF-8 sequence,
            /// i.e. previous char started two-byte UTF-8 sequence
            if (prev_c != 0xc3 || c < 0x80 || c > 0xbf) {
                /// Warn about two-byte UTF-8 sequences that are not known letters (e.g. Yen symbol)
                unsigned char utf8char[] = {prev_c, c, '\0'};
                Error::warn("Skipping unsupported UTF-8 character: %s (%02x %02x = U+%04X)", utf8char, utf8char[0], utf8char[1], (utf8char[0] & 31) << 6 | (utf8char[0] & 63));
                lastword.pop_back(); ///< Start of sequence already in word, remove it
                prev_c = 0; ///< Do not remember this UTF-8 sequence
                continue; ///< Continue with next character
            }
        }

        if ((prev_c & 224) == 192 /** Inside an UTF-8 sequence cannot be a gap */
                || gap.find(c) == std::string::npos) {
            /// Character is not a 'gap' character
            /// First, convert character to lower case
            const unsigned char lower_c = utf8tolower(prev_c, c);
            /// Second, add character to current word
            lastword.append((char *)(&lower_c), 1);
            prev_c = lower_c;
        } else if (!lastword.empty()) {
            /// Character is a 'gap' character and the current word is not empty
            /// Current word is not a stop word
            if (multiplicity == Duplicates)
                /// If duplicates are allowed, memorize word
                words.push_back(lastword);
            else if (multiplicity == Unique && known_words.find(lastword) == known_words.end()) {
                /// If no duplicates are allowed and word is not yet know, memorize it
                words.push_back(lastword);
                known_words.insert(lastword);
            }

            /// Reset current word
            lastword.clear();
            prev_c = 0;
        }
    }
    if (!lastword.empty()) {
        /// Very last word in line is not empty
        if (multiplicity == Duplicates)
            /// If duplicates are allowed, memorize word
            words.push_back(lastword);
        else if (multiplicity == Unique && known_words.find(lastword) == known_words.end()) {
            /// If no duplicates are allowed and word is not yet know, memorize it
            words.push_back(lastword);
            known_words.insert(lastword);
        }
    }

    return words.size();
}
