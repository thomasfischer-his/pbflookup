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

#ifndef IDTREE_H
#define IDTREE_H

#include <cmath>

#include <istream>
#include <ostream>
#include <limits>

#include <osmpbf/osmpbf.h>

#include "error.h"
#include "types.h"
#include "global.h"

struct WayNodes {
    WayNodes()
        : num_nodes(0), nodes(nullptr) {
        /// nothing
    }

    WayNodes(uint32_t _num_nodes)
        : num_nodes(_num_nodes), nodes(nullptr) {
        if (num_nodes == 0)
            Error::err("Creating way without nodes");
        nodes = (uint64_t *)calloc(_num_nodes, sizeof(uint64_t));
        if (nodes == nullptr)
            Error::err("Could not allocate memory for WayNodes::nodes");
    }

    WayNodes &operator=(const WayNodes &other) {
        if (other.num_nodes == 0 || other.nodes == nullptr)
            Error::err("Assigning way without nodes");

        if (nodes != nullptr) free(nodes);

        /// Cast to circumvene 'const'ness
        uint32_t *_num_nodes = (uint32_t *)(&num_nodes);
        *_num_nodes = other.num_nodes;

        const size_t bytes = num_nodes * sizeof(uint64_t);
        nodes = (uint64_t *)malloc(bytes);
        if (nodes == nullptr)
            Error::err("Could not allocate memory for WayNodes::nodes");
        memcpy(nodes, other.nodes, bytes);

        return *this;
    }

    WayNodes(std::istream &input)
        : num_nodes(0), nodes(nullptr) {
        /// Cast to circumvene 'const'ness
        uint32_t *_num_nodes = (uint32_t *)(&num_nodes);
        input.read((char *)_num_nodes, sizeof(num_nodes));
        if (!input)
            Error::err("Could not read number of nodes from input stream");

        if (num_nodes > 0) {
            const size_t bytes = num_nodes * sizeof(uint64_t);
            nodes = (uint64_t *)malloc(bytes);
            if (nodes == nullptr)
                Error::err("Could not allocate memory for WayNodes::nodes");
            input.read((char *)nodes, bytes);
            if (!input)
                Error::err("Could not read all nodes from input stream");
        }
    }

    ~WayNodes() {
        if (nodes != nullptr)
            free(nodes);
    }

    std::ostream &write(std::ostream &output) {
        output.write((char *)&num_nodes, sizeof(num_nodes));
        if (!output)
            Error::err("Could not write number of nodes to output stream");
        if (num_nodes > 0) {
            const size_t bytes = num_nodes * sizeof(uint64_t);
            output.write((char *)nodes, bytes);
            if (!output)
                Error::err("Could not write all nodes to output stream");
        } else
            Error::warn("Writing way without nodes");
        return output;
    }

    const uint32_t num_nodes;
    uint64_t *nodes;
};

enum RelationFlags {RoleOuter = 1, RoleInner = 2, RoleInnerOuter = RoleOuter | RoleInner };

struct RelationMem {
    RelationMem()
        : num_members(0), members(nullptr), member_flags(nullptr) {
        /// nothing
    }

    RelationMem(int num)
        : num_members(num), members(nullptr), member_flags(nullptr) {
        if (num_members <= 0)
            Error::err("Creating relation without members");

        members = (OSMElement *)calloc(num, sizeof(OSMElement));
        if (members == nullptr)
            Error::err("Could not allocate memory for RelationMem::members");
        member_flags = (uint16_t *)calloc(num, sizeof(uint16_t));
        if (member_flags == nullptr)
            Error::err("Could not allocate memory for RelationMem::member_flags");
    }

    RelationMem &operator=(const RelationMem &other) {
        if (other.num_members == 0 || other.members == nullptr || other.member_flags == nullptr)
            Error::err("Assigning relation without members");

        if (members != nullptr)
            free(members);
        if (member_flags != nullptr)
            free(member_flags);

        /// Cast to circumvene 'const'ness
        uint32_t *_num_members = (uint32_t *)(&num_members);
        *_num_members = other.num_members;

        const size_t bytesElements = num_members * sizeof(OSMElement);
        members = (OSMElement *)malloc(bytesElements);
        if (members == nullptr)
            Error::err("Could not allocate memory for RelationMem::members");
        memcpy(members, other.members, bytesElements);

        const size_t bytesFlags = num_members * sizeof(uint16_t);
        member_flags = (uint16_t *)malloc(bytesFlags);
        if (member_flags == nullptr)
            Error::err("Could not allocate bytesFlags for RelationMem::member_flags");
        memcpy(member_flags, other.member_flags, bytesFlags);

        return *this;
    }

