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

#ifndef IDTREE_H
#define IDTREE_H

#include <istream>
#include <ostream>

#include <osmpbf/osmpbf.h>

#include "error.h"
#include "types.h"
#include "global.h"

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

enum RelationFlags {RoleOuter = 1, RoleInner = 2, RoleInnerOuter = RoleOuter | RoleInner };

struct RelationMem {
    RelationMem() {
        num_members = 0;
        members = NULL;
        member_flags = NULL;
    }

    RelationMem(int num) {
        num_members = num;
        members = (OSMElement *)calloc(num, sizeof(OSMElement));
        if (members == NULL)
            Error::err("Could not allocate memory for RelationMem::members");
        member_flags = (uint16_t *)calloc(num, sizeof(uint16_t));
        if (member_flags == NULL)
            Error::err("Could not allocate memory for RelationMem::member_flags");
    }

    RelationMem &operator=(const RelationMem &other) {
        if (members != NULL)
            free(members);
        if (member_flags != NULL)
            free(member_flags);

        num_members = other.num_members;

        const size_t bytesElements = num_members * sizeof(OSMElement);
        members = (OSMElement *)malloc(bytesElements);
        if (members == NULL)
            Error::err("Could not allocate memory for RelationMem::members");
        memcpy(members, other.members, bytesElements);

        const size_t bytesFlags = num_members * sizeof(uint16_t);
        member_flags = (uint16_t *)malloc(bytesFlags);
        if (member_flags == NULL)
            Error::err("Could not allocate bytesFlags for RelationMem::member_flags");
        memcpy(member_flags, other.member_flags, bytesFlags);

        return *this;
    }

    RelationMem(std::istream &input) {
        input.read((char *)&num_members, sizeof(num_members));
        if (!input)
            Error::err("Could not read number of members from input stream");

        const size_t bytesElements = num_members * sizeof(OSMElement);
        members = (OSMElement *)malloc(bytesElements);
        if (members == NULL)
            Error::err("Could not allocate memory for RelationMem::members");
        input.read((char *)members, bytesElements);
        if (!input)
            Error::err("Could not read all members from input stream");

        const size_t bytesFlags = num_members * sizeof(uint16_t);
        member_flags = (uint16_t *)malloc(bytesFlags);
        if (member_flags == NULL)
            Error::err("Could not allocate memory for RelationMem::member_flags");
        input.read((char *)member_flags, bytesFlags);
        if (!input)
            Error::err("Could not read all members from input stream");
    }

    ~RelationMem() {
        if (members != NULL)
            free(members);
        if (member_flags != NULL)
            free(member_flags);
    }

    std::ostream &write(std::ostream &output) {
        output.write((char *)&num_members, sizeof(num_members));
        if (!output)
            Error::err("Could not write number of members to output stream");

        const size_t bytesElements = num_members * sizeof(OSMElement);
        output.write((char *)members, bytesElements);
        if (!output)
            Error::err("Could not write all members to output stream");

        const size_t bytesFlags = num_members * sizeof(uint16_t);
        output.write((char *)member_flags, bytesFlags);
        if (!output)
            Error::err("Could not write all members to output stream");
        return output;
    }

    uint32_t num_members;
    OSMElement *members;
    uint16_t *member_flags;
};

class WriteableString : public std::string {
public:
    WriteableString()
        : std::string() {
        /// nothing
    }

    WriteableString(const std::string &other)
        : std::string(other) {
        /// nothing
    }

    WriteableString(std::istream &input)
        : std::string() {
        size_t len;
        input.read((char *)&len, sizeof(len));
        if (!input)
            Error::err("Could not string len from input stream");
        char *data = (char *)malloc(sizeof(char) * len);
        input.read(data, len);
        if (!input)
            Error::err("Could not read coordinates from input stream");

        append(data, len);
        free(data);
    }

    std::ostream &write(std::ostream &output) {
        const char *data = c_str();
        const size_t len = strlen(data);
        output.write((char *)&len, sizeof(len));
        if (!output)
            Error::err("Could not write coordinates to output stream");
        output.write(data, len);
        if (!output)
            Error::err("Could not write coordinates to output stream");
        return output;
    }
};

struct Coord {
    Coord() {
        x = y = 0;
    }

    Coord(int _x, int _y) {
        x = _x;
        y = _y;
    }

    Coord &operator=(const Coord &other) {
        x = other.x;
        y = other.y;
        return *this;
    }

    Coord(std::istream &input) {
        input.read((char *)&x, sizeof(x));
        if (!input)
            Error::err("Could not read coordinates from input stream");
        input.read((char *)&y, sizeof(y));
        if (!input)
            Error::err("Could not read coordinates from input stream");
    }

    std::ostream &write(std::ostream &output) {
        output.write((char *)&x, sizeof(x));
        if (!output)
            Error::err("Could not write coordinates to output stream");
        output.write((char *)&y, sizeof(y));
        if (!output)
            Error::err("Could not write coordinates to output stream");
        return output;
    }

    static Coord fromLatLon(double longitude, double latitude) {
        return Coord(fromLongitude(longitude), fromLatitude(latitude));
    }

    inline double longitude() const {
        return toLongitude(x);
    }

    static inline int fromLongitude(const double l) {
        return (int)((l - minlon) * decimeterDegreeLongitude + 0.5);
    }

    static inline double toLongitude(const int x) {
        return ((double)x - 0.5) / decimeterDegreeLongitude + minlon;
    }

    inline double latitude() const {
        return toLatitude(y);
    }

    static inline int fromLatitude(const double l) {
        return (int)((l - minlat) * decimeterDegreeLatitude + 0.5);
    }

    static inline double toLatitude(const int y) {
        return ((double)y - 0.5) / decimeterDegreeLatitude + minlat;
    }

    int x, y;
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
    bool retrieve(const uint64_t id, T &) const;
    bool remove(uint64_t id);

    uint16_t counter(const uint64_t id) const;
    void increaseCounter(const uint64_t id);

    std::ostream &write(std::ostream &output);

private:
    class Private;
    Private *const d;
};

#include "idtree_impl.h"

#endif // IDTREE_H
