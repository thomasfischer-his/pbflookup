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

#ifndef SWEDISHTEXTTREE_H
#define SWEDISHTEXTTREE_H

#include <iostream>
#include <vector>

#include <google/protobuf/stubs/common.h>

#include "types.h"

struct SwedishTextNode;

class SwedishTextTree {
public:
    enum Warnings {NoWarnings = 0, WarningWordNotInTree = 1, WarningsAll = 0x0fffffff};
    explicit SwedishTextTree();
    explicit SwedishTextTree(std::istream &input);
    ~SwedishTextTree();

    bool insert(const std::string &input, const OSMElement &element);
    std::vector<OSMElement> retrieve(const char *word, Warnings warnings = WarningsAll);

    size_t size();

    std::ostream &write(std::ostream &output);

    static const size_t num_codes;
    static const unsigned int default_num_indices;

private:
    typedef std::vector<unsigned int> code_word;
    static const int code_word_sep;
    static const int code_unknown;

    SwedishTextNode *root;
    size_t _size;

    bool internal_insert(const char *word, const OSMElement &element);
    size_t compute_size(const SwedishTextNode *cur) const;

    code_word to_code_word(const char *word) const;
    unsigned int code_char(const unsigned char &prev_c, const unsigned char &c) const;
};

struct SwedishTextNode {
    explicit SwedishTextNode();
    explicit SwedishTextNode(std::istream &input);
    ~SwedishTextNode();

    std::ostream &write(std::ostream &output);

    SwedishTextNode **children;
    std::vector<OSMElement> elements;
};

#endif // SWEDISHTEXTTREE_H
