#ifndef SWEDISHTEXTTREE_H
#define SWEDISHTEXTTREE_H

#define NODE_NIBBLE 1
#define WAY_NIBBLE 2
#define RELATION_NIBBLE 3

#include <iostream>
#include <vector>

#include <google/protobuf/stubs/common.h>

namespace SwedishText {

struct Node;

class Tree {
public:
    explicit Tree();
    explicit Tree(std::istream &input);
    ~Tree();

    bool insert(const std::string &input, uint64_t id);
    std::vector<uint64_t> retrieve_ids(const char *word);

    size_t size() const;

    std::ostream &write(std::ostream &output);

    static const int num_codes;
    static const unsigned int default_num_indices;

private:
    static const int code_word_sep;
    static const int code_unknown;

    Node *root;
    size_t _size;

    bool internal_insert(const char *word, uint64_t id);

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
    uint64_t *ids;
};

} // namespace SwedishText

#endif // SWEDISHTEXTTREE_H
