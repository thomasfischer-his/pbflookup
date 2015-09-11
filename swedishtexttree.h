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

#ifndef SWEDISHTEXTTREE_H
#define SWEDISHTEXTTREE_H

#include <iostream>
#include <vector>

#include <google/protobuf/stubs/common.h>

#include "types.h"

namespace SwedishText {

struct Node;

class Tree {
public:
    explicit Tree();
    explicit Tree(std::istream &input);
    ~Tree();

    bool insert(const std::string &input, const OSMElement &element);
    std::vector<OSMElement> retrieve(const char *word);

    size_t size() const;

    std::ostream &write(std::ostream &output);

    static const int num_codes;
    static const unsigned int default_num_indices;

private:
    static const int code_word_sep;
    static const int code_unknown;

    Node *root;
    size_t _size;

    bool internal_insert(const char *word, const OSMElement &element);

    int separate_words(const std::string &input, std::vector<std::string> &words) const;
    unsigned char utf8tolower(const unsigned char &prev_c, unsigned char c) const;
    std::vector<unsigned int> code_word(const char *word) const;
    unsigned int code_char(const unsigned char &prev_c, const unsigned char &c) const;
};

struct Node {
    explicit Node();
    explicit Node(std::istream &input);
    ~Node();

    std::ostream &write(std::ostream &output);

    Node **children;
    size_t elements_size;
    OSMElement *elements;
};

} // namespace SwedishText

#endif // SWEDISHTEXTTREE_H
