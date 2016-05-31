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

#include <vector>
#include <typeinfo>

template <typename T>
struct IdTreeNode {
    static const int bitsPerNode;
    static const int bitsPerId;
    static const unsigned int numChildren;

    explicit IdTreeNode()
    {
        children = nullptr;
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

        char chr;
        input.read((char *)&chr, sizeof(chr));

        if (chr == 'N') {
            /// Is leaf node
            children = nullptr;
            input.read((char *)&counter, sizeof(counter));
            data = T(input);
        } else if (chr == 'C') {
            /// Is inner node
            const size_t bytes = numChildren * sizeof(IdTreeNode<T> *);
            children = (IdTreeNode<T> **)malloc(bytes);

            for (int c = numChildren - 1; c >= 0; --c) {
                input.read((char *)&chr, sizeof(chr));
                if (chr == '0') {
                    /// No child at this position
                    children[c] = nullptr;
                } else if (chr == '1') {
                    children[c] = new IdTreeNode<T>(input);
                } else
                    Error::err("IdTree<%s>: Expected '0' or '1', got '0x%02x' at position %d", typeid(T).name(), chr, input.tellg());
            }
        } else
            Error::err("IdTree<%s>: Expected 'N' or 'C', got '0x%02x' at position %d", typeid(T).name(), chr, input.tellg());
    }

    ~IdTreeNode() {
        if (children != nullptr) {
            for (int i = (1 << bitsPerNode) - 1; i >= 0; --i) {
                if (children[i] != nullptr)
                    delete children[i];
            }
            free(children);
        }
    }

