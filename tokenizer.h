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

#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <string>
#include <vector>

class Tokenizer
{
public:
    enum Multiplicity { Unique = 0, Duplicates = 1 };

    explicit Tokenizer();
    ~Tokenizer();

    std::vector<std::string> read_words(const std::string &text, Multiplicity multiplicity);
    std::vector<std::string> read_words(std::istream &input, Multiplicity multiplicity);

    std::vector<std::string> generate_word_combinations(const std::vector<std::string> &words, const size_t max_words_per_combination, const size_t min_words_per_combination = 1) const;

    std::string input_text() const;

    /**
     * For a given input text, extract individual words in this input and
     * add found words into the words vector. Using the multiplicity parameter,
     * it can be controlled if duplicates may get added to the words vector
     * or if each word may be added only once even if its occurrs multiple
     * times in the input text.
     * As the input text may contain unsupported UTF-8 sequences, the optional
     * warnings parameter may be used to track if such warnings occurred.
     *
     * The words vector pass as reference parameter will be neither cleared,
     * nor will strings already in the vector be removed or modified.
     *
     * @param input Input text to process
     * @param words List where found words will be added to.
     * @param multiplicity Controls how duplicate words in input will be handled
     * @param warnings Optional parameter to learn if unsupported UTF-8 sequences occurred
     * @return Number of words added to the words vector
     */
    static size_t tokenize_input(const std::string &input, std::vector<std::string> &words, Multiplicity multiplicity, bool *warnings = nullptr);

private:
    class Private;
    Private *const d;
};


#endif // TOKENIZER_H
