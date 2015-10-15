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

#include <vector>

template <typename T>
struct IdTreeNode {
    static const int bitsPerNode;
    static const int bitsPerId;
    static const unsigned int numChildren;

    explicit IdTreeNode()
    {
        children = NULL;
#ifdef DEBUG
        id = 0;
#endif // DEBUG
        counter = 0;
    }

    explicit IdTreeNode(std::istream &input) {
        counter = 0;
#ifdef DEBUG
        input.read((char *)&id, sizeof(id));
#endif // DEBUG
        input.read((char *)&counter, sizeof(counter));
        data = T(input);

        char chr;
        input.read((char *)&chr, sizeof(chr));

        if (chr == 'N') {
            children = NULL;
        } else if (chr == 'C') {
            const size_t bytes = numChildren * sizeof(IdTreeNode<T> *);
            children = (IdTreeNode<T> **)malloc(bytes);

            for (int c = numChildren - 1; c >= 0; --c) {
                input.read((char *)&chr, sizeof(chr));
                if (chr == '0') {
                    /// No child at this position
                    children[c] = NULL;
                } else if (chr == '1') {
                    children[c] = new IdTreeNode<T>(input);
                } else
                    Error::warn("Expected '0' or '1', got '0x%02x'", chr);
            }
        } else
            Error::warn("Expected 'N' or 'C', got '0x%02x'", chr);
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

    std::ostream &write(std::ostream &output) {
#ifdef DEBUG
        output.write((char *)&id, sizeof(id));
#endif // DEBUG
        output.write((char *)&counter, sizeof(counter));
        data.write(output);

        char chr = '\0';
        if (children == NULL) {
            chr = 'N';
            output.write((char *)&chr, sizeof(chr));
        } else {
            chr = 'C';
            output.write((char *)&chr, sizeof(chr));
            for (int c = numChildren - 1; c >= 0; --c) {
                if (children[c] == NULL) {
                    chr = '0';
                    output.write((char *)&chr, sizeof(chr));
                } else {
                    chr = '1';
                    output.write((char *)&chr, sizeof(chr));
                    children[c]->write(output);
                }
            }
        }

        return output;
    }

    IdTreeNode<T> **children;

#ifdef DEBUG
    uint64_t id;
#endif // DEBUG
    T data;

    uint16_t counter;
};

template <typename T>
const int IdTreeNode<T>::bitsPerNode = 4;
template <typename T>
const int IdTreeNode<T>::bitsPerId = 64;
template <typename T>
const unsigned int IdTreeNode<T>::numChildren = 1 << 4;

template <class T>
class IdTree<T>::Private
{
private:
    IdTree *p;

public:
    static const unsigned int num_children;
    static const uint64_t mask;

    struct IdTreeNode<T> *root;

    Private(IdTree *parent)
        : p(parent) {
        root = NULL;
    }

    ~Private() {
        if (root != NULL)
            delete root;
    }

