#include "swedishtexttree.h"

#include "error.h"

namespace SwedishText {

const int Tree::num_codes = 48;
const unsigned int Tree::default_num_indices = 8;
const int Tree::code_word_sep = Tree::num_codes - 2;
const int Tree::code_unknown = Tree::num_codes - 1;

Node::~Node() {
    if (children != NULL) {
        for (int i = 0; i < Tree::num_codes; ++i)
            if (children[i] != NULL) delete children[i];
        free(children);
    }
    if (ids != NULL) free(ids);
}

Tree::Tree() {
    root = new Node();
    _size = 0;
}

Tree::~Tree() {
    delete root;
}

bool Tree::insert(const std::string &input, uint64_t id) {
    bool result = true;
    std::vector<std::string> words;
    const int num_components = separate_words(input, words);
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
                result &= internal_insert(buffer, id);
            }
        }

        return result;
    } else
        return false;
}

bool Tree::internal_insert(const char *word, uint64_t id) {
    ++_size;
    std::vector<unsigned int> code = code_word(word);
    if (code.empty())
        return false;

    Node *cur = root;
    unsigned int pos = 0;
    while (pos < code.size()) {
        const unsigned int nc = code[pos];
        if (cur->children == NULL) {
            cur->children = (Node **)calloc(num_codes, sizeof(Node *));
            if (cur->children == NULL) {
                Error::err("Could not allocate memory for cur->children");
                return false;
            }
        }
        Node *next = cur->children[nc];
        if (next == NULL) {
            next = cur->children[nc] = new Node();
            if (next == NULL) {
                Error::err("Could not allocate memory for next Node");
                return false;
            }
        }
        ++pos;
        cur = next;
    }

    if (cur->ids == NULL) {
        cur->ids = (uint64_t *)calloc(default_num_indices, sizeof(uint64_t));
        if (cur->ids == NULL) {
            Error::err("Could not allocate memory for cur->ids");
            return false;
        }
        cur->ids[0] = default_num_indices;
    }
    unsigned int idx = 1;
    while (idx < cur->ids[0] && cur->ids[idx] != 0) ++idx;
    if (idx >= cur->ids[0]) {
        const uint64_t new_size = cur->ids[0] + default_num_indices;
        uint64_t *new_array = (uint64_t *)calloc(new_size, sizeof(uint64_t));
        if (new_array == NULL) {
            Error::err("Could not allocate memory for new_array");
            return false;
        }
        new_array[0] = new_size;
        memcpy(new_array + 1, cur->ids + 1, (cur->ids[0] - 1)*sizeof(uint64_t));
        free(cur->ids);
        cur->ids = new_array;
    }
    cur->ids[idx] = id;

    return true;
}

std::vector<uint64_t> Tree::retrieve_ids(const char *word) {
    std::vector<unsigned int> code = code_word(word);
    std::vector<uint64_t> result;

    Node *cur = root;
    unsigned int pos = 0;
    while (pos < code.size()) {
        Node *next = cur->children[code[pos]];
        if (next == NULL)
            return result;
        ++pos;
        cur = next;
    }

    if (cur == NULL)
        return result;
    if (cur->ids == NULL)
        return result;

    for (unsigned int idx = 1; idx < cur->ids[0] && cur->ids[idx] != 0; ++idx) {
        result.push_back(cur->ids[idx]);
    }

    return result;
}

size_t Tree::size() const {
    return _size;
}

int Tree::separate_words(const std::string &input, std::vector<std::string> &words) const {
    std::string lastword;
    static const std::string gap(" ?!\"'#%*&()=,;._\n\r\t");

    words.clear();

    unsigned char prev_c = 0;
    for (std::string::const_iterator it = input.begin(); it != input.end(); ++it) {
        if (gap.find(*it) == std::string::npos) {
            unsigned char c = utf8tolower(prev_c, *it);
            lastword.append((char *)(&c), 1);
            prev_c = c;
        } else if (!lastword.empty()) {
            words.push_back(lastword);
            lastword.clear();
            prev_c = 0;
        }
    }
    if (!lastword.empty()) {
        words.push_back(lastword);
        lastword.clear();
    }

    return words.size();
}

unsigned char Tree::utf8tolower(const unsigned char &prev_c, unsigned char c) const {
    if ((c >= 'A' && c <= 'Z') ||
            (prev_c == 0xc3 && c >= 0x80 && c <= 0x9e /** poor man's Latin-1 Supplement lower case */))
        c |= 0x20;
    return c;
}

std::vector<unsigned int> Tree::code_word(const char *word)  const {
    std::vector<unsigned int> result;

    const unsigned int len = strlen(word);
    unsigned char prev_c = 0;
    for (unsigned int i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)word[i];
        if (c < 0x20) /// break at newline or similar
            break;
        if (c == 0xc3) {
            prev_c = c;
            continue;
        }

        c = utf8tolower(prev_c, c);
        result.push_back(code_char(prev_c, c));

        prev_c = c;
    }

    return result;
}

unsigned int Tree::code_char(const unsigned char &prev_c, const unsigned char &c) const {
    if (c == 0)
        return 0;
    else if (c >= 'a' && c <= 'z')
        return c - 'a' + 1; // 1..26
    else if (c >= '0' && c <= '9')
        return c - '0' + 27; // 27..36
    else if (prev_c == 0xc3) {
        switch (c) {
        case 0xa5: /// a-ring
            return 37;
        case 0xa4: // a-uml
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

} // namespace SwedishText

