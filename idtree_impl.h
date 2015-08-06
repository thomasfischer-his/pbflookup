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
    }

    explicit IdTreeNode(std::istream &input) {
#ifdef DEBUG
        input.read((char *)&id, sizeof(id));
#endif // DEBUG
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
                    Error::warn("Expected '0' or '1', got '%02x'", chr);
            }
        } else
            Error::warn("Expected 'N' or 'C', got '%02x'", chr);
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
bool IdTree<T>::retrieve(const uint64_t id, T &data) {
    if (d->root == NULL) {
        Error::warn("Tree root is invalid, no id was ever added");
        return false;
    }

    IdTreeNode<T> *cur = d->root;
    uint64_t workingId = id;
    for (int s = (IdTreeNode<T>::bitsPerId / IdTreeNode<T>::bitsPerNode) - 1; s >= 0 && workingId > 0; --s) {
        if (cur->children == NULL) {
            //Error::warn("id=%llu   s=%d", id, s);
            return false;
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
            //Error::warn("id=%llu   s=%d lowerBits=%d  workingId=%llu", id, s, lowerBits, workingId);
            return false;
        }

        cur = cur->children[lowerBits];
    }

#ifdef DEBUG
    if (cur->id != id) {
        Error::warn("Ids do not match: %llu != %llu", cur->id, id);
        return false;
    }
#endif // DEBUG

    data = cur->data;

    return true;
}

template <class T>
std::ostream &IdTree<T>::write(std::ostream &output) {
    if (d->root == NULL)
        return output;
    return d->root->write(output);
}
