#ifndef IDTREE_H
#define IDTREE_H

#include <osmpbf/osmpbf.h>

struct WayNodes {
    WayNodes() {
        num_nodes = 0;
        nodes = NULL;
    }

    WayNodes(int num) {
        num_nodes = num;
        nodes = (uint64_t *)calloc(num, sizeof(uint64_t));
    }

    ~WayNodes() {
        if (nodes != NULL)
            free(nodes);
    }

    int num_nodes;
    uint64_t *nodes;
};

struct Coord {
    Coord() {
        lon = lat = 0.0;
    }

    Coord(double longitude, double latitude) {
        lon = longitude;
        lat = latitude;
    }

    double lon, lat;
};

template <typename T>
struct IdTreeNode;

template <class T>
class IdTree
{
public:
    explicit IdTree();
    ~IdTree();

    bool insert(uint64_t id, T const &);
    bool retrieve(const uint64_t id, T &);

private:
    struct IdTreeNode<T> *root;
};

#include "idtree_impl.h"

#endif // IDTREE_H
