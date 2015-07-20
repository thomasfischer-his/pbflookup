#include "nodetocoord.h"

#include "error.h"

struct N2CNode {
    static const int bitsPerNode;
    static const int bitsPerId;

    explicit N2CNode() {
        children = NULL;
        lat = lon = 0.0;
        id = 0;
    }

    ~N2CNode() {
        if (children != NULL) {
            for (int i = (1 << bitsPerNode) - 1; i >= 0; --i) {
                if (children[i] != NULL)
                    delete children[i];
            }
            free(children);
        }
    }

    N2CNode **children;

    uint64_t id;
    double lat, lon;
};

const int N2CNode::bitsPerNode = 4;
const int N2CNode::bitsPerId = 32;

NodeToCoord::NodeToCoord()
{
    root = NULL;
}

NodeToCoord::~NodeToCoord()
{
    if (root != NULL)
        delete root;
}

bool NodeToCoord::insert(uint64_t id, double lat, double lon) {
    if (root == NULL) root = new N2CNode;

    static const unsigned int num_children = 1 << N2CNode::bitsPerNode;
    static const unsigned int mask = num_children - 1;
    N2CNode *cur = root;
    uint64_t workingId = id;
    for (int s = (N2CNode::bitsPerId / N2CNode::bitsPerNode) - 1; s >= 0; --s) {
        if (cur->children == NULL)
            cur->children = (N2CNode **)calloc(num_children, sizeof(N2CNode *));

        unsigned int lowerBits = workingId & mask;
        workingId >>= N2CNode::bitsPerNode;

        if (cur->children[lowerBits] == NULL)
            cur->children[lowerBits] = new N2CNode;
        else if (s == 0)
            Error::err("Leaf already in use: %ld != %ld", id, cur->children[lowerBits]->id);

        cur = cur->children[lowerBits];
    }

    cur->id = id;
    cur->lat = lat;
    cur->lon = lon;

    return true;
}

bool NodeToCoord::retrieve(uint64_t id, double &lat, double &lon) {
    lon = lat = 0.0;
    if (root == NULL) {
        Error::warn("Tree root is invalid, no id was ever added");
        return false;
    }

    static const unsigned int num_children = 1 << N2CNode::bitsPerNode;
    static const unsigned int mask = num_children - 1;

    N2CNode *cur = root;
    uint64_t workingId = id;
    for (int s = (N2CNode::bitsPerId / N2CNode::bitsPerNode) - 1; s >= 0; --s) {
        if (cur->children == NULL) {
            Error::warn("id=%ld   s=%d", id, s);
            return false;
        }

        unsigned int lowerBits = workingId & mask;
        workingId >>= N2CNode::bitsPerNode;

        if (cur->children[lowerBits] == NULL) {
            Error::warn("id=%ld   s=%d lowerBits=%d  workingId=%ld", id, s, lowerBits, workingId);
            return false;
        }

        cur = cur->children[lowerBits];
    }

    if (cur->id != id) {
        Error::warn("Ids do not match: %ld != %ld", cur->id, id);
        return false;
    }

    lat = cur->lat;
    lon = cur->lon;

    return true;
}
