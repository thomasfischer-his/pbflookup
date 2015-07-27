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

    WayNodes &operator=(const WayNodes &other) {
        if (nodes != NULL) free(nodes);

        num_nodes = other.num_nodes;
        const size_t bytes = num_nodes * sizeof(uint64_t);
        nodes = (uint64_t *)malloc(bytes);
        memcpy(nodes, other.nodes, bytes);
        return *this;
    }

    ~WayNodes() {
        if (nodes != NULL)
            free(nodes);
    }

    int num_nodes;
    uint64_t *nodes;
};

struct RelationMem {
    RelationMem() {
        num_members = 0;
        members = NULL;
    }

    RelationMem(int num) {
        num_members = num;
        members = (uint64_t *)calloc(num, sizeof(uint64_t));
    }

    RelationMem &operator=(const RelationMem &other) {
        if (members != NULL) free(members);

        num_members = other.num_members;
        const size_t bytes = num_members * sizeof(uint64_t);
        members = (uint64_t *)malloc(bytes);
        memcpy(members, other.members, bytes);
        return *this;
    }

    ~RelationMem() {
        if (members != NULL)
            free(members);
    }

    int num_members;
    uint64_t *members;
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
    class Private;
    Private *const d;
};

#include "idtree_impl.h"

#endif // IDTREE_H
