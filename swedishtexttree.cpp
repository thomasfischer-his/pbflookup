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

const size_t SwedishTextTree::num_codes = 48;
const unsigned int SwedishTextTree::default_num_indices = 8;
const int SwedishTextTree::code_word_sep = SwedishTextTree::num_codes - 2;
const int SwedishTextTree::code_unknown = SwedishTextTree::num_codes - 1;

SwedishTextNode::SwedishTextNode() {
    children = nullptr;
}

SwedishTextNode::SwedishTextNode(std::istream &input) {
    char chr;

    input.read((char *)&chr, sizeof(chr));
    if (chr == 'N') {
        children = nullptr;
    } else if (chr == 'C') {
        const size_t bytes = SwedishTextTree::num_codes * sizeof(SwedishTextNode *);
        children = (SwedishTextNode **)malloc(bytes);

        for (size_t i = 0; i < SwedishTextTree::num_codes; ++i) {
            input.read((char *)&chr, sizeof(chr));
            if (chr == '0') {
                /// No child at this position
                children[i] = nullptr;
            } else if (chr == '1') {
                children[i] = new SwedishTextNode(input);
            } else
                Error::err("SwedishTextNode: Expected '0' or '1', got '0x%02x' at position %d", chr, input.tellg());
        }
    } else
        Error::err("SwedishTextNode: Expected 'N' or 'C', got '0x%02x' at position %d", chr, input.tellg());

    input.read((char *)&chr, sizeof(chr));
    elements.clear();
    if (chr == 'n') {
        /// No elements to process
    } else if (chr == 'i') {
        size_t count = 0;
        input.read((char *)&count, sizeof(count));
        for (size_t i = 0; i < count; ++i) {
            OSMElement element;
            input.read((char *)&element, sizeof(OSMElement));
            elements.push_back(element);
        }
    } else
        Error::err("SwedishTextNode: Expected 'n' or 'i', got '0x%02x' at position %d", chr, input.tellg());
}

SwedishTextNode::~SwedishTextNode() {
    if (children != nullptr) {
        for (size_t i = 0; i < SwedishTextTree::num_codes; ++i)
            if (children[i] != nullptr) delete children[i];
        free(children);
    }
}

std::ostream &SwedishTextNode::write(std::ostream &output) {
    char chr = '\0';
    if (children == nullptr) {
        chr = 'N';
        output.write((char *)&chr, sizeof(chr));
    } else {
        chr = 'C';
        output.write((char *)&chr, sizeof(chr));
        for (size_t i = 0; i < SwedishTextTree::num_codes; ++i) {
            if (children[i] == nullptr) {
                chr = '0';
                output.write((char *)&chr, sizeof(chr));
            } else {
                chr = '1';
                output.write((char *)&chr, sizeof(chr));
                children[i]->write(output);
            }
        }
    }

    if (elements.empty()) {
        chr = 'n';
        output.write((char *)&chr, sizeof(chr));
    } else {
        chr = 'i';
        output.write((char *)&chr, sizeof(chr));
        const size_t count = elements.size();
        output.write((char *)&count, sizeof(count));
        for (size_t i = 0; i < count; ++i) {
            const OSMElement &element = elements[i];
            output.write((char *)&element, sizeof(OSMElement));
        }
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
    Error::debug("SwedishTextTree had %d elements", size());
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
                    strncpy(cur, cstr, buffer_len - (cur - buffer) - 2);
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
    code_word code = to_code_word(word);
    if (code.empty())
        return false;

    SwedishTextNode *cur = root;
    unsigned int pos = 0;
    while (pos < code.size()) {
        const unsigned int nc = code[pos];
        if (cur->children == nullptr) {
            cur->children = (SwedishTextNode **)calloc(num_codes, sizeof(SwedishTextNode *));
            if (cur->children == nullptr) {
                Error::err("Could not allocate memory for cur->children");
                return false;
            }
        }
        SwedishTextNode *next = cur->children[nc];
        if (next == nullptr) {
            next = cur->children[nc] = new SwedishTextNode();
            if (next == nullptr) {
                Error::err("Could not allocate memory for next Node");
                return false;
            }
        }
        ++pos;
        cur = next;
    }

    cur->elements.push_back(element);
    ++_size;

    return true;
}

std::vector<OSMElement> SwedishTextTree::retrieve(const char *word, Warnings warnings) {
    code_word code = to_code_word(word);
    std::vector<OSMElement> result;

    SwedishTextNode *cur = root;
    unsigned int pos = 0;
    while (pos < code.size()) {
        if (cur->children == nullptr) {
#ifdef DEBUG
            if (warnings & WarningWordNotInTree)
                Error::debug("SwedishTextTree node has no children to follow for word %s at position %d", word, pos);
#endif // DEBUG
            return result; ///< empty
        }
        SwedishTextNode *next = cur->children[code[pos]];
        if (next == nullptr) {
#ifdef DEBUG
            if (warnings & WarningWordNotInTree)
                Error::debug("SwedishTextTree node has no children to follow for word %s at position %d for code %d", word, pos, code[pos]);
#endif // DEBUG
            return result; ///< empty
        }
        ++pos;
        cur = next;
    }

    if (cur == nullptr)
        return result; ///< empty
    if (cur->elements.empty()) {
#ifdef DEBUG
        if (warnings & WarningWordNotInTree)
            Error::debug("SwedishTextTree did not find valid leaf for word %s", word);
#endif // DEBUG
        return result; ///< empty
    }

    std::copy(cur->elements.cbegin(), cur->elements.cend(), std::back_inserter(result));

    return result;
}

size_t SwedishTextTree::compute_size(const SwedishTextNode *cur) const {
    size_t result = 0;

    if (cur->children != nullptr)
        for (size_t i = 0; i < num_codes; ++i)
            if (cur->children[i] != nullptr)
                result += compute_size(cur->children[i]);

    result += cur->elements.size();

    return result;
}

size_t SwedishTextTree::size() {
    if (_size == 0)
        /// SwedishTextTree was loaded from file and size never computer, so do it now
        _size = compute_size(root);
    return _size;
}

SwedishTextTree::code_word SwedishTextTree::to_code_word(const char *input)  const {
    SwedishTextTree::code_word result;

    const unsigned int len = strlen(input);
    unsigned char prev_c = 0;
    for (unsigned int i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)input[i];
        if (c < 0x20) {
            /// break at newline or similar
            Error::warn("Control character unexpected when mapping text to code word");
            break;
        } else if (c == 0xc3) {
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