    std::ostream &write(std::ostream &output) {
#ifdef DEBUG
        output.write((char *)&id, sizeof(id));
#endif // DEBUG

        char chr = '\0';
        if (children == nullptr) {
            /// Is leaf node
            chr = 'N';
            output.write((char *)&chr, sizeof(chr));
            output.write((char *)&counter, sizeof(counter));
            data.write(output);
        } else {
            /// Is inner node
            chr = 'C';
            output.write((char *)&chr, sizeof(chr));
            for (int c = numChildren - 1; c >= 0; --c) {
                if (children[c] == nullptr) {
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
const unsigned int IdTreeNode<T>::numChildren = 1 << IdTreeNode<T>::bitsPerNode;

template <class T>
class IdTree<T>::Private
{
private:
    IdTree *p;
#ifdef REVERSE_ID_TREE
    std::vector<IdTreeNode<T> *> zeroPath;
    static const size_t zeroPathDepth = 8;
#endif // REVERSE_ID_TREE

public:
    static const unsigned int num_children;
    static const uint64_t mask;
    size_t size;

    struct IdTreeNode<T> *root;

    /**
     * Simple cache to speed up retrieval of data from this tree.
     * The cache has cache_size many entries, where each entry is
     * a pair of id and data. The lowest log2(cache_size) many bits
     * determine the line in this cache.
     * Upon lookup, the cache row is uniquely determined by the id's
     * lowest bits. If the id in the cache line is identical to the
     * request id, it is a cache hit, otherwise a cache miss.
     */
    struct CacheLine {
        uint64_t id;
        T data;
    };
    static const size_t cache_size = 1024;
    CacheLine cache[cache_size];
    size_t cache_hit_counter, cache_miss_counter;

    Private(IdTree *parent)
        : p(parent), size(0), root(nullptr) {
        /// Invalidate all cache lines by setting 'id' to an invalid value (=0)
        for (size_t i = 0; i < cache_size; ++i)
            cache[i].id = 0;
        /// Initialize cache hit/miss counters
        cache_hit_counter = cache_miss_counter = 0;
    }

    ~Private() {
        Error::info("IdTree<%s>:  cache_hit= %d (%.1f%%)  cache_miss= %d", typeid(T).name(), cache_hit_counter, 100.0 * cache_hit_counter / (cache_hit_counter + cache_miss_counter), cache_miss_counter);
        if (root != nullptr)
            delete root;
    }

#ifdef REVERSE_ID_TREE
    /**
     * It is an observation that out of the 64-bit space for ids, most used ids are small,
     * i.e. have their most significant bits set to zero. This results in that the id tree
     * will be very unbalanced, as for the first levels the 0th child will be used.
     * To avoid costly traversals of a tree that is unbalanced, here the id is checked if the
     * bits used for the up to eight first traversal steps are all set to zero. If true,
     * a pre-recorded path ('zeroPath') will be used that guides the search function faster
     * through the tree than a 'regular' tree traversal would do.
     * Initial tests showed a 25% decrease in CPU time if using this function.
     * @param id
     * @param s
     * @param path
     * @return
     */
    IdTreeNode<T> *skipAheadOnZeroPath(uint64_t &id, unsigned int &s, std::vector<IdTreeNode<T> *> *path = nullptr) {
#define mask_for_level(n) (IdTree::Private::mask<<((sizeof(id) * 8 /** size in Bytes to size in Bits */) - ((n)+1) * IdTreeNode<T>::bitsPerNode))
        static const uint64_t masks[zeroPathDepth] = {
            mask_for_level(0),
            mask_for_level(0) | mask_for_level(1),
            mask_for_level(0) | mask_for_level(1) | mask_for_level(2),
            mask_for_level(0) | mask_for_level(1) | mask_for_level(2) | mask_for_level(3),
            mask_for_level(0) | mask_for_level(1) | mask_for_level(2) | mask_for_level(3) | mask_for_level(4),
            mask_for_level(0) | mask_for_level(1) | mask_for_level(2) | mask_for_level(3) | mask_for_level(4) | mask_for_level(5),
            mask_for_level(0) | mask_for_level(1) | mask_for_level(2) | mask_for_level(3) | mask_for_level(4) | mask_for_level(5) | mask_for_level(6),
            mask_for_level(0) | mask_for_level(1) | mask_for_level(2) | mask_for_level(3) | mask_for_level(4) | mask_for_level(5) | mask_for_level(6) | mask_for_level(7)
        };

        for (s = zeroPathDepth; s > 0; --s) {
            if ((id & masks[s - 1]) == 0 && zeroPath.size() > s) {
                /// id would pick child 0 for the first s-many levels when traversing tree
                id <<= IdTreeNode<T>::bitsPerNode * s;
                if (path != nullptr)
                    for (unsigned int k = 0; k < s; ++k) path->push_back(zeroPath[k]);
                return zeroPath[s];
            }
        }

        return root;
    }
#endif // REVERSE_ID_TREE

#ifdef REVERSE_ID_TREE
    void buildZeroPath() {
        if (root == nullptr)
            root = new IdTreeNode<T>();

        IdTreeNode<T> *cur = root;
        for (size_t s = 0; s < zeroPathDepth; ++s) {
            zeroPath.push_back(cur);
            if (s >= zeroPathDepth - 1) break; /// no need to build children for last node
            if (cur->children == nullptr) {
                cur->children = (IdTreeNode<T> **)calloc(IdTreeNode<T>::numChildren, sizeof(IdTreeNode<T> *));
                if (cur->children == nullptr)
                    Error::err("IdTree<%s>: Could not allocate memory for cur->children", typeid(T).name());
            }
            if (cur->children[0] == nullptr)
                cur->children[0] = new IdTreeNode<T>();
            cur = cur->children[0];
        }
    }
#endif // REVERSE_ID_TREE

    /**
     * This is the single most expensive function, taking 25-35% of the CPU time.
     */
    IdTreeNode<T> *findNodeForId(uint64_t id, std::vector<IdTreeNode<T> *> *path = nullptr) {
        if (root == nullptr) {
            Error::warn("IdTree<%s> root is invalid, no id was ever added", typeid(T).name());
            return nullptr;
        }

        unsigned int s = 0;
        uint64_t workingId = id;
#ifdef REVERSE_ID_TREE
        IdTreeNode<T> *cur = skipAheadOnZeroPath(workingId, s, path);
#else // REVERSE_ID_TREE
        IdTreeNode<T> *cur = root;
#endif // REVERSE_ID_TREE
        for (unsigned int s_limit = (IdTreeNode<T>::bitsPerId / IdTreeNode<T>::bitsPerNode); s < s_limit; ++s) {
            if (cur->children == nullptr) {
#ifdef DEBUG
                Error::debug("IdTree<%s> node has no children to follow id %llu", typeid(T).name(), id);
#endif // DEBUG
                return nullptr;
            }

#ifdef REVERSE_ID_TREE
            /// For 64-bit-wide ids and 4 bits per node, shiftOffset is 60
            static const int shiftOffset = (sizeof(workingId) * 8 /** size in Bytes to size in Bits */) - IdTreeNode<T>::bitsPerNode;
            /// For 4 bits per node, mask is (2^4)-1 = 15 (i.e. four bits set)
            /// Extracts the most-significant bitsPerNode-many bits from workingId,
            /// then shifts them to be in range 0 .. (2^bitsPerNode)-1
            const unsigned int bits = (workingId & (IdTree::Private::mask << shiftOffset)) >> shiftOffset;
            /// Shift out to the left bits that were just extracted
            workingId <<= IdTreeNode<T>::bitsPerNode;
#else // REVERSE_ID_TREE
            /// Extracts the least-significant bitsPerNode-many bits from workingId
            /// Result will be in range 0 .. (2^bitsPerNode)-1
            const unsigned int bits = workingId & IdTree::Private::mask;
            /// Shift out to the right bits that were just extracted
            workingId >>= IdTreeNode<T>::bitsPerNode;
#endif // REVERSE_ID_TREE

            if (cur->children[bits] == nullptr) {
#ifdef DEBUG
                Error::debug("IdTree<%s> node has no children at pos %d to follow id %llu", typeid(T).name(), bits, id);
#endif // DEBUG
                return nullptr;
            }

            if (path != nullptr) path->push_back(cur);
            cur = cur->children[bits];
        }

#ifdef DEBUG
        if (cur->id != id) {
            Error::warn("IdTree<%s>: Ids do not match: %llu != %llu", typeid(T).name(), cur->id, id);
            return nullptr;
        }
#endif // DEBUG

        return cur;
    }

    size_t compute_size(const IdTreeNode<T> *cur, size_t depth = 0) const {
        size_t result = 0;

        if (cur->children != nullptr)
            for (size_t i = 0; i < IdTreeNode<T>::numChildren; ++i)
                if (cur->children[i] != nullptr) {
                    if (depth < 15)
                        result += compute_size(cur->children[i], depth + 1);
                    else ///< depth == 15
                        ++result;
                }

        return result;
    }
};

template <typename T>
const uint64_t IdTree<T>::Private::mask = (1 << IdTreeNode<T>::bitsPerNode) - 1;

template <class T>
IdTree<T>::IdTree()
    : d(new IdTree<T>::Private(this))
{
    if (d == nullptr)
        Error::err("Could not allocate memory for IdTree<T>::Private");
#ifdef REVERSE_ID_TREE
    Error::debug("Using most significant bits as first sorting critera in IdTree<%s>", typeid(T).name());
    d->buildZeroPath();
#else // REVERSE_ID_TREE
    Error::debug("Using least significant bits as first sorting critera in IdTree<%s>", typeid(T).name());
#endif // REVERSE_ID_TREE
}

template <class T>
IdTree<T>::IdTree(std::istream &input)
    : d(new IdTree<T>::Private(this))
{
    size_t s;
    input.read((char *)&s, sizeof(s));
    d->root = new IdTreeNode<T>(input);
    if (s != size())
        Error::err("Recorded size of IdTree<%s> does not match actual size: %d != %d", typeid(T).name(), s, size());
#ifdef REVERSE_ID_TREE
    d->buildZeroPath();
#endif // REVERSE_ID_TREE
}

template <class T>
IdTree<T>::~IdTree()
{
    Error::debug("IdTree<%s> had %d elements", typeid(T).name(), size());
    delete d;
}

template <class T>
bool IdTree<T>::insert(uint64_t id, T const &data) {
    if (id == 0)
        Error::err("Cannot insert element with id=0 into IdTree<%s>", typeid(T).name());

    if (d->root == nullptr) {
        d->root = new IdTreeNode<T>();
        if (d->root == nullptr)
            Error::err("Could not allocate memory for IdTree<%s>::root", typeid(T).name());
    }

    IdTreeNode<T> *cur = d->root;
    uint64_t workingId = id;
    for (int s = (IdTreeNode<T>::bitsPerId / IdTreeNode<T>::bitsPerNode) - 1; s >= 0; --s) {
        if (cur->children == nullptr) {
            cur->children = (IdTreeNode<T> **)calloc(IdTreeNode<T>::numChildren, sizeof(IdTreeNode<T> *));
            if (cur->children == nullptr)
                Error::err("IdTree<%s>: Could not allocate memory for cur->children", typeid(T).name());
        }

#ifdef REVERSE_ID_TREE
        static const int shiftOffset = 64 - IdTreeNode<T>::bitsPerNode;
        const unsigned int lowerBits = (workingId & (IdTree::Private::mask << shiftOffset)) >> shiftOffset;
        workingId <<= IdTreeNode<T>::bitsPerNode;
#else // REVERSE_ID_TREE
        const unsigned int lowerBits = workingId & IdTree::Private::mask;
        workingId >>= IdTreeNode<T>::bitsPerNode;
#endif // REVERSE_ID_TREE

        if (cur->children[lowerBits] == nullptr) {
            cur->children[lowerBits] = new IdTreeNode<T>();
            if (cur->children[lowerBits] == nullptr)
                Error::err("IdTree<%s>: Could not allocate memory for cur->children[lowerBits]", typeid(T).name());
        }
#ifdef DEBUG
        else if (s == 0)
            Error::err("IdTree<%s>: Leaf already in use: %llu != %llu", typeid(T).name(), id, cur->children[lowerBits]->id);
#endif // DEBUG

        cur = cur->children[lowerBits];
    }

#ifdef DEBUG
    cur->id = id;
#endif // DEBUG
    cur->data = data;

    ++d->size;
    return true;
}

template <class T>
bool IdTree<T>::retrieve(const uint64_t id, T &data) const {
    if (id == 0)
        Error::err("Cannot retrieve IdTree<%s> data for id==0", typeid(T).name());

    const size_t cache_index = id % Private::cache_size;
    if (d->cache[cache_index].id == id) {
        data = d->cache[cache_index].data;
        ++d->cache_hit_counter;
        return true;
    } else
        ++d->cache_miss_counter;

    IdTreeNode<T> *cur = d->findNodeForId(id);
    if (cur == nullptr)
        return false;

    data = cur->data;

    d->cache[cache_index].id = id;
    d->cache[cache_index].data = data;

    return true;
}

template <class T>
bool IdTree<T>::remove(uint64_t id) {
    std::vector<IdTreeNode<T> *> path;
    IdTreeNode<T> *cur = d->findNodeForId(id, &path);
    if (cur == nullptr)
        return false;

    int num_children = 0;
    while (num_children == 0 && path.size() > 1) {
        path.pop_back();
        IdTreeNode<T> *parent = path.back();
        num_children = 0;
        if (parent->children != nullptr)
            for (int i = (1 << IdTreeNode<T>::bitsPerNode) - 1; i >= 0; --i) {
                if (cur != nullptr && parent->children[i] == cur) {
                    delete cur;
                    cur = nullptr;
                    parent->children[i] = nullptr;
                }
                if (parent->children[i] != nullptr)
                    ++num_children;
            }
        cur = parent;
    }
    if (num_children == 0 && cur == d->root) {
        delete d->root;
        d->root = nullptr;
    }

    --d->size;
    return true;
}

template <class T>
size_t IdTree<T>::size() const {
    if (d->size == 0)
        /// IdTree was loaded from file and size never computer, so do it now
        d->size = d->compute_size(d->root);
    return d->size;
}

template <class T>
uint16_t IdTree<T>::counter(const uint64_t id) const {
    IdTreeNode<T> *cur = d->findNodeForId(id);
    if (cur == nullptr)
        Error::err("Cannot retrieve counter for a non-existing IdTreeNode<%s> of id=%llu", typeid(T).name(), id);

    return cur->counter;
}

template <class T>
void IdTree<T>::increaseCounter(const uint64_t id) {
    IdTreeNode<T> *cur = d->findNodeForId(id);

    if (cur != nullptr)
        ++cur->counter;
    else
        Error::err("Cannot increase counter for a non-existing IdTreeNode<%s> of id=%llu", typeid(T).name(), id);
}

template <class T>
std::ostream &IdTree<T>::write(std::ostream &output) {
    if (d->root == nullptr)
        return output;
    const size_t s = size();
    output.write((char *)&s, sizeof(s));
    return d->root->write(output);
}
