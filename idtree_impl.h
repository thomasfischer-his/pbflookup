#include "error.h"

template <typename T>
struct IdTreeNode {
    static const int bitsPerNode;
    static const int bitsPerId;

    explicit IdTreeNode()
    {
        children = NULL;
        id = 0;
    }

    ~IdTreeNode() {
        if (children != NULL) {
            for (int i = (1 << bitsPerNode) - 1; i >= 0; --i) {
                if (children[i] != NULL)
                    delete children[i];
            }
            free(children);
        }
    }

    IdTreeNode<T> **children;

    uint64_t id;
    T data;
};

template <typename T>
const int IdTreeNode<T>::bitsPerNode = 2;
template <typename T>
const int IdTreeNode<T>::bitsPerId = 48;

template <class T>
IdTree<T>::IdTree()
{
    root = NULL;
}

template <class T>
IdTree<T>::~IdTree()
{
    if (root != NULL)
        delete root;
}

template <class T>
bool IdTree<T>::insert(uint64_t id, T const &data) {
    if (root == NULL)
        root = new IdTreeNode<T>();

    static const unsigned int num_children = 1 << IdTreeNode<T>::bitsPerNode;
    static const unsigned int mask = num_children - 1;
    IdTreeNode<T> *cur = root;
    uint64_t workingId = id;
    for (int s = (IdTreeNode<T>::bitsPerId / IdTreeNode<T>::bitsPerNode) - 1; s >= 0 && workingId > 0; --s) {
        if (cur->children == NULL)
            cur->children = (IdTreeNode<T> **)calloc(num_children, sizeof(IdTreeNode<T> *));

        unsigned int lowerBits = workingId & mask;
        workingId >>= IdTreeNode<T>::bitsPerNode;

        if (cur->children[lowerBits] == NULL)
            cur->children[lowerBits] = new IdTreeNode<T>();
        else if (s == 0)
            Error::err("Leaf already in use: %llu != %llu", id, cur->children[lowerBits]->id);

        cur = cur->children[lowerBits];
    }

    cur->id = id;
    cur->data = data;

    return true;
}

template <class T>
bool IdTree<T>::retrieve(const uint64_t id, T &data) {
    if (root == NULL) {
        Error::warn("Tree root is invalid, no id was ever added");
        return false;
    }

    static const unsigned int num_children = 1 << IdTreeNode<T>::bitsPerNode;
    static const unsigned int mask = num_children - 1;

    IdTreeNode<T> *cur = root;
    uint64_t workingId = id;
    for (int s = (IdTreeNode<T>::bitsPerId / IdTreeNode<T>::bitsPerNode) - 1; s >= 0 && workingId > 0; --s) {
        if (cur->children == NULL) {
            Error::warn("id=%llu   s=%d", id, s);
            return false;
        }

        const unsigned int lowerBits = workingId & mask;
        workingId >>= IdTreeNode<T>::bitsPerNode;

        if (cur->children[lowerBits] == NULL) {
            Error::warn("id=%llu   s=%d lowerBits=%d  workingId=%llu", id, s, lowerBits, workingId);
            return false;
        }

        cur = cur->children[lowerBits];
    }

    if (cur->id != id) {
        Error::warn("Ids do not match: %llu != %llu", cur->id, id);
        return false;
    }

    data = cur->data;

    return true;
}