    RelationMem(std::istream &input)
        : num_members(0), members(nullptr), member_flags(nullptr) {
        /// Cast to circumvene 'const'ness
        uint32_t *_num_members = (uint32_t *)(&num_members);
        input.read((char *)_num_members, sizeof(num_members));
        if (!input)
            Error::err("Could not read number of members from input stream");

        if (num_members > 0) {
            const size_t bytesElements = num_members * sizeof(OSMElement);
            members = (OSMElement *)malloc(bytesElements);
            if (members == nullptr)
                Error::err("Could not allocate memory for RelationMem::members");
            input.read((char *)members, bytesElements);
            if (!input)
                Error::err("Could not read all members from input stream");

            const size_t bytesFlags = num_members * sizeof(uint16_t);
            member_flags = (uint16_t *)malloc(bytesFlags);
            if (member_flags == nullptr)
                Error::err("Could not allocate memory for RelationMem::member_flags");
            input.read((char *)member_flags, bytesFlags);
            if (!input)
                Error::err("Could not read all members from input stream");
        }
    }

    ~RelationMem() {
        if (members != nullptr)
            free(members);
        if (member_flags != nullptr)
            free(member_flags);
    }

    std::ostream &write(std::ostream &output) {
        output.write((char *)&num_members, sizeof(num_members));
        if (!output)
            Error::err("Could not write number of members to output stream");

        if (num_members > 0) {
            const size_t bytesElements = num_members * sizeof(OSMElement);
            output.write((char *)members, bytesElements);
            if (!output)
                Error::err("Could not write all members to output stream");

            const size_t bytesFlags = num_members * sizeof(uint16_t);
            output.write((char *)member_flags, bytesFlags);
            if (!output)
                Error::err("Could not write all members to output stream");
        }

        return output;
    }

    const uint32_t num_members;
    OSMElement *members;
    uint16_t *member_flags;
};

/**
 * Thin wrapper around std::string, basically only adding serialization
 * operations to an input stream: New constructor can load data from stream,
 * new write(..) function dumps object to stream.
 */
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
        /// Make use of a static buffer to avoid dynamic memory allocations
        size_t len;
        input.read((char *)&len, sizeof(len));
        if (!input)
            Error::err("Could not read string len from input stream");

        clear(); ///< remove any data/garbage that may be inside the string
        if (len > 0) {
            char *buffer = (char *)calloc(((len >> 8) + 1) << 8, sizeof(char)); ///< buffer size is multiple of 256
            if (buffer == nullptr)
                Error::err("Failed to allocate data for string");
            input.read(buffer, len);
            if (!input)
                Error::err("Could not read string from input stream");

            append(buffer, len);
            free(buffer);
        }
    }

    WriteableString &operator=(const WriteableString &other) {
        std::string::operator=(other);
        return *this;
    }

    std::ostream &write(std::ostream &output) {
        const char *data = c_str();
        const size_t len = length();
        output.write((char *)&len, sizeof(len));
        if (!output)
            Error::err("Could not write string to output stream");
        if (len > 0) {
            output.write(data, len);
            if (!output)
                Error::err("Could not write string to output stream");
        }
        return output;
    }
};


/*:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::*/
/*::  This function converts decimal degrees to radians             :*/
/*:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::*/
inline double deg2rad(double deg) {
    static const double pi = 3.14159265358979323846;
    return (deg * pi / 180);
}

/*:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::*/
/*::  This function converts radians to decimal degrees             :*/
/*:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::*/
inline double rad2deg(double rad) {
    static const double pi = 3.14159265358979323846;
    return (rad * 180 / pi);
}

struct Coord {
    /**
     * Create a new coordinate, initialized with x=y=0.
     */
    Coord() {
        x = y = 0;
    }

    /**
     * Create a new coordinate, initialized with given values
     * for x and y on the decimeter grid.
     * @param x Initial x-coordinate
     * @param y Initial y-coordinate
     */
    Coord(int x, int y) {
        this->x = x;
        this->y = y;
    }

    /**
     * Create a new coordinate, initialized with given values
     * for lat and lon.
     * @param lon Initial longitude
     * @param lat Initial latitude
     */
    Coord(double lon, double lat) {
        this->x = fromLongitude(lon);
        this->y = fromLatitude(lat);
    }

    Coord(std::istream &input) {
        input.read((char *)&x, sizeof(x));
        if (!input)
            Error::err("Could not read coordinates from input stream");
        input.read((char *)&y, sizeof(y));
        if (!input)
            Error::err("Could not read coordinates from input stream");
    }

    /**
     * Test if this coordinate is valid (i.e. correctly initialized).
     * @return True if a valid position
     */
    bool isValid() const {
        return x > 0 && y > 0;
    }

