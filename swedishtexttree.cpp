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

#include "swedishtexttree.h"

#include "tokenizer.h"
#include "error.h"
#include "helper.h"

const int SwedishTextTree::num_codes = 48;
const unsigned int SwedishTextTree::default_num_indices = 8;
const int SwedishTextTree::code_word_sep = SwedishTextTree::num_codes - 2;
const int SwedishTextTree::code_unknown = SwedishTextTree::num_codes - 1;

SwedishTextNode::SwedishTextNode() {
    children = NULL;
    elements_size = 0;
    elements = NULL;
}

SwedishTextNode::SwedishTextNode(std::istream &input) {
    char chr;

    input.read((char *)&chr, sizeof(chr));
    if (chr == 'N') {
        children = NULL;
    } else if (chr == 'C') {
        const size_t bytes = SwedishTextTree::num_codes * sizeof(SwedishTextNode *);
        children = (SwedishTextNode **)malloc(bytes);

        for (int i = 0; i < SwedishTextTree::num_codes; ++i) {
            input.read((char *)&chr, sizeof(chr));
            if (chr == '0') {
                /// No child at this position
                children[i] = NULL;
            } else if (chr == '1') {
                children[i] = new SwedishTextNode(input);
            } else
                Error::err("Expected '0' or '1', got '0x%02x'", chr);
        }
    } else
        Error::err("Expected 'N' or 'C', got '0x%02x'", chr);

    input.read((char *)&chr, sizeof(chr));
    if (chr == 'n') {
        elements_size = 0;
        elements = NULL;
    } else if (chr == 'i') {
        input.read((char *)&elements_size, sizeof(elements_size));
        const size_t bytes = elements_size * sizeof(OSMElement);
        elements = (OSMElement *)malloc(bytes);
        input.read((char *)elements, bytes);
    } else
        Error::err("Expected 'n' or 'i', got '0x%02x'", chr);
}

SwedishTextNode::~SwedishTextNode() {
    if (children != NULL) {
        for (int i = 0; i < SwedishTextTree::num_codes; ++i)
            if (children[i] != NULL) delete children[i];
        free(children);
    }
    if (elements != NULL) free(elements);
}

std::ostream &SwedishTextNode::write(std::ostream &output) {
    char chr = '\0';
    if (children == NULL) {
        chr = 'N';
        output.write((char *)&chr, sizeof(chr));
    } else {
        chr = 'C';
        output.write((char *)&chr, sizeof(chr));
        for (int i = 0; i < SwedishTextTree::num_codes; ++i) {
            if (children[i] == NULL) {
                chr = '0';
                output.write((char *)&chr, sizeof(chr));
            } else {
                chr = '1';
                output.write((char *)&chr, sizeof(chr));
                children[i]->write(output);
            }
        }
    }

    if (elements == NULL) {
        chr = 'n';
        output.write((char *)&chr, sizeof(chr));
    } else {
        chr = 'i';
        output.write((char *)&chr, sizeof(chr));
        output.write((char *)&elements_size, sizeof(elements_size));
        output.write((char *)elements, elements_size * sizeof(OSMElement));
    }

    return output;
}


SwedishTextTree::SwedishTextTree() {
    root = new SwedishTextNode();
    _size = 0;
}

SwedishTextTree::SwedishTextTree(std::istream &input) {
    root = new SwedishTextNode(input);
    _size = 0;
}

SwedishTextTree::~SwedishTextTree() {
    delete root;
}

std::ostream &SwedishTextTree::write(std::ostream &output) {
    return root->write(output);
}

bool SwedishTextTree::insert(const std::string &input, const OSMElement &element) {
    bool result = true;
    std::vector<std::string> words;
    bool warnings = false;
    const int num_components = Tokenizer::tokenize_input(input, words, Tokenizer::Duplicates, &warnings);
    if (warnings)
        Error::warn("Got tokenizer warnings for OSM Element %s", element.operator std::string().c_str());
    if (num_components > 0) {
        static const int buffer_len = 1024;
        char buffer[buffer_len];
        for (int s = num_components; result && s > num_components - 3 && s > 0; --s) {
            for (int start = 0; result && start <= num_components - s; ++start) {
                char *cur = buffer;
                for (int i = 0; i < s; ++i) {
                    const char *cstr = words[start + i].c_str();
                    strncpy(cur, cstr, buffer_len - (cur - buffer));
                    cur += strlen(cstr);
                    if (i + 1 < s) {
                        /// space in between
                        *cur = 0x20;
                        ++cur;
                    }
                }
                result &= internal_insert(buffer, element);
            }
        }

        return result;
    } else
        return false;
}

