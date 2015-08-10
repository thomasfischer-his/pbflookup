#ifndef IDTREE_H
#define IDTREE_H

#include <istream>
#include <ostream>

#include <osmpbf/osmpbf.h>

#include "error.h"

struct WayNodes {
    WayNodes() {
        num_nodes = 0;
        nodes = NULL;
    }

    WayNodes(uint32_t num) {
        num_nodes = num;
        nodes = (uint64_t *)calloc(num, sizeof(uint64_t));
        if (nodes == NULL)
            Error::err("Could not allocate memory for WayNodes::nodes");
    }

    WayNodes &operator=(const WayNodes &other) {
        if (nodes != NULL) free(nodes);

        num_nodes = other.num_nodes;
        const size_t bytes = num_nodes * sizeof(uint64_t);
        nodes = (uint64_t *)malloc(bytes);
        if (nodes == NULL)
            Error::err("Could not allocate memory for WayNodes::nodes");
        memcpy(nodes, other.nodes, bytes);
        return *this;
    }

    WayNodes(std::istream &input) {
        input.read((char *)&num_nodes, sizeof(num_nodes));
        if (!input)
            Error::err("Could not read number of nodes from input stream");
        const size_t bytes = num_nodes * sizeof(uint64_t);
        nodes = (uint64_t *)malloc(bytes);
        if (nodes == NULL)
            Error::err("Could not allocate memory for WayNodes::nodes");
        input.read((char *)nodes, bytes);
        if (!input)
            Error::err("Could not read all nodes from input stream");
    }

    ~WayNodes() {
        if (nodes != NULL)
            free(nodes);
    }

    std::ostream &write(std::ostream &output) {
        output.write((char *)&num_nodes, sizeof(num_nodes));
        if (!output)
            Error::err("Could not write number of nodes to output stream");
        const size_t bytes = num_nodes * sizeof(uint64_t);
        output.write((char *)nodes, bytes);
        if (!output)
            Error::err("Could not write all nodes to output stream");
        return output;
    }

    uint32_t num_nodes;
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
        if (members == NULL)
            Error::err("Could not allocate memory for RelationMem::members");
    }

    RelationMem &operator=(const RelationMem &other) {
        if (members != NULL) free(members);

        num_members = other.num_members;
        const size_t bytes = num_members * sizeof(uint64_t);
        members = (uint64_t *)malloc(bytes);
        if (members == NULL)
            Error::err("Could not allocate memory for RelationMem::members");
        memcpy(members, other.members, bytes);
        return *this;
    }

    RelationMem(std::istream &input) {
        input.read((char *)&num_members, sizeof(num_members));
        if (!input)
            Error::err("Could not read number of members from input stream");
        const size_t bytes = num_members * sizeof(uint64_t);
        members = (uint64_t *)malloc(bytes);
        if (members == NULL)
            Error::err("Could not allocate memory for RelationMem::members");
        input.read((char *)members, bytes);
        if (!input)
            Error::err("Could not read all members from input stream");
    }

    ~RelationMem() {
        if (members != NULL)
            free(members);
    }

    std::ostream &write(std::ostream &output) {
        output.write((char *)&num_members, sizeof(num_members));
        if (!output)
            Error::err("Could not write number of members to output stream");
        const size_t bytes = num_members * sizeof(uint64_t);
        output.write((char *)members, bytes);
        if (!output)
            Error::err("Could not write all members to output stream");
        return output;
    }

    uint32_t num_members;
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

    Coord &operator=(const Coord &other) {
        lon = other.lon;
        lat = other.lat;
        return *this;
    }

    Coord(std::istream &input) {
        input.read((char *)&lon, sizeof(lon));
        if (!input)
            Error::err("Could not read coordinates from input stream");
        input.read((char *)&lat, sizeof(lat));
        if (!input)
            Error::err("Could not read coordinates from input stream");
    }

    std::ostream &write(std::ostream &output) {
        output.write((char *)&lon, sizeof(lon));
        if (!output)
            Error::err("Could not write coordinates to output stream");
        output.write((char *)&lat, sizeof(lat));
        if (!output)
            Error::err("Could not write coordinates to output stream");
        return output;
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
    explicit IdTree(std::istream &input);
    ~IdTree();

    bool insert(uint64_t id, T const &);
    bool retrieve(const uint64_t id, T &);

    std::ostream &write(std::ostream &output);

private:
    class Private;
    Private *const d;
};

#include "idtree_impl.h"

#endif // IDTREE_H