    void invalidate() {
        x = y = 0;
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

    Coord &operator=(const Coord &other) {
        x = other.x;
        y = other.y;
        return *this;
    }

    inline bool operator==(const Coord &other) const {
        return other.x == x && other.y == y;
    }


    /**
     * Compute the distance between to coordinates based on their
     * x/y values on the decimeter grid. About twice as fast as
     * distanceLatLon, but most likely off by a few percent (e.g.
     * about 3% in Southern Sweden, up to 20% in Northern Sweden).
     * A corresponding object-variant (non-static) exists as
     * distanceXY(const Coord &).
     * @param a One coordinate
     * @param b Another coordinate
     * @return Distance between two coordinates in meter
     */
    static int distanceXY(const Coord &a, const Coord &b) {
        if (a.x == b.x && a.y == b.y) return 0; ///< identical coordinates
        const int64_t deltaX = ((a.x > b.x ? (a.x - b.x) : (b.x - a.x)) + 5) / 10;
        const int64_t deltaY = ((a.y > b.y ? (a.y - b.y) : (b.y - a.y)) + 5) / 10;
        return (int)sqrt(deltaX * deltaX + deltaY * deltaY + 0.5);
    }

    /**
     * Compute the distance between to coordinates based on their
     * x/y values on the decimeter grid. Should be faster than
     * distanceXY, but returns the square value of it, i.e. omits
     * calling costly sqrt(..).
     * @param a One coordinate
     * @param b Another coordinate
     * @return Distance between two coordinates in meter to the square
     */
    static int64_t distanceXYsquare(const Coord &a, const Coord &b) {
        if (a.x == b.x && a.y == b.y) return 0L; ///< identical coordinates
        const int64_t deltaX = ((a.x > b.x ? (a.x - b.x) : (b.x - a.x)) + 5) / 10;
        const int64_t deltaY = ((a.y > b.y ? (a.y - b.y) : (b.y - a.y)) + 5) / 10;
        return deltaX * deltaX + deltaY * deltaY;
    }

    /**
     * Compute the distance between to coordinates based on their
     * latitudes and longitudes. About half the speed of distanceXY
     * which is based on a decimeter grid, but exact.
     * A corresponding object-variant (non-static) exists as
     * distanceLatLon(const Coord &).
     * @param a One coordinate
     * @param b Another coordinate
     * @return Distance between two coordinates in meter
     */
    static int distanceLatLon(const Coord &a, const Coord &b) {
        if (a.x == b.x && a.y == b.y) return 0; ///< identical coordinates

        const double latA = toLatitude(a.y);
        const double lonA = toLongitude(a.x);
        const double latB = toLatitude(b.y);
        const double lonB = toLongitude(b.x);

        const double theta = lonA - lonB;
        double dist = sin(deg2rad(latA)) * sin(deg2rad(latB)) + cos(deg2rad(latA)) * cos(deg2rad(latB)) * cos(deg2rad(theta));
        dist = acos(dist);
        dist = rad2deg(dist);
        dist = dist * 60 * 1853.1596;
        return (int)(dist + 0.5);
    }

    /**
     * An object-variant (non-static) of distanceXY(const Coord &,const Coord &).
     * @return Distance between two coordinates in meter
     */
    inline int distanceXY(const Coord &other) const {
        return Coord::distanceXY(*this, other);
    }
    /**
     * An object-variant (non-static) of distanceXYsquare(const Coord &,const Coord &).
     * @return Distance between two coordinates in meter to the square
     */
    inline int distanceXYsquare(const Coord &other) const {
        return Coord::distanceXYsquare(*this, other);
    }

    /**
     * An object-variant (non-static) of distanceLatLon(const Coord &,const Coord &).
     * @return Distance between two coordinates in meter
     */
    inline int distanceLatLon(const Coord &other) const {
        return Coord::distanceLatLon(*this, other);
    }

    static Coord fromLonLat(double longitude, double latitude) {
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


namespace std
{
template <>
struct hash<Coord>
{
    inline size_t operator()(const Coord &element) const {
        size_t x = (size_t)(element.x % numeric_limits<size_t>::max());
        size_t y = (size_t)(element.y % numeric_limits<size_t>::max());
        static const int shiftbits = (sizeof(size_t) - sizeof(element.y)) * 8;
        if (shiftbits > 0)
            y <<= shiftbits;
        return x ^ y;
    }
};
}


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
    /**
     * The number of elements inserted (and not yet removed)
     * into this tree.
     * @return Number of elements
     */
    size_t size() const;

    uint16_t counter(const uint64_t id) const;
    void increaseCounter(const uint64_t id);

    std::ostream &write(std::ostream &output);

private:
    class Private;
    Private *const d;
};

#include "idtree_impl.h"

#endif // IDTREE_H