bool SwedishTextTree::internal_insert(const char *word, const OSMElement &element) {
    ++_size;
    std::vector<unsigned int> code = code_word(word);
    if (code.empty())
        return false;

    SwedishTextNode *cur = root;
    unsigned int pos = 0;
    while (pos < code.size()) {
        const unsigned int nc = code[pos];
        if (cur->children == NULL) {
            cur->children = (SwedishTextNode **)calloc(num_codes, sizeof(SwedishTextNode *));
            if (cur->children == NULL) {
                Error::err("Could not allocate memory for cur->children");
                return false;
            }
        }
        SwedishTextNode *next = cur->children[nc];
        if (next == NULL) {
            next = cur->children[nc] = new SwedishTextNode();
            if (next == NULL) {
                Error::err("Could not allocate memory for next Node");
                return false;
            }
        }
        ++pos;
        cur = next;
    }

    if (cur->elements == NULL) {
        cur->elements = (OSMElement *)calloc(default_num_indices, sizeof(OSMElement));
        if (cur->elements == NULL) {
            Error::err("Could not allocate memory for cur->elements");
            return false;
        }
        cur->elements_size = default_num_indices;
    }
    unsigned int idx = 1;
    while (idx < cur->elements_size && cur->elements[idx].id != 0) ++idx;
    if (idx >= cur->elements_size) {
        const size_t new_size = cur->elements_size + default_num_indices;
        OSMElement *new_array = (OSMElement *)calloc(new_size, sizeof(OSMElement));
        if (new_array == NULL) {
            Error::err("Could not allocate memory for new_array");
            return false;
        }
        memcpy(new_array, cur->elements, (cur->elements_size)*sizeof(OSMElement));
        free(cur->elements);
        cur->elements = new_array;
        cur->elements_size = new_size;
    }
    cur->elements[idx] = element;

    return true;
}

std::vector<OSMElement> SwedishTextTree::retrieve(const char *word, Warnings warnings) {
    std::vector<unsigned int> code = code_word(word);
    std::vector<OSMElement> result;

    SwedishTextNode *cur = root;
    unsigned int pos = 0;
    while (pos < code.size()) {
        if (cur->children == NULL) {
#ifdef DEBUG
            if (warnings & WarningWordNotInTree)
                Error::debug("SwedishTextTree node has no children to follow for word %s at position %d", word, pos);
#endif // DEBUG
            return result; ///< empty
        }
        SwedishTextNode *next = cur->children[code[pos]];
        if (next == NULL) {
#ifdef DEBUG
            if (warnings & WarningWordNotInTree)
                Error::debug("SwedishTextTree node has no children to follow for word %s at position %d for code %d", word, pos, code[pos]);
#endif // DEBUG
            return result; ///< empty
        }
        ++pos;
        cur = next;
    }

    if (cur == NULL)
        return result; ///< empty
    if (cur->elements_size == 0 || cur->elements == NULL) {
#ifdef DEBUG
        if (warnings & WarningWordNotInTree)
            Error::debug("SwedishTextTree did not find valid leaf for word %s", word);
#endif // DEBUG
        return result; ///< empty
    }

    for (unsigned int idx = 1; idx < cur->elements_size && cur->elements[idx].id != 0; ++idx) {
        result.push_back(cur->elements[idx]);
    }

    return result;
}

size_t SwedishTextTree::size() const {
    return _size;
}

std::vector<unsigned int> SwedishTextTree::code_word(const char *word)  const {
    std::vector<unsigned int> result;

    const unsigned int len = strlen(word);
    unsigned char prev_c = 0;
    for (unsigned int i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)word[i];
        if (c < 0x20) /// break at newline or similar
            break;
        if (c == 0xc3) {
            prev_c = c;
            continue;
        }

        result.push_back(code_char(prev_c, c));

        prev_c = c;
    }

    return result;
}

unsigned int SwedishTextTree::code_char(const unsigned char &prev_c, const unsigned char &c) const {
    if (c == 0)
        return 0;
    else if (c >= 'a' && c <= 'z')
        return c - 'a' + 1; /// 1..26
    else if (c >= '0' && c <= '9')
        return c - '0' + 27; /// 27..36
    else if (prev_c == 0xc3) {
        switch (c) {
        case 0xa5: /// a-ring
            return 37;
        case 0xa4: /// a-uml
            return 38;
        case 0xb6: /// o-uml
            return 39;
        case 0xa9: /// e-acute
            return 40;
        case 0xbc: /// u-uml
            return 41;
        case 0xb8: /// o-stroke
            return 42;
        default:
            return code_unknown;
        }
    } else if (c <= 0x20) /// space
        return code_word_sep;
    else if (c < 0x7f) {
        switch (c) {
        case 0x2d: /// hyphen-minus
            return 45;
        default:
            return code_unknown;
        }
    } else
        return code_unknown;
}
