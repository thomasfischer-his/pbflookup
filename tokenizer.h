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

    int read_words(const std::string &text, std::vector<std::string> &words, Multiplicity multiplicity);
    int read_words(std::istream &input, std::vector<std::string> &words, Multiplicity multiplicity);

    int generate_word_combinations(const std::vector<std::string> &words, std::vector<std::string> &combinations, const size_t words_per_combination, const Multiplicity multiplicity);

    std::string input_text() const;

private:
    class Private;
    Private *const d;
};


#endif // TOKENIZER_H