    IdTreeNode<T> *findNodeForId(uint64_t id, std::vector<IdTreeNode<T> *> *path = NULL) const {
        if (root == NULL) {
            Error::warn("Tree root is invalid, no id was ever added");
            return NULL;
        }

        IdTreeNode<T> *cur = root;
        uint64_t workingId = id;
        for (int s = (IdTreeNode<T>::bitsPerId / IdTreeNode<T>::bitsPerNode) - 1; s >= 0 && workingId > 0; --s) {
            if (cur->children == NULL) {
#ifdef DEBUG
                Error::debug("IdTree node has no children to follow id %llu", id);
#endif // DEBUG
                return NULL;
            }

#ifdef REVERSE_ID_TREE
            static const int shiftOffset = 64 - IdTreeNode<T>::bitsPerNode;
            const unsigned int lowerBits = (workingId & (IdTree::Private::mask << shiftOffset)) >> shiftOffset;
            workingId <<= IdTreeNode<T>::bitsPerNode;
#else // REVERSE_ID_TREE
            const unsigned int lowerBits = workingId & IdTree::Private::mask;
            workingId >>= IdTreeNode<T>::bitsPerNode;
#endif // REVERSE_ID_TREE

            if (cur->children[lowerBits] == NULL) {
#ifdef DEBUG
                Error::debug("IdTree node has no children at pos %d to follow id %llu", lowerBits, id);
#endif // DEBUG
                return NULL;
            }

            if (path != NULL) path->push_back(cur);
            cur = cur->children[lowerBits];
        }
        if (path != NULL) path->push_back(cur);

#ifdef DEBUG
        if (cur->id != id) {
            Error::warn("Ids do not match: %llu != %llu", cur->id, id);
            return NULL;
        }
#endif // DEBUG

        return cur;
    }

};

template <typename T>
const unsigned int IdTree<T>::Private::num_children = 1 << IdTreeNode<T>::bitsPerNode;
template <typename T>
const uint64_t IdTree<T>::Private::mask = (1 << IdTreeNode<T>::bitsPerNode) - 1;

template <class T>
IdTree<T>::IdTree()
    : d(new IdTree<T>::Private(this))
{
    if (d == NULL)
        Error::err("Could not allocate memory for IdTree<T>::Private");
#ifdef REVERSE_ID_TREE
    Error::debug("Using most significant bits as first sorting critera in IdTree");
#else // REVERSE_ID_TREE
    Error::debug("Using least significant bits as first sorting critera in IdTree");
#endif // REVERSE_ID_TREE
}

template <class T>
IdTree<T>::IdTree(std::istream &input)
    : d(new IdTree<T>::Private(this))
{
    d->root = new IdTreeNode<T>(input);
}

template <class T>
IdTree<T>::~IdTree()
{
    delete d;
}

template <class T>
bool IdTree<T>::insert(uint64_t id, T const &data) {
    if (d->root == NULL) {
        d->root = new IdTreeNode<T>();
        if (d->root == NULL)
            Error::err("Could not allocate memory for IdTree::root");
    }

    IdTreeNode<T> *cur = d->root;
    uint64_t workingId = id;
    for (int s = (IdTreeNode<T>::bitsPerId / IdTreeNode<T>::bitsPerNode) - 1; s >= 0 && workingId > 0; --s) {
        if (cur->children == NULL) {
            cur->children = (IdTreeNode<T> **)calloc(IdTree::Private::num_children, sizeof(IdTreeNode<T> *));
            if (cur->children == NULL)
                Error::err("Could not allocate memory for cur->children");
        }

#ifdef REVERSE_ID_TREE
        static const int shiftOffset = 64 - IdTreeNode<T>::bitsPerNode;
        const unsigned int lowerBits = (workingId & (IdTree::Private::mask << shiftOffset)) >> shiftOffset;
        workingId <<= IdTreeNode<T>::bitsPerNode;
#else // REVERSE_ID_TREE
        const unsigned int lowerBits = workingId & IdTree::Private::mask;
        workingId >>= IdTreeNode<T>::bitsPerNode;
#endif // REVERSE_ID_TREE

        if (cur->children[lowerBits] == NULL) {
            cur->children[lowerBits] = new IdTreeNode<T>();
            if (cur->children[lowerBits] == NULL)
                Error::err("Could not allocate memory for cur->children[lowerBits]");
        }
#ifdef DEBUG
        else if (s == 0)
            Error::err("Leaf already in use: %llu != %llu", id, cur->children[lowerBits]->id);
#endif // DEBUG

        cur = cur->children[lowerBits];
    }

#ifdef DEBUG
    cur->id = id;
#endif // DEBUG
    cur->data = data;

    return true;
}

template <class T>
bool IdTree<T>::retrieve(const uint64_t id, T &data) const {
    IdTreeNode<T> *cur = d->findNodeForId(id);
    if (cur == NULL)
        return false;

    data = cur->data;

    return true;
}

template <class T>
bool IdTree<T>::remove(uint64_t id) {
    std::vector<IdTreeNode<T> *> path;
    IdTreeNode<T> *cur = d->findNodeForId(id, &path);
    if (cur == NULL)
        return false;

    int num_children = 0;
    while (num_children == 0 && path.size() > 1) {
        path.pop_back();
        IdTreeNode<T> *parent = path.back();
        num_children = 0;
        if (parent->children != NULL)
            for (int i = (1 << IdTreeNode<T>::bitsPerNode) - 1; i >= 0; --i) {
                if (cur != NULL && parent->children[i] == cur) {
                    delete cur;
                    cur = NULL;
                    parent->children[i] = NULL;
                }
                if (parent->children[i] != NULL)
                    ++num_children;
            }
        cur = parent;
    }
    if (num_children == 0 && cur == d->root) {
        delete d->root;
        d->root = NULL;
    }

    return true;
}

template <class T>
uint16_t IdTree<T>::counter(const uint64_t id) const {
    IdTreeNode<T> *cur = d->findNodeForId(id);
    if (cur == NULL)
        return 0;

    return cur->counter;
}

template <class T>
void IdTree<T>::increaseCounter(const uint64_t id) {
    IdTreeNode<T> *cur = d->findNodeForId(id);

    if (cur != NULL)
        ++cur->counter;
}

template <class T>
std::ostream &IdTree<T>::write(std::ostream &output) {
    if (d->root == NULL)
        return output;
    return d->root->write(output);
}
