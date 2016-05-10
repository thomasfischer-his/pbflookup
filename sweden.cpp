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

/** FIXME
 *  Much of this code here uses 'old-style' C with malloc/calloc and, more severe,
 *  keeping track of arrays in variables separate of the array variable itself.
 *  This code should be ported to a more modern, safer style.
 */

#include "sweden.h"

#include <cmath>

#include <iostream>
#include <fstream>
#include <deque>
#include <algorithm>
#include <vector>
#include <unordered_set>

#include "error.h"
#include "globalobjects.h"
#include "svgwriter.h"
#include "helper.h"

const double minlon = 4.4; ///< declared in 'global.h'
const double minlat = 53.8; ///< declared in 'global.h'
const double maxlon = 35.0; ///< declared in 'global.h'
const double maxlat = 71.5; ///< declared in 'global.h'

/// Values come from  http://www.csgnetwork.com/degreelenllavcalc.html
const double decimeterDegreeLongitude = 557999.790; ///< declared in 'global.h'
const double decimeterDegreeLatitude = 1114122.402; ///< declared in 'global.h'

const size_t reasonableLargeSizeT = 0x3fffff;
const uint16_t reasonableLargeUInt16 = 0x3fff;

#define STRING_BUFFER_SIZE     1024

bool startsWith(const std::string &haystack, const std::string &needle) {
    if (haystack.length() >= needle.length())
        return haystack.compare(0, needle.length(), needle) == 0;
    else
        return false;
}

bool endsWith(const std::string &haystack, const std::string &needle) {
    if (haystack.length() >= needle.length())
        return haystack.compare(haystack.length() - needle.length(), needle.length(), needle) == 0;
    else
        return false;
}

class AdministrativeRegion {
private:
    static const std::string region_beginnings[];
    static const std::string region_endings[];

    struct Region {
        std::string name;
        int admin_level;
        uint64_t relationId;

        Region()
            : admin_level(-1), relationId(0) {
            /// nothing
        }

        Region(const std::string &_name, int _admin_level, uint64_t _relationId)
            : name(_name), admin_level(_admin_level), relationId(_relationId) {
            /// nothing
        }
    };

    std::vector<Region> regions;
    bool regions_sorted = false;

    std::string normalizeAdministrativeRegionName(const std::string &name) const {
        std::string internal_name(name);
        utf8tolower(internal_name);

        for (int i = 0; !region_beginnings[i].empty(); ++i)
            if (startsWith(internal_name, region_beginnings[i]))
                return internal_name.substr(region_beginnings[i].length(), internal_name.length() - region_beginnings[i].length());

        for (int i = 0; !region_endings[i].empty(); ++i)
            if (endsWith(internal_name, region_endings[i])) {
                internal_name = internal_name.substr(0, internal_name.length() - region_endings[i].length());

                static const std::unordered_set<std::string> municipalitiesMissingS = {"alings\xc3\xa5", "bengtsfor", "bolln\xc3\xa4", "degerfor", "grum", "hagfor", "hofor", "h\xc3\xa4llefor", "h\xc3\xb6gan\xc3\xa4", "kramfor", "munkfor", "m\xc3\xb6nster\xc3\xa5", "robertsfor", "soten\xc3\xa4", "storfor", "str\xc3\xa4ngn\xc3\xa4", "tors\xc3\xa5", "tran\xc3\xa5", "v\xc3\xa4nn\xc3\xa4", "bor\xc3\xa5", "v\xc3\xa4ster\xc3\xa5"};
                if (municipalitiesMissingS.count(internal_name) > 0)
                    internal_name += "s";

                return internal_name;
            }

        return internal_name;
    }

    void sortRegions() {
        std::sort(regions.begin(), regions.end(), [](struct AdministrativeRegion::Region & a, struct AdministrativeRegion::Region & b) {
            const int cmp = a.name.compare(b.name);
            if (cmp < 0) return true;
            else if (cmp > 0) return false;
            else { /** cmp == 0 */
                /**
                 * High level administrative organizations like counties
                 * go before lower-level organizations. For example,
                 * Uppsala county goes before Uppsala municipality
                 * http://www.openstreetmap.org/relation/54220
                 * http://www.openstreetmap.org/relation/305455
                 */
                return a.admin_level < b.admin_level;
            }
        });
        regions_sorted = true;
    }

public:
    explicit AdministrativeRegion()
        : regions_sorted(false) {
        /// nothing
    }

    std::istream &read(std::istream &input) {
        regions.clear();
        size_t count;
        input.read((char *)&count, sizeof(count));
        for (size_t i = 0; i < count; ++i) {
            Region region;
            input.read((char *)&region.relationId, sizeof(region.relationId));
            input.read((char *)&region.admin_level, sizeof(region.admin_level));
            size_t str_len;
            input.read((char *)&str_len, sizeof(str_len));
            static const size_t buffer_len = 8192;
            static char buffer[buffer_len];
            /// Assumption: str_len * sizeof(char) < 8192
            input.read(buffer, str_len * sizeof(char));
            buffer[str_len] = '\0';
            region.name = std::string(buffer);
            regions.push_back(region);
        }

        /// By convention, data was sorted before saving it
        regions_sorted = true;

        return input;
    }

    std::ostream &write(std::ostream &output) {
        if (!regions_sorted)
            sortRegions();

        const size_t count = regions.size();
        output.write((char *) &count, sizeof(count));
        for (size_t i = 0; i < count; ++i) {
            output.write((char *) &regions[i].relationId, sizeof(regions[i].relationId));
            output.write((char *) &regions[i].admin_level, sizeof(regions[i].admin_level));
            const char *buffer = regions[i].name.c_str();
            const size_t buffer_len = strlen(buffer);
            output.write((char *) &buffer_len, sizeof(buffer_len));
            output.write(buffer, buffer_len * sizeof(char));
        }

        return output;
    }

    void insert(const std::string &name, int admin_level, uint64_t relationId) {
        const std::string internal_name = normalizeAdministrativeRegionName(name);
        Region region(internal_name, admin_level, relationId);
        regions.push_back(region);
        regions_sorted = false;
    }

    uint64_t retrieve(const std::string &name, int *admin_level = NULL) {
        if (admin_level != NULL)
            *admin_level = 0;
        if (regions.empty()) return 0; ///< no administrative regions are known

        if (!regions_sorted)
            sortRegions();

        const std::string normalized_name = normalizeAdministrativeRegionName(name);
        int min = 0, max = regions.size() - 1, result = -1;
        while (min < max) {
            const int idx = (max - min) / 2 + min;
            const int cmp = normalized_name.compare(regions[idx].name);
            if (cmp > 0)
                min = idx + 1;
            else if (cmp < 0)
                max = idx - 1;
            else if (cmp == 0) {
                result = idx;
                break;
            }
        }
        if (min == max && normalized_name.compare(regions[min].name) == 0)
            result = min;

        if (result >= 0) {
            /// Select region of highest administrative level (e.g. county over municipality)
            while (result > 0 && regions[result - 1].admin_level < regions[result].admin_level && normalized_name.compare(regions[result - 1].name) == 0)
                --result;

            if (admin_level != NULL)
                *admin_level = regions[result].admin_level;
            return regions[result].relationId;
        } else
            return 0;
    }
};

const std::string AdministrativeRegion::region_beginnings[] = {"landskapet ", ""};
/// Some names have a genitiv-s, some names simply have 's' as the last character.
/// This wil lbe no problem for the purpose of matching known counties or municipality
/// to queried text strings.
const std::string AdministrativeRegion::region_endings[] = {"s l\xc3\xa4n", " l\xc3\xa4n", "s kommun", " kommun" , ""};

class Sweden::Private {
private:
    static const int INT_RANGE;

    Sweden *p;

public:
    static const uint16_t terminator16bit;
    static const size_t terminatorSizeT;

    struct Region {
        std::vector<std::deque<Coord> > polygons;
        int minx, miny, maxx, maxy;
    };
    std::map<int, uint64_t> scbcode_to_relationid, nuts3code_to_relationid;
    std::map<uint64_t, Region> relationId_to_polygons;

    static constexpr size_t european_len = 30, national_len = 500;
    static const size_t regional_outer_len, regional_inner_len;
    struct {
        std::array<std::vector<uint64_t>, european_len> european;
        std::array<std::vector<uint64_t>, national_len> national;
        std::vector<uint64_t> ** **regional;
    } roads;
    static const size_t regional_len;
    static const std::vector<int> EuropeanRoadNumbers;

    AdministrativeRegion administrativeRegion;

    explicit Private(Sweden *parent)
        : p(parent) {
        roads.regional = (std::vector<uint64_t> ** **)calloc(regional_len, sizeof(std::vector<uint64_t> ** *));
    }

    ~Private() {
        for (size_t i = 0; i < regional_len; ++i)
            if (roads.regional[i] != NULL) {
                for (size_t j = 0; j < regional_outer_len; ++j)
                    if (roads.regional[i][j] != NULL) {
                        for (size_t k = 0; k < regional_inner_len; ++k)
                            if (roads.regional[i][j][k] != NULL) delete roads.regional[i][j][k];
                        free(roads.regional[i][j]);
                    }
                free(roads.regional[i]);
            }
        free(roads.regional);
    }

    static inline int europeanRoadNumberToIndex(int eRoadNumber) {
        if (eRoadNumber < 40)
            /// Numbers 4, 6, 10, 12, 14, 16, 18, 20, 22 stay as they are
            return eRoadNumber;
        else if (eRoadNumber < 40 + (int)european_len)
            /// Numbers 45, 47, 55, 65 (<40+30) become 5, 7, 15, 25
            return eRoadNumber - 40;
        else if (eRoadNumber == 265)
            /// Number 265 becomes 1
            return 1;
        else {
            Error::warn("Cannot map E%d to a road number index", eRoadNumber);
            return 0;
        }
    }

    /**
     * Adding the nodes contained in a WayNodes object to an existing
     * polygon (list of nodes). Emphasis is put on adding a WayNode's
     * nodes only if this way can be attached to the polygon,
     * otherwise 'false' is returned and another WayNodes object may
     * be tested for attachment.
     * If the WayNodes object's nodes can be attached, they will be
     * attached to the right end and in the right order to match the
     * polygon.
     * If the polygon is empty, the WayNodes object will be inserted
     * completely into the polygon.
     */
    bool addWayToPolygon(const WayNodes &wn, std::deque<Coord> &polygon) {
        if (polygon.empty()) {
            /// Always add first way completely
            for (uint32_t j = 0; j < wn.num_nodes; ++j) {
                const uint64_t nodeid = wn.nodes[j];
                Coord coord;
                if (node2Coord->retrieve(nodeid, coord))
                    polygon.push_back(coord);
                else
                    Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[j]);
            }
            return true;
        }

        /// Retrieve the coordinates of one end of the way
        /// as described by the WayNodes object
        Coord coord;
        if (!node2Coord->retrieve(wn.nodes[0], coord)) {
            Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[0]);
            return false;
        }

        /// Test if the way can be attached to one side of the (growing) polygon
        if (polygon[0].x == coord.x && polygon[0].y == coord.y) {
            for (uint32_t j = 1; j < wn.num_nodes; ++j) {
                if (node2Coord->retrieve(wn.nodes[j], coord))
                    polygon.push_front(coord);
                else
                    Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[j]);
            }
            return true;
        } else
            /// Test if the way can be attached to other side of the (growing) polygon
            if (polygon[polygon.size() - 1].x == coord.x && polygon[polygon.size() - 1].y == coord.y) {
                for (uint32_t j = 1; j < wn.num_nodes; ++j) {
                    if (node2Coord->retrieve(wn.nodes[j], coord))
                        polygon.push_back(coord);
                    else
                        Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[j]);
                }
                return true;
            }

        /// Retrieve the coordinates of the other end of the way
        /// as described by the WayNodes object
        if (!node2Coord->retrieve(wn.nodes[wn.num_nodes - 1], coord)) {
            Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[wn.num_nodes - 1]);
            return false;
        }

        /// Test if the way can be attached to one side of the (growing) polygon
        if (polygon[0].x == coord.x && polygon[0].y == coord.y) {
            for (int j = wn.num_nodes - 2; j >= 0; --j) {
                if (node2Coord->retrieve(wn.nodes[j], coord))
                    polygon.push_front(coord);
                else
                    Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[j]);
            }
            return true;
        } else
            /// Test if the way can be attached to other side of the (growing) polygon
            if (polygon[polygon.size() - 1].x == coord.x && polygon[polygon.size() - 1].y == coord.y) {
                for (int j = wn.num_nodes - 2; j >= 0; --j) {
                    if (node2Coord->retrieve(wn.nodes[j], coord))
                        polygon.push_back(coord);
                    else
                        Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[j]);
                }
                return true;
            }

        /// Way could not attached to either side of the polygon
        return false;
    }

    void buildPolygonForRelation(uint64_t relid) {
        if (relationId_to_polygons.find(relid) != relationId_to_polygons.cend()) return;

        int minx = INT_RANGE, miny = INT_RANGE, maxx = -1, maxy = -1;
        RelationMem rel;
        if (relMembers->retrieve(relid, rel) && rel.num_members > 0) {
            std::vector<std::deque<Coord> > polygonlist;

            /// Keep track of which ways of a relation have already been added to one of the polygons
            bool *wayattached = new bool[rel.num_members];
            uint32_t expected_outer_members = 0;
            for (int i = rel.num_members - 1; i >= 0; --i) {
                wayattached[i] = false; ///< initially, no way is added to any polygon
                /// Compute how many ways are expected to describe the outer boundary of a polygon
                if (rel.members[i].type == OSMElement::Way && (rel.member_flags[i] & RelationFlags::RoleInnerOuter) > 0) ++expected_outer_members;
            }

            uint32_t successful_additions = 0;
            /// 'wrap around' is neccessary, as multiple iterations over the set of ways
            /// may be required to identify all ways in the correct order for insertion
            for (uint32_t wrap_around = 0; successful_additions < expected_outer_members && wrap_around < rel.num_members + 5; ++wrap_around)
                for (uint32_t i = 0; i < rel.num_members && successful_additions < expected_outer_members; ++i) {
                    if (wayattached[i]) continue; ///< skip ways that got added in previous wrap_around iterations
                    if (rel.members[i].type != OSMElement::Way) continue; ///< consider only ways as polygon boundaries
                    if ((rel.member_flags[i] & RelationFlags::RoleInnerOuter) == 0) continue; ///< consider only members of role 'outer' or 'inner'

                    WayNodes wn;
                    const uint64_t memid = rel.members[i].id;
                    if (wayNodes->retrieve(memid, wn)) {
                        bool successfullyAdded = false;
                        for (std::vector<std::deque<Coord> >::iterator it = polygonlist.begin(); !successfullyAdded && it != polygonlist.end(); ++it) {
                            /// Test existing polygons if current way can be attached
                            if (addWayToPolygon(wn, *it)) {
                                successfullyAdded = true;
                            }
                        }
                        if (!successfullyAdded) {
                            /// No existing polygon was feasible to attach the way to,
                            /// so create a new polygon, add way, and add polygon to list of polygons
                            std::deque<Coord> polygon;
                            if (addWayToPolygon(wn, polygon)) {
                                successfullyAdded = true;
                                polygonlist.push_back(polygon);
                            }
                        }

                        if (successfullyAdded) {
                            ++successful_additions;
                            wayattached[i] = true;

                            /// Go through all nodes inside the just added way,
                            /// retrieve coordinates and record min/max coordinates
                            /// later used to determine bounding rectangle around polygons
                            Coord c;
                            for (int i = wn.num_nodes - 1; i >= 0; --i)
                                if (node2Coord->retrieve(wn.nodes[i], c)) {
                                    if (c.x < minx) minx = c.x;
                                    if (c.x > maxx) maxx = c.x;
                                    if (c.y < miny) miny = c.y;
                                    if (c.y > maxy) maxy = c.y;
                                }
                        }
                    } else {
                        /// Warn about member ids that are not ways (and not nodes)
                        Error::warn("Id %llu is way in relation %llu, but no nodes could be retrieved for this way", memid, relid);
                    }
                }

            if (successful_additions < expected_outer_members) {
                Error::warn("Only %i out of %i elements could not be attached to polygon for relation %llu", successful_additions, expected_outer_members, relid);
            }

            delete[] wayattached;

            bool stillMatchingPolygons = true;
            while (stillMatchingPolygons && polygonlist.size() > 1) {
                stillMatchingPolygons = false;

                for (std::vector<std::deque<Coord> >::iterator itA = polygonlist.begin(); itA != polygonlist.end(); ++itA) {
                    /// Look for 'open' polygons
                    std::deque<Coord> &polygonA = *itA;
                    const Coord &firstA = polygonA.front();
                    const Coord &lastA = polygonA.back();
                    if (firstA.x != lastA.x || firstA.y != lastA.y) {
                        /// Look for a matching second polygon
                        for (std::vector<std::deque<Coord> >::iterator itB = itA + 1; itB != polygonlist.end();) {
                            std::deque<Coord> &polygonB = *itB;
                            const Coord &firstB = polygonB.front();
                            const Coord &lastB = polygonB.back();
                            if (firstA.x == firstB.x && firstA.y == firstB.y) {
                                for (std::deque<Coord>::const_iterator itP = polygonB.cbegin() + 1; itP != polygonB.cend(); ++itP)
                                    polygonA.push_front(*itP);
                                itB = polygonlist.erase(itB);
                                stillMatchingPolygons = true;
                            } else if (firstA.x == lastB.x && firstA.y == lastB.y) {
                                for (std::deque<Coord>::const_reverse_iterator itP = polygonB.crbegin() + 1; itP != polygonB.crend(); ++itP)
                                    polygonA.push_front(*itP);
                                itB = polygonlist.erase(itB);
                                stillMatchingPolygons = true;
                            } else if (lastA.x == firstB.x && lastA.y == firstB.y) {
                                for (std::deque<Coord>::const_iterator itP = polygonB.cbegin() + 1; itP != polygonB.cend(); ++itP)
                                    polygonA.push_back(*itP);
                                itB = polygonlist.erase(itB);
                                stillMatchingPolygons = true;
                            } else if (lastA.x == lastB.x && lastA.y == lastB.y) {
                                for (std::deque<Coord>::const_reverse_iterator itP = polygonB.crbegin() + 1; itP != polygonB.crend(); ++itP)
                                    polygonA.push_back(*itP);
                                itB = polygonlist.erase(itB);
                                stillMatchingPolygons = true;
                            } else
                                ++itB;
                        }
                    }
                }
            }

            if (successful_additions == expected_outer_members) {
                /// If all members of the relation could be attached
                /// to the polygon, the relation is contained completely
                /// in the geographic database and as such may be considered
                /// in the following analysis

                int i = 0;
                for (std::vector<std::deque<Coord> >::iterator it = polygonlist.begin(); it != polygonlist.end(); ++it, ++i) {
                    std::deque<Coord> &polygon = *it;
                    const Coord &first = polygon.front();
                    const Coord &last = polygon.back();
                    if (first.x == last.x && first.y == last.y) {
                        /// First and last element in polygon are identical,
                        /// but as that is redundant for the polygon, remove last element
                        polygon.pop_back();
                    } else
                        Error::warn("Unexpectedly, the first and last element in polygon %d for relation %llu do not match", i, relid);
                }
                Region region;
                region.polygons = polygonlist;
                region.minx = minx;
                region.miny = miny;
                region.maxx = maxx;
                region.maxy = maxy;
                relationId_to_polygons.insert(std::pair<uint64_t, Region>(relid, region));
            } else
                Error::info("Could not insert relation %llu, not all ways found/known?", relid);
        }
    }

    bool nodeInsideRelationRegion(const Coord &coord, uint64_t relationId) {
        buildPolygonForRelation(relationId);
        const Region &region = relationId_to_polygons.at(relationId);

        /// Quick check if node is outside rectangle that encloses all polygons,
        /// avoids costly operations further below
        if (coord.x < region.minx || coord.x > region.maxx || coord.y < region.miny || coord.y > region.maxy) return false;

        for (const std::deque<Coord> &polygon : region.polygons) {
            /// For a good explanation, see here: http://alienryderflex.com/polygon/
            const int polyCorners = polygon.size();
            int j = polyCorners - 1;
            bool oddNodes = false;

            for (int i = 0; i < polyCorners; ++i) {
                if (((polygon[i].y < coord.y && polygon[j].y >= coord.y) || (polygon[j].y < coord.y && polygon[i].y >= coord.y)) && (polygon[i].x <= coord.x || polygon[j].x <= coord.x)) {
                    const int intermediate = polygon[i].x + (coord.y - polygon[i].y) * (polygon[j].x - polygon[i].x) / (polygon[j].y - polygon[i].y);
                    oddNodes ^= intermediate < coord.x;
                }
                j = i;
            }

            if (oddNodes)
                return true;
        }

        return false;
    }

    bool nodeInsideRelationRegion(uint64_t nodeid, uint64_t relationId) {
        Coord coord;
        if (node2Coord->retrieve(nodeid, coord))
            return nodeInsideRelationRegion(coord, relationId);

        return false;
    }

    void drawArea(const std::string &filename, const std::map<int, uint64_t> &code_to_relationid) {
        SvgWriter writer(filename);
        drawArea(writer, code_to_relationid);
    }

    void drawArea(SvgWriter &svgWriter, const std::map<int, uint64_t> &code_to_relationid) {
        for (auto itCRID = code_to_relationid.cbegin(); itCRID != code_to_relationid.cend(); ++itCRID) {
            const int &code = itCRID->first;
            const uint64_t &relId = itCRID->second;
            buildPolygonForRelation(relId);
            const Region &region = relationId_to_polygons[relId];

            char buffer[STRING_BUFFER_SIZE];
            snprintf(buffer, STRING_BUFFER_SIZE, "area code: %i", code);
            for (const std::deque<Coord> dequeCoord : region.polygons) {
                std::vector<int> x, y;
                for (auto itC = dequeCoord.cbegin(); itC != dequeCoord.cend(); ++itC) {
                    x.push_back(itC->x);
                    y.push_back(itC->y);
                }
                svgWriter.drawPolygon(x, y, SvgWriter::BaseGroup, std::string(buffer));
            }
        }
    }

    void closestWayNodeToCoord(int x, int y, uint64_t wayId, uint64_t &resultNodeId, int64_t &minSqDistance) {
        resultNodeId = 0;
        minSqDistance = INT64_MAX;

        WayNodes wn;
        if (wayNodes->retrieve(wayId, wn)) {
            for (uint32_t i = 0; i < wn.num_nodes; ++i) {
                Coord c;
                if (node2Coord->retrieve(wn.nodes[i], c)) {
                    const int64_t dX = c.x - x;
                    const int64_t dY = c.y - y;
                    const int64_t sqDist = dX * dX + dY * dY;
                    if (sqDist < minSqDistance) {
                        resultNodeId = wn.nodes[i];
                        minSqDistance = sqDist;
                    }
                }
            }
        }
    }

    Sweden::RoadType lettersToRoadType(const char *letters, uint16_t roadNumber) const {
        if (letters[1] == '\0') {
            switch (letters[0]) {
            case 'c':
                return Sweden::LanC;
            case 'd':
                return Sweden::LanD;
            case 'e':
                return Sweden::identifyEroad(roadNumber);
            case 'f':
                return Sweden::LanF;
            case 'g':
                return Sweden::LanG;
            case 'h':
                return Sweden::LanH;
            case 'i':
                return Sweden::LanI;
            case 'k':
                return Sweden::LanK;
            case 'm':
                return Sweden::LanM;
            case 'n':
                return Sweden::LanN;
            case 'o':
                return Sweden::LanO;
            case 's':
                return Sweden::LanS;
            case 't':
                return Sweden::LanT;
            case 'u':
                return Sweden::LanU;
            case 'w':
                return Sweden::LanW;
            case 'x':
                return Sweden::LanX;
            case 'y':
                return Sweden::LanY;
            case 'z':
                return Sweden::LanZ;
            }
        } else if (letters[2] == '\0') {
            if (letters[0] == 'a' && letters[1] == 'b')
                return Sweden::LanAB;
            else if (letters[0] == 'a' && letters[1] == 'c')
                return Sweden::LanAC;
            else if (letters[0] == 'b' && letters[1] == 'd')
                return Sweden::LanBD;
        }

        Error::warn("Cannot determine road type for letters %s and road number %d", letters, roadNumber);
        return Sweden::UnknownRoadType;
    }
};

const int Sweden::Private::INT_RANGE = 0x3fffffff;
const uint16_t Sweden::Private::terminator16bit = 0xfefe;
const size_t Sweden::Private::terminatorSizeT = 0xcafebabe;
const size_t Sweden::Private::regional_len = Sweden::UnknownRoadType - 2;
const std::vector<int> Sweden::Private::EuropeanRoadNumbers = {4, 6, 10, 12, 14, 16, 18, 20, 22, 45, 47, 55, 65, 265};
/// Assumption: no regional road number is larger or equal to regional_outer_len * regional_inner_len = 4096
const size_t Sweden::Private::regional_outer_len = 64;
const size_t Sweden::Private::regional_inner_len = 64;

Sweden::Road::operator std::string() const {
    switch (type) {
    case RoadType::Europe: return "Eu" + std::to_string(number);
    case RoadType::National: return "Rik" + std::to_string(number);
    case RoadType::LanAB: return "AB" + std::to_string(number);
    case RoadType::LanAC: return "AC" + std::to_string(number);
    case RoadType::LanBD: return "BD" + std::to_string(number);
    case RoadType::LanC: return "C" + std::to_string(number);
    case RoadType::LanD: return "D" + std::to_string(number);
    case RoadType::LanE: return "E" + std::to_string(number);
    case RoadType::LanF: return "F" + std::to_string(number);
    case RoadType::LanG: return "G" + std::to_string(number);
    case RoadType::LanH: return "H" + std::to_string(number);
    case RoadType::LanI: return "I" + std::to_string(number);
    case RoadType::LanK: return "K" + std::to_string(number);
    case RoadType::LanM: return "M" + std::to_string(number);
    case RoadType::LanN: return "N" + std::to_string(number);
    case RoadType::LanO: return "O" + std::to_string(number);
    case RoadType::LanS: return "S" + std::to_string(number);
    case RoadType::LanT: return "T" + std::to_string(number);
    case RoadType::LanU: return "U" + std::to_string(number);
    case RoadType::LanW: return "W" + std::to_string(number);
    case RoadType::LanX: return "X" + std::to_string(number);
    case RoadType::LanY: return "Y" + std::to_string(number);
    case RoadType::LanZ: return "Z" + std::to_string(number);
    default: return "?" + std::to_string(number);
    }
}

Sweden::Sweden()
    : d(new Sweden::Private(this))
{
    /// nothing
}

Sweden::Sweden(std::istream &input)
    : d(new Sweden::Private(this))
{
    char chr = '\0';
    input.read((char *)&chr, sizeof(chr));
    if (chr == 'S') {
        size_t num_elements;
        input.read((char *)&num_elements, sizeof(num_elements));
        for (size_t i = 0; i < num_elements; ++i) {
            int code;
            input.read((char *)&code, sizeof(int));
            uint64_t nodeid;
            input.read((char *)&nodeid, sizeof(uint64_t));
            d->scbcode_to_relationid.insert(std::pair<int, uint64_t>(code, nodeid));
        }
    } else
        Error::warn("Expected 'S', got '0x%02x'", chr);

    input.read((char *)&chr, sizeof(chr));
    if (chr == 'n') {
        size_t  num_elements;
        input.read((char *)&num_elements, sizeof(num_elements));
        for (size_t i = 0; i < num_elements; ++i) {
            int code;
            input.read((char *)&code, sizeof(int));
            uint64_t nodeid;
            input.read((char *)&nodeid, sizeof(uint64_t));
            d->nuts3code_to_relationid.insert(std::pair<int, uint64_t>(code, nodeid));
        }
    } else
        Error::warn("Expected 'n', got '0x%02x'", chr);

    input.read((char *)&chr, sizeof(chr));
    if (chr == 'E') {
        for (size_t i = 0; i < Private::EuropeanRoadNumbers.size(); ++i) {
            size_t count;
            input.read((char *)&count, sizeof(count));
            if (count > reasonableLargeSizeT)
                Error::err("Count %ld looks unrealistically large", count);
            uint64_t wayid;
            for (size_t r = 0; r < count; ++r) {
                input.read((char *)&wayid, sizeof(wayid));
                d->roads.european[Private::europeanRoadNumberToIndex(d->EuropeanRoadNumbers[i])].push_back(wayid);
            }
        }
    } else
        Error::warn("Expected 'E', got '0x%02x'", chr);

    input.read((char *)&chr, sizeof(chr));
    if (chr == 'R') {
        uint16_t road;
        input.read((char *)&road, sizeof(road));
        if (road >= Sweden::Private::national_len)
            Error::err("Road number %d is larger than Sweden::Private::national_len=%d", road, Sweden::Private::national_len);
        while (road != Private::terminator16bit) {
            size_t count;
            input.read((char *)&count, sizeof(count));
            if (count > reasonableLargeSizeT)
                Error::err("Count %ld looks unrealistically large", count);
            uint64_t wayid;
            for (size_t r = 0; r < count; ++r) {
                input.read((char *)&wayid, sizeof(wayid));
                d->roads.national[road].push_back(wayid);
            }

            input.read((char *)&road, sizeof(road));
        }
    } else
        Error::warn("Expected 'R', got '0x%02x'", chr);

    input.read((char *)&chr, sizeof(chr));
    if (chr == 'L') {
        size_t region;
        input.read((char *)&region, sizeof(region));
        if (region != Private::terminatorSizeT && region >= Sweden::Private::regional_len)
            Error::err("Region %ld looks unrealistically large or is larger than Sweden::Private::regional_len=%d", region, Sweden::Private::regional_len);
        while (region != Private::terminatorSizeT) {
            d->roads.regional[region] = (std::vector<uint64_t> ** *)calloc(Private::regional_outer_len, sizeof(std::vector<uint64_t> **));
            size_t a;
            input.read((char *)&a, sizeof(a));
            if (a != Private::terminatorSizeT && a >= Sweden::Private::regional_outer_len)
                Error::err("Variable a=%ld looks unrealistically large or is larger than Sweden::Private::regional_outer_len=%d", a, Sweden::Private::regional_outer_len);
            while (a != Private::terminatorSizeT) {
                d->roads.regional[region][a] = (std::vector<uint64_t> **)calloc(Private::regional_inner_len, sizeof(std::vector<uint64_t> *));
                size_t b;
                input.read((char *)&b, sizeof(b));
                if (b != Private::terminatorSizeT && b >= Sweden::Private::regional_inner_len)
                    Error::err("Variable b=%ld looks unrealistically large or is larger than Sweden::Private::regional_inner_len=%d", b, Sweden::Private::regional_inner_len);
                while (b != Private::terminatorSizeT) {
                    d->roads.regional[region][a][b] = new std::vector<uint64_t>();
                    size_t count;
                    input.read((char *)&count, sizeof(count));
                    if (count > reasonableLargeSizeT)
                        Error::err("Count %ld looks unrealistically large", count);
                    uint64_t wayid;
                    for (size_t r = 0; r < count; ++r) {
                        input.read((char *)&wayid, sizeof(wayid));
                        d->roads.regional[region][a][b]->push_back(wayid);
                    }

                    input.read((char *)&b, sizeof(b));
                    if (b != Private::terminatorSizeT && b >= Sweden::Private::regional_inner_len)
                        Error::err("Variable b=%ld looks unrealistically large or is larger than Sweden::Private::regional_inner_len=%d", b, Sweden::Private::regional_inner_len);
                }

                input.read((char *)&a, sizeof(a));
                if (a != Private::terminatorSizeT && a >= Sweden::Private::regional_outer_len)
                    Error::err("Variable a=%ld looks unrealistically large or is larger than Sweden::Private::regional_outer_len=%d", a, Sweden::Private::regional_outer_len);
            }

            input.read((char *)&region, sizeof(region));
            if (region != Private::terminatorSizeT && region >= Sweden::Private::regional_len)
                Error::err("Region %ld looks unrealistically large or is larger than Sweden::Private::regional_len=%d", region, Sweden::Private::regional_len);
        }
    } else
        Error::warn("Expected 'L', got '0x%02x'", chr);

    input.read((char *)&chr, sizeof(chr));
    if (chr != 'A')
        Error::warn("Expected 'A', got '0x%02x'", chr);
    d->administrativeRegion.read(input);

    input.read((char *)&chr, sizeof(chr));
    if (chr != '_')
        Error::warn("Expected '_', got '0x%02x'", chr);
}

Sweden::~Sweden()
{
    delete d;
}

void Sweden::test() {
    uint64_t id = 322746501;
    Coord coord;
    if (node2Coord->retrieve(id, coord)) {
        Error::info("node %llu is located at lat=%.5f (y=%d), lon=%.5f (x=%d)", id, coord.latitude(), coord.y, coord.longitude(), coord.x);
    }
    std::vector<int> scbcodes = insideSCBarea(id);
    if (scbcodes.empty())
        Error::warn("No SCB code found for node %llu", id);
    else if (scbcodes.front() == 2361) {
        Error::info("Found correct SCB code for node %llu which is %i", id, scbcodes.front());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    } else {
        Error::warn("Found SCB code for node %llu is %i, should be 2361 (%d codes in total)", id, scbcodes.front(), scbcodes.size());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    }
    // FIXME nuts

    id = 541187594;
    if (node2Coord->retrieve(id, coord)) {
        Error::info("node %llu is located at lat=%.5f (y=%d), lon=%.5f (x=%d)", id, coord.latitude(), coord.y, coord.longitude(), coord.x);
    }
    scbcodes = insideSCBarea(id);
    if (scbcodes.empty())
        Error::warn("No SCB code found for node %llu", id);
    else if (scbcodes.front() == 2034) {
        Error::info("Found correct SCB code for node %llu which is %i", id, scbcodes.front());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    } else {
        Error::warn("Found SCB code for node %llu is %i, should be 2034 (%d codes in total)", id, scbcodes.front(), scbcodes.size());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    }
    // FIXME nuts


    id = 3170517078;
    if (node2Coord->retrieve(id, coord)) {
        Error::info("node %llu is located at lat=%.5f (y=%d), lon=%.5f (x=%d)", id, coord.latitude(), coord.y, coord.longitude(), coord.x);
    }
    scbcodes = insideSCBarea(id);
    if (scbcodes.empty())
        Error::warn("No SCB code found for node %llu", id);
    else if (scbcodes.front() == 2161) {
        Error::info("Found correct SCB code for node %llu which is %i", id, scbcodes.front());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    } else {
        Error::warn("Found SCB code for node %llu is %i, should be 2161 (%d codes in total)", id, scbcodes.front(), scbcodes.size());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    }
    // FIXME nuts

    id = 3037352826;
    if (node2Coord->retrieve(id, coord)) {
        Error::info("node %llu is located at lat=%.5f (y=%d), lon=%.5f (x=%d)", id, coord.latitude(), coord.y, coord.longitude(), coord.x);
    }
    scbcodes = insideSCBarea(id);
    if (scbcodes.empty())
        Error::warn("No SCB code found for node %llu", id);
    else if (scbcodes.front() == 2583) {
        Error::info("Found correct SCB code for node %llu which is %i", id, scbcodes.front());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    } else {
        Error::warn("Found SCB code for node %llu is %i, should be 2583 (%d codes in total)", id, scbcodes.front(), scbcodes.size());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    }
    // FIXME nuts

    id = 3037352827;
    if (node2Coord->retrieve(id, coord)) {
        Error::info("node %llu is located at lat=%.5f (y=%d), lon=%.5f (x=%d)", id, coord.latitude(), coord.y, coord.longitude(), coord.x);
    }
    scbcodes = insideSCBarea(id);
    if (scbcodes.empty())
        Error::warn("No SCB code found for node %llu", id);
    else if (scbcodes.front() == 2518) {
        Error::info("Found correct SCB code for node %llu which is %i", id, scbcodes.front());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    } else {
        Error::warn("Found SCB code for node %llu is %i, should be 2518 (%d codes in total)", id, scbcodes.front(), scbcodes.size());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    }
    // FIXME nuts

    id = 3296599772;
    if (node2Coord->retrieve(id, coord)) {
        Error::info("node %llu is located at lat=%.5f (y=%d), lon=%.5f (x=%d)", id, coord.latitude(), coord.y, coord.longitude(), coord.x);
    }
    scbcodes = insideSCBarea(id);
    if (scbcodes.empty())
        Error::warn("No SCB code found for node %llu", id);
    else if (scbcodes.size() == 2 && ((scbcodes.front() == 2518 && scbcodes.back() == 2583) || (scbcodes.front() == 2583 && scbcodes.back() == 2518))) {
        Error::info("Found correct SCB codes for node %llu which are %i and %i", id, scbcodes.front(), scbcodes.back());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.back());
    } else {
        Error::warn("Found SCB code for node %llu is %i, should be 2518 and 2583", id, scbcodes.front());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    }
    // FIXME nuts

    id = 2005653590;
    if (node2Coord->retrieve(id, coord)) {
        Error::info("node %llu is located at lat=%.5f (y=%d), lon=%.5f (x=%d)", id, coord.latitude(), coord.y, coord.longitude(), coord.x);
    }
    scbcodes = insideSCBarea(id);
    if (scbcodes.empty())
        Error::warn("No SCB code found for node %llu", id);
    else if (scbcodes.size() == 2 && ((scbcodes.front() == 1880 && scbcodes.back() == 428) || (scbcodes.front() == 428 && scbcodes.back() == 1880))) {
        Error::info("Found correct SCB codes for node %llu which are %04i and %04i", id, scbcodes.front(), scbcodes.back());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.back());
    } else {
        Error::warn("Found SCB code for node %llu is %04i, should be 1880 and 0428", id, scbcodes.front());
        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcodes.front());
    }
    // FIXME nuts
}

std::ostream &Sweden::write(std::ostream &output) {
    char chr = 'S';
    output.write((char *)&chr, sizeof(chr));
    size_t num_elements = d->scbcode_to_relationid.size();
    output.write((char *)&num_elements, sizeof(num_elements));
    for (std::map<int, uint64_t>::const_iterator it = d->scbcode_to_relationid.cbegin(); it != d->scbcode_to_relationid.cend(); ++it) {
        output.write((char *) & ((*it).first), sizeof(int));
        output.write((char *) & ((*it).second), sizeof(uint64_t));
    }

    chr = 'n';
    output.write((char *)&chr, sizeof(chr));
    num_elements = d->nuts3code_to_relationid.size();
    output.write((char *)&num_elements, sizeof(num_elements));
    for (std::map<int, uint64_t>::const_iterator it = d->nuts3code_to_relationid.cbegin(); it != d->nuts3code_to_relationid.cend(); ++it) {
        output.write((char *) & ((*it).first), sizeof(int));
        output.write((char *) & ((*it).second), sizeof(uint64_t));
    }

    chr = 'E';
    output.write((char *)&chr, sizeof(chr));
    for (size_t i = 0; i < Private::EuropeanRoadNumbers.size(); ++i) {
        const size_t count = d->roads.european[Private::europeanRoadNumberToIndex(d->EuropeanRoadNumbers[i])].size();
        output.write((char *) &count, sizeof(count));
        for (size_t r = 0; r < count; ++r) {
            const uint64_t wayid = d->roads.european[Private::europeanRoadNumberToIndex(d->EuropeanRoadNumbers[i])][r];
            output.write((char *) &wayid, sizeof(uint64_t));
        }
    }

    chr = 'R';
    output.write((char *)&chr, sizeof(chr));
    for (uint16_t i = 0; i < Private::national_len; ++i)
        if (d->roads.national[i].empty()) continue;
        else {
            output.write((char *) &i, sizeof(i));
            const size_t count = d->roads.national[i].size();
            output.write((char *) &count, sizeof(count));
            for (size_t r = 0; r < count; ++r) {
                output.write((char *) &d->roads.national[i][r], sizeof(uint64_t));
            }
        }
    output.write((char *) &Private::terminator16bit, sizeof(Private::terminator16bit));

    chr = 'L';
    output.write((char *)&chr, sizeof(chr));
    for (size_t l = 0; l < Private::regional_len; ++l)
        if (d->roads.regional[l] == NULL) continue;
        else
        {
            output.write((char *) &l, sizeof(l));
            for (size_t a = 0; a < Private::regional_outer_len; ++a)
                if (d->roads.regional[l][a] == NULL) continue;
                else
                {
                    output.write((char *) &a, sizeof(a));
                    for (size_t b = 0; b < Private::regional_inner_len; ++b)
                        if (d->roads.regional[l][a][b] == NULL) continue;
                        else
                        {
                            output.write((char *) &b, sizeof(b));
                            const size_t count = d->roads.regional[l][a][b]->size();
                            output.write((char *) &count, sizeof(count));
                            for (size_t r = 0; r < count; ++r) {
                                output.write((char *) &d->roads.regional[l][a][b]->at(r), sizeof(uint64_t));
                            }
                        }
                    output.write((char *) &Private::terminatorSizeT, sizeof(Private::terminatorSizeT));
                }
            output.write((char *) &Private::terminatorSizeT, sizeof(Private::terminatorSizeT));
        }
    output.write((char *) &Private::terminatorSizeT, sizeof(Private::terminatorSizeT));

    chr = 'A';
    output.write((char *)&chr, sizeof(chr));
    d->administrativeRegion.write(output);

    chr = '_';
    output.write((char *)&chr, sizeof(chr));

    return output;
}

bool Sweden::nodeInsideRelationRegion(uint64_t nodeId, uint64_t relationId) {
    return d->nodeInsideRelationRegion(nodeId, relationId);
}

void Sweden::insertSCBarea(const int code, uint64_t relid) {
    d->scbcode_to_relationid.insert(std::pair<int, uint64_t>(code, relid));
}

std::vector<int> Sweden::insideSCBarea(const Coord &coord) {
    std::vector<int> result;
    for (auto it = d->scbcode_to_relationid.cbegin(); it != d->scbcode_to_relationid.cend(); ++it) {
        if (d->nodeInsideRelationRegion(coord, it->second))
            result.push_back(it->first);
    }

    return result;
}

std::vector<int> Sweden::insideSCBarea(uint64_t nodeid) {
    std::vector<int> result;
    for (auto it = d->scbcode_to_relationid.cbegin(); it != d->scbcode_to_relationid.cend(); ++it) {
        if (d->nodeInsideRelationRegion(nodeid, it->second))
            result.push_back(it->first);
    }

    return result;
}

Sweden::RoadType Sweden::roadTypeForSCBarea(int scbarea) {
    switch (scbarea / 100) {
    case 10: return RoadType::LanK;
    case 20: return RoadType::LanW;
    case 9: return RoadType::LanI;
    case 21: return RoadType::LanX;
    case 13: return RoadType::LanN;
    case 23: return RoadType::LanZ;
    case 6: return RoadType::LanF;
    case 8: return RoadType::LanH;
    case 7: return RoadType::LanG;
    case 25: return RoadType::LanBD;
    case 12: return RoadType::LanM;
    case 1: return RoadType::LanAB;
    case 4: return RoadType::LanD;
    case 3: return RoadType::LanC;
    case 17: return RoadType::LanS;
    case 24: return RoadType::LanAC;
    case 22: return RoadType::LanY;
    case 19: return RoadType::LanU;
    case 14: return RoadType::LanO;
    case 18: return RoadType::LanT;
    case 5: return RoadType::LanE;
    default:
        return RoadType::LanUnknown;
    }
}

std::string Sweden::nameOfSCBarea(int scbarea) {
    switch (scbarea) {
    /** Regional codes */
    case  1: return "Stockholm L\xc3\xa4n";
    case  3: return "Uppsala L\xc3\xa4n";
    case  4: return "S\xc3\xb6""dermanlands L\xc3\xa4n";
    case  5: return "\xc3\xb6sterg\xc3\xb6tlands L\xc3\xa4n";
    case  6: return "J\xc3\xb6nk\xc3\xb6pings L\xc3\xa4n";
    case  7: return "Kronobergs L\xc3\xa4n";
    case  8: return "Kalmar L\xc3\xa4n";
    case  9: return "Gotlands L\xc3\xa4n";
    case 10: return "Blekinge L\xc3\xa4n";
    case 12: return "Sk\xc3\xa5ne L\xc3\xa4n";
    case 13: return "Hallands L\xc3\xa4n";
    case 14: return "V\xc3\xa4stra G\xc3\xb6talands L\xc3\xa4n";
    case 17: return "V\xc3\xa4rmlands L\xc3\xa4n";
    case 18: return "\xc3\xb6rebro L\xc3\xa4n";
    case 19: return "V\xc3\xa4stmanlands L\xc3\xa4n";
    case 20: return "Dalarnas L\xc3\xa4n";
    case 21: return "G\xc3\xa4vleborgs L\xc3\xa4n";
    case 22: return "V\xc3\xa4sternorrlands L\xc3\xa4n";
    case 23: return "J\xc3\xa4mtlands L\xc3\xa4n";
    case 24: return "V\xc3\xa4sterbottens L\xc3\xa4n";
    case 25: return "Norrbottens L\xc3\xa4n";
    /** Municipality codes */
    case 1440: return "Ale";
    case 1489: return "Alings\xc3\xa5s";
    case  764: return "Alvesta";
    case  604: return "Aneby";
    case 1984: return "Arboga";
    case 2506: return "Arjeplog";
    case 2505: return "Arvidsjaur";
    case 1784: return "Arvika";
    case 1882: return "Askersund";
    case 2084: return "Avesta";
    case 1460: return "Bengtsfors";
    case 2326: return "Berg";
    case 2403: return "Bjurholm";
    case 1260: return "Bjuv";
    case 2582: return "Boden";
    case 1443: return "Bollebygd";
    case 2183: return "Bolln\xc3\xa4s";
    case  885: return "Borgholm";
    case 2081: return "Borl\xc3\xa4nge";
    case 1490: return "Bor\xc3\xa5s";
    case  127: return "Botkyrka";
    case  560: return "Boxholm";
    case 1272: return "Brom\xc3\xb6lla";
    case 2305: return "Br\xc3\xa4""cke";
    case 1231: return "Burl\xc3\xb6v";
    case 1278: return "B\xc3\xa5stad";
    case 1438: return "Dals-Ed";
    case  162: return "Danderyd";
    case 1862: return "Degerfors";
    case 2425: return "Dorotea";
    case 1730: return "Eda";
    case  125: return "Eker\xc3\xb6";
    case  686: return "Eksj\xc3\xb6";
    case  862: return "Emmaboda";
    case  381: return "Enk\xc3\xb6ping";
    case  484: return "Eskilstuna";
    case 1285: return "Esl\xc3\xb6v";
    case 1445: return "Essunga";
    case 1982: return "Fagersta";
    case 1382: return "Falkenberg";
    case 1499: return "Falk\xc3\xb6ping";
    case 2080: return "Falun";
    case 1782: return "Filipstad";
    case  562: return "Finsp\xc3\xa5ng";
    case  482: return "Flen";
    case 1763: return "Forshaga";
    case 1439: return "F\xc3\xa4rgelanda";
    case 2026: return "Gagnef";
    case  662: return "Gislaved";
    case  461: return "Gnesta";
    case  617: return "Gnosj\xc3\xb6";
    case  980: return "Gotland";
    case 1764: return "Grums";
    case 1444: return "Gr\xc3\xa4storp";
    case 1447: return "Gullsp\xc3\xa5ng";
    case 2523: return "G\xc3\xa4llivare";
    case 2180: return "G\xc3\xa4vle";
    case 1480: return "G\xc3\xb6teborg";
    case 1471: return "G\xc3\xb6tene";
    case  643: return "Habo";
    case 1783: return "Hagfors";
    case 1861: return "Hallsberg";
    case 1961: return "Hallstahammar";
    case 1380: return "Halmstad";
    case 1761: return "Hammar\xc3\xb6";
    case  136: return "Haninge";
    case 2583: return "Haparanda";
    case  331: return "Heby";
    case 2083: return "Hedemora";
    case 1283: return "Helsingborg";
    case 1466: return "Herrljunga";
    case 1497: return "Hjo";
    case 2104: return "Hofors";
    case  126: return "Huddinge";
    case 2184: return "Hudiksvall";
    case  860: return "Hultsfred";
    case 1315: return "Hylte";
    case  305: return "H\xc3\xa5""bo";
    case 1863: return "H\xc3\xa4llefors";
    case 2361: return "H\xc3\xa4rjedalen";
    case 2280: return "H\xc3\xa4rn\xc3\xb6sand";
    case 1401: return "H\xc3\xa4rryda";
    case 1293: return "H\xc3\xa4ssleholm";
    case 1284: return "H\xc3\xb6gan\xc3\xa4s";
    case  821: return "H\xc3\xb6gsby";
    case 1266: return "H\xc3\xb6rby";
    case 1267: return "H\xc3\xb6\xc3\xb6r";
    case 2510: return "Jokkmokk";
    case  123: return "J\xc3\xa4rf\xc3\xa4lla";
    case  680: return "J\xc3\xb6nk\xc3\xb6ping";
    case 2514: return "Kalix";
    case  880: return "Kalmar";
    case 1446: return "Karlsborg";
    case 1082: return "Karlshamn";
    case 1883: return "Karlskoga";
    case 1080: return "Karlskrona";
    case 1780: return "Karlstad";
    case  483: return "Katrineholm";
    case 1715: return "Kil";
    case  513: return "Kinda";
    case 2584: return "Kiruna";
    case 1276: return "Klippan";
    case  330: return "Knivsta";
    case 2282: return "Kramfors";
    case 1290: return "Kristianstad";
    case 1781: return "Kristinehamn";
    case 2309: return "Krokom";
    case 1881: return "Kumla";
    case 1384: return "Kungsbacka";
    case 1960: return "Kungs\xc3\xb6r";
    case 1482: return "Kung\xc3\xa4lv";
    case 1261: return "K\xc3\xa4vlinge";
    case 1983: return "K\xc3\xb6ping";
    case 1381: return "Laholm";
    case 1282: return "Landskrona";
    case 1860: return "Lax\xc3\xa5";
    case 1814: return "Lekeberg";
    case 2029: return "Leksand";
    case 1441: return "Lerum";
    case  761: return "Lessebo";
    case  186: return "Liding\xc3\xb6";
    case 1494: return "Lidk\xc3\xb6ping";
    case 1462: return "Lilla Edet";
    case 1885: return "Lindesberg";
    case  580: return "Link\xc3\xb6ping";
    case  781: return "Ljungby";
    case 2161: return "Ljusdal";
    case 1864: return "Ljusnarsberg";
    case 1262: return "Lomma";
    case 2085: return "Ludvika";
    case 2580: return "Lule\xc3\xa5";
    case 1281: return "Lund";
    case 2481: return "Lycksele";
    case 1484: return "Lysekil";
    case 1280: return "Malm\xc3\xb6";
    case 2023: return "Malung";
    case 2418: return "Mal\xc3\xa5";
    case 1493: return "Mariestad";
    case 1463: return "Mark";
    case  767: return "Markaryd";
    case 1461: return "Mellerud";
    case  586: return "Mj\xc3\xb6lby";
    case 2062: return "Mora";
    case  583: return "Motala";
    case  642: return "Mullsj\xc3\xb6";
    case 1430: return "Munkedal";
    case 1762: return "Munkfors";
    case 1481: return "M\xc3\xb6lndal";
    case  861: return "M\xc3\xb6nster\xc3\xa5s";
    case  840: return "M\xc3\xb6rbyl\xc3\xa5nga";
    case  182: return "Nacka";
    case 1962: return "Norberg";
    case 1884: return "Nora";
    case 2132: return "Nordanstig";
    case 2401: return "Nordmaling";
    case  581: return "Norrk\xc3\xb6ping";
    case  188: return "Norrt\xc3\xa4lje";
    case 2417: return "Norsj\xc3\xb6";
    case  881: return "Nybro";
    case  140: return "Nykvarn";
    case  480: return "Nyk\xc3\xb6ping";
    case  192: return "Nyn\xc3\xa4shamn";
    case  682: return "N\xc3\xa4ssj\xc3\xb6";
    case 2101: return "Ockelbo";
    case 1060: return "Olofstr\xc3\xb6m";
    case 2034: return "Orsa";
    case 1421: return "Orust";
    case 1273: return "Osby";
    case  882: return "Oskarshamn";
    case 2121: return "Ovan\xc3\xa5ker";
    case  481: return "Oxel\xc3\xb6sund";
    case 2521: return "Pajala";
    case 1402: return "Partille";
    case 1275: return "Perstorp";
    case 2581: return "Pite\xc3\xa5";
    case 2303: return "Ragunda";
    case 2409: return "Robertsfors";
    case 1081: return "Ronneby";
    case 2031: return "R\xc3\xa4ttvik";
    case 1981: return "Sala";
    case  128: return "Salem";
    case 2181: return "Sandviken";
    case  191: return "Sigtuna";
    case 1291: return "Simrishamn";
    case 1265: return "Sj\xc3\xb6""bo";
    case 1495: return "Skara";
    case 2482: return "Skellefte\xc3\xa5";
    case 1904: return "Skinnskatteberg";
    case 1264: return "Skurup";
    case 1496: return "Sk\xc3\xb6vde";
    case 2061: return "Smedjebacken";
    case 2283: return "Sollefte\xc3\xa5";
    case  163: return "Sollentuna";
    case  184: return "Solna";
    case 2422: return "Sorsele";
    case 1427: return "Soten\xc3\xa4s";
    case 1230: return "Staffanstorp";
    case 1415: return "Stenungsund";
    case  180: return "Stockholm";
    case 1760: return "Storfors";
    case 2421: return "Storuman";
    case  486: return "Str\xc3\xa4ngn\xc3\xa4s";
    case 1486: return "Str\xc3\xb6mstad";
    case 2313: return "Str\xc3\xb6msund";
    case  183: return "Sundbyberg";
    case 2281: return "Sundsvall";
    case 1766: return "Sunne";
    case 1907: return "Surahammar";
    case 1214: return "Sval\xc3\xb6v";
    case 1263: return "Svedala";
    case 1465: return "Svenljunga";
    case 1785: return "S\xc3\xa4""ffle";
    case 2082: return "S\xc3\xa4ter";
    case  684: return "S\xc3\xa4vsj\xc3\xb6";
    case 2182: return "S\xc3\xb6""derhamn";
    case  582: return "S\xc3\xb6""derk\xc3\xb6ping";
    case  181: return "S\xc3\xb6""dert\xc3\xa4lje";
    case 1083: return "S\xc3\xb6lvesborg";
    case 1435: return "Tanum";
    case 1472: return "Tibro";
    case 1498: return "Tidaholm";
    case  360: return "Tierp";
    case 2262: return "Timr\xc3\xa5";
    case  763: return "Tingsryd";
    case 1419: return "Tj\xc3\xb6rn";
    case 1270: return "Tomelilla";
    case 1737: return "Torsby";
    case  834: return "Tors\xc3\xa5s";
    case 1452: return "Tranemo";
    case  687: return "Tran\xc3\xa5s";
    case 1287: return "Trelleborg";
    case 1488: return "Trollh\xc3\xa4ttan";
    case  488: return "Trosa";
    case  138: return "Tyres\xc3\xb6";
    case  160: return "T\xc3\xa4""by";
    case 1473: return "T\xc3\xb6reboda";
    case 1485: return "Uddevalla";
    case 1491: return "Ulricehamn";
    case 2480: return "Ume\xc3\xa5";
    case  139: return "Upplands-Bro";
    case  114: return "Upplands V\xc3\xa4sby";
    case  380: return "Uppsala";
    case  760: return "Uppvidinge";
    case  584: return "Vadstena";
    case  665: return "Vaggeryd";
    case  563: return "Valdemarsvik";
    case  115: return "Vallentuna";
    case 2021: return "Vansbro";
    case 1470: return "Vara";
    case 1383: return "Varberg";
    case  187: return "Vaxholm";
    case 1233: return "Vellinge";
    case  685: return "Vetlanda";
    case 2462: return "Vilhelmina";
    case  884: return "Vimmerby";
    case 2404: return "Vindeln";
    case  428: return "Ving\xc3\xa5ker";
    case 1442: return "V\xc3\xa5rg\xc3\xa5rda";
    case 1487: return "V\xc3\xa4nersborg";
    case 2460: return "V\xc3\xa4nn\xc3\xa4s";
    case  120: return "V\xc3\xa4rmd\xc3\xb6";
    case  683: return "V\xc3\xa4rnamo";
    case  883: return "V\xc3\xa4stervik";
    case 1980: return "V\xc3\xa4ster\xc3\xa5s";
    case  780: return "V\xc3\xa4xj\xc3\xb6";
    case  512: return "Ydre";
    case 1286: return "Ystad";
    case 1492: return "\xc3\x85m\xc3\xa5l";
    case 2260: return "\xc3\x85nge";
    case 2321: return "\xc3\x85re";
    case 1765: return "\xc3\x85rj\xc3\xa4ng";
    case 2463: return "\xc3\x85sele";
    case 1277: return "\xc3\x85storp";
    case  561: return "\xc3\x85tvidaberg";
    case  765: return "\xc3\x84lmhult";
    case 2039: return "\xc3\x84lvdalen";
    case  319: return "\xc3\x84lvkarleby";
    case 2560: return "\xc3\x84lvsbyn";
    case 1292: return "\xc3\x84ngelholm";
    case 1407: return "\xc3\x96""cker\xc3\xb6";
    case  509: return "\xc3\x96""desh\xc3\xb6g";
    case 1880: return "\xc3\x96rebro";
    case 1257: return "\xc3\x96rkelljunga";
    case 2284: return "\xc3\x96rnsk\xc3\xb6ldsvik";
    case 2380: return "\xc3\x96stersund";
    case  117: return "\xc3\x96ster\xc3\xa5ker";
    case  382: return "\xc3\x96sthammar";
    case 1256: return "\xc3\x96stra G\xc3\xb6inge";
    case 2513: return "\xc3\x96verkalix";
    case 2518: return "\xc3\x96vertorne\xc3\xa5";
    }

    return std::string();
}

void Sweden::insertNUTS3area(const int code, uint64_t relid) {
    d->nuts3code_to_relationid.insert(std::pair<int, uint64_t>(code, relid));
}

std::vector<int> Sweden::insideNUTS3area(uint64_t nodeid) {
    std::vector<int> result;
    for (auto it = d->nuts3code_to_relationid.cbegin(); it != d->nuts3code_to_relationid.cend(); ++it) {
        if (d->nodeInsideRelationRegion(nodeid, it->second))
            result.push_back(it->first);
    }

    return result;
}

void Sweden::drawSCBareas(const std::string &filename) {
    d->drawArea(filename, d->scbcode_to_relationid);
}

void Sweden::drawSCBareas(SvgWriter &svgWriter) {
    d->drawArea(svgWriter, d->scbcode_to_relationid);
}

void Sweden::drawRoads(SvgWriter &svgWriter) {
    WayNodes wn;
    Coord c;
    std::vector<int> x, y;
    char buffer[STRING_BUFFER_SIZE];

    /// European roads
    for (size_t i = 0; i < Private::EuropeanRoadNumbers.size(); ++i) {
        const size_t count = d->roads.european[Private::europeanRoadNumberToIndex(d->EuropeanRoadNumbers[i])].size();
        for (size_t r = 0; r < count; ++r) {
            x.clear();
            y.clear();
            const uint64_t wayid = d->roads.european[Private::europeanRoadNumberToIndex(d->EuropeanRoadNumbers[i])][r];
            if (wayNodes->retrieve(wayid, wn)) {
                for (uint32_t n = 0; n < wn.num_nodes; ++n)
                    if (node2Coord->retrieve(wn.nodes[n], c)) {
                        x.push_back(c.x);
                        y.push_back(c.y);
                    }
                snprintf(buffer, STRING_BUFFER_SIZE, "E%d  segm %lu of %lu with %d nodes, way id %lu", d->EuropeanRoadNumbers[i], r, count, wn.num_nodes, wayid);
                svgWriter.drawRoad(x, y, SvgWriter::RoadMajorImportance, std::string(buffer));
            }
        }
    }

    /// National roads
    for (size_t i = 0; i < Private::national_len; ++i)
        if (!d->roads.national[i].empty())
            for (int j = d->roads.national[i].size() - 1; j >= 0; --j) {
                const uint64_t wayid = d->roads.national[i][j];
                if (wayNodes->retrieve(wayid, wn)) {
                    x.clear();
                    y.clear();
                    for (uint32_t n = 0; n < wn.num_nodes; ++n)
                        if (node2Coord->retrieve(wn.nodes[n], c)) {
                            x.push_back(c.x);
                            y.push_back(c.y);
                        }
                    snprintf(buffer, STRING_BUFFER_SIZE, "R%lu  segm %d of %lu with %d nodes, way id %lu", i, j, d->roads.national[i].size(), wn.num_nodes, wayid);
                    svgWriter.drawRoad(x, y, SvgWriter::RoadAvgImportance, std::string(buffer));
                }
            }

    /// Omit regional roads, too many details
}

void Sweden::insertWayAsRoad(uint64_t wayid, const char *refValue) {
    const char *cur = refValue;

    while (*cur != '\0') {
        while (*cur == ' ') ++cur; ///< Skip spaces

        RoadType roadType = National;
        if (cur[0] == 'E' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = Europe;///< or LanE, needs more testing further down
            cur += 2;
        } else if (cur[0] == 'M' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanM;
            cur += 2;
        } else if (cur[0] == 'K' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanM;
            cur += 2;
        } else if (cur[0] == 'K' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanM;
            cur += 2;
        } else if (cur[0] == 'I' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanI;
            cur += 2;
        } else if (cur[0] == 'H' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanH;
            cur += 2;
        } else if (cur[0] == 'G' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanG;
            cur += 2;
        } else if (cur[0] == 'N' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanN;
            cur += 2;
        } else if (cur[0] == 'O' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanO;
            cur += 2;
        } else if (cur[0] == 'F' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanF;
            cur += 2;
        }
        /// LanE must be checked based on road number,
        /// as letter E is used for European roads as well
        else if (cur[0] == 'D' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanD;
            cur += 2;
        } else if (cur[0] == 'A' && cur[1] == 'B' && cur[2] == ' ' && cur[3] >= '1' && cur[3] <= '9') {
            roadType = LanAB;
            cur += 3;
        } else if (cur[0] == 'C' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanC;
            cur += 2;
        } else if (cur[0] == 'U' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanU;
            cur += 2;
        } else if (cur[0] == 'T' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanT;
            cur += 2;
        } else if (cur[0] == 'S' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanS;
            cur += 2;
        } else if (cur[0] == 'W' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanW;
            cur += 2;
        } else if (cur[0] == 'X' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanX;
            cur += 2;
        } else if (cur[0] == 'Z' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanZ;
            cur += 2;
        } else if (cur[0] == 'Y' && cur[1] == ' ' && cur[2] >= '1' && cur[2] <= '9') {
            roadType = LanY;
            cur += 2;
        } else if (cur[0] == 'A' && cur[1] == 'C' && cur[2] == ' ' && cur[3] >= '1' && cur[3] <= '9') {
            roadType = LanAC;
            cur += 3;
        } else if (cur[0] == 'B' && cur[1] == 'D' && cur[2] == ' ' && cur[3] >= '1' && cur[3] <= '9') {
            roadType = LanBD;
            cur += 3;
        } else if (cur[0] < '1' || cur[0] > '9')
            return;

        char *next;
        int roadNumber = (int)strtol(cur, &next, 10);
        if (roadNumber > 0 && next > cur) {
            if (roadType == LanE || roadType == Europe) {
                /// Handle the case that E may be used for European roads
                /// or roads in East Gothland
                roadType = identifyEroad(roadNumber);
            } else if (roadType == National && roadNumber >= 500)
                roadType = LanUnknown;
            insertWayAsRoad(wayid, roadType, roadNumber);
        }
        cur = next;
        if (*cur == ';' || *cur == ',')
            /// Multiple road numbers may be separated by a semicolor
            /// or komma (non-standard)
            ++cur;
        else if (*cur == '.') {
            /// Link road like E4.04, record as 'E4' only
            while (*cur == '.' || *cur == ';' || *cur == ',' || *cur == ' ' || (*cur >= '0' && *cur <= '9')) ++cur;
        } else
            break;
    }
}

void Sweden::insertWayAsRoad(uint64_t wayid, RoadType roadType, uint16_t roadNumber) {
    /// Some way ids should be ignored, e.g. (1) those right outside of Sweden
    /// which just happend to be included in the map data or (2) where OSM data
    /// is most likely wrong (some 5xxx roads in central Linköping).
    static const uint64_t blacklistedWayIds[] = {1648176, 1648475, 1651992, 2954124, 4605570, 8150233, 23275365, 23444292, 24040916, 24731243, 24786276, 27872415, 27872417, 27872418, 29054792, 29054793, 30784964, 30887520, 34419027, 34419029, 38227481, 38564589, 38564590, 44141405, 44298775, 45329454, 45876899, 46931166, 48386475, 51381476, 51385960, 59065373, 59065380, 59065382, 59065388, 61380105, 67171996, 69358305, 73854172, 80360747, 116831322, 138003259, 146294832, 180751968, 194028774, 229700851, 308918468, 308918469, 321318578, 324044848, 324093732, 324271180, 324492881, 324492887, 326365472, 345614344, 345614345, 347763180, 347763181, 347763182, 347763184, 347763185, 347763186, 347763188, 366707779, 375573546, 375573548, 383462866, 399732015, 402989392, 0};
    for (int i = 0; blacklistedWayIds[i] > 0; ++i)
        if (wayid == blacklistedWayIds[i]) return;
    /// In Sundsvall, there are a few 'regional roads' with numbers 5300-5399,
    /// not sure if that is a mistake, ignoring those.
    if (roadNumber >= 5300 && roadNumber < 5400 && (roadType == LanUnknown || roadType == LanY))
        return;

    /// Check for invalid parameters
    if (wayid == 0 || roadType >= UnknownRoadType || roadNumber > 9999) {
        Error::warn("Combination of way id %llu, road number %d, and road type %d (%s) is invalid", wayid, roadNumber, roadType, roadTypeToString(roadType).c_str());
        return;
    }

    switch (roadType) {
    case Europe:
        d->roads.european[Private::europeanRoadNumberToIndex(roadNumber)].push_back(wayid);
        break;
    case National:
        if (roadNumber < Private::national_len)
            /// National roads ('riksvägar') have numbers 1 to 99
            /// Primary regional roads ('primära länsvägar') have numbers 100 to 499,
            /// but no letters, therefore counted as national roads in this context
            d->roads.national[roadNumber].push_back(wayid);
        else
            Error::warn("Road number %d is %d or larger, but no regional code/letter given for way https://www.openstreetmap.org/way/%lu", roadNumber, Private::national_len, wayid);
        break;
    default:
    {
        const int idx = (int)roadType - 2;
        if (idx >= 0 && (size_t)idx < Private::regional_len && roadNumber < Private::regional_outer_len * Private::regional_inner_len) {
            if (d->roads.regional[idx] == NULL)
                d->roads.regional[idx] = (std::vector<uint64_t> ** *)calloc(Private::regional_outer_len, sizeof(std::vector<uint64_t> **));
            const int firstIndex = roadNumber / Private::regional_inner_len, secondIndex = roadNumber % Private::regional_inner_len;
            if (d->roads.regional[idx][firstIndex] == NULL)
                d->roads.regional[idx][firstIndex] = (std::vector<uint64_t> **)calloc(Private::regional_inner_len, sizeof(std::vector<uint64_t> *));
            if (d->roads.regional[idx][firstIndex][secondIndex] == NULL)
                d->roads.regional[idx][firstIndex][secondIndex] = new std::vector<uint64_t>();
            d->roads.regional[idx][firstIndex][secondIndex]->push_back(wayid);
        } else
            Error::warn("Combination of way id %llu, road number %d, and road type %d (%s) is invalid", wayid, roadNumber, roadType, roadTypeToString(roadType).c_str());
    }
    }
}

std::vector<uint64_t> Sweden::waysForRoad(RoadType roadType, uint16_t roadNumber) {
    if (roadNumber <= 0 || roadType >= UnknownRoadType)
        return std::vector<uint64_t>();

    switch (roadType) {
    case Europe:
        if (roadNumber < Private::european_len)
            return d->roads.european[Private::europeanRoadNumberToIndex(roadNumber)];
        break;
    case National:
        if (roadNumber < Private::national_len)
            return d->roads.national[roadNumber];
        break;
    default:
    {
        const int idx = (int)roadType - 2;
        const size_t firstIndex = roadNumber / Private::regional_inner_len, secondIndex = roadNumber % Private::regional_inner_len;
        if (idx >= 0 && (size_t)idx < Private::regional_len && firstIndex < Private::regional_outer_len && d->roads.regional[idx] != NULL &&  d->roads.regional[idx][firstIndex] != NULL && d->roads.regional[idx][firstIndex][secondIndex] != NULL)
            return *d->roads.regional[idx][firstIndex][secondIndex];
    }
    }

    return std::vector<uint64_t>();
}

std::string Sweden::roadTypeToString(Sweden::RoadType roadType) {
    switch (roadType) {
    case Europe: return std::string("E");
    case National: return std::string("Nat");
    case LanM: return std::string("M");
    case LanK: return std::string("K");
    case LanI: return std::string("I");
    case LanH: return std::string("H");
    case LanG: return std::string("G");
    case LanN: return std::string("N");
    case LanO: return std::string("O");
    case LanF: return std::string("F");
    case LanE: return std::string("E");
    case LanD: return std::string("D");
    case LanAB: return std::string("AB");
    case LanC: return std::string("C");
    case LanU: return std::string("U");
    case LanT: return std::string("T");
    case LanS: return std::string("S");
    case LanW: return std::string("W");
    case LanX: return std::string("X");
    case LanZ: return std::string("Z");
    case LanY: return std::string("Y");
    case LanAC: return std::string("AC");
    case LanBD: return std::string("BD");
    case LanUnknown: return std::string("Reg");
    default: return std::string("???");
    }
}

Sweden::RoadType Sweden::identifyEroad(uint16_t roadNumber) {
    for (std::vector<int>::size_type i = 0; i < Private::EuropeanRoadNumbers.size(); ++i)
        if (Private::EuropeanRoadNumbers[i] == roadNumber)
            return Europe;
    return LanE;
}

Sweden::RoadType Sweden::closestRoadNodeToCoord(int x, int y, const Sweden::Road &road, uint64_t &bestNode, int &distance) const {
    distance = INT_MAX;
    bestNode = 0;

    std::vector<uint64_t> *wayIds = NULL;
    std::vector<int> lanStartingIndex(Private::regional_len, INT_MAX);

    if (road.number <= 0) return road.type; ///< Invalid road number
    if (road.type == UnknownRoadType) return UnknownRoadType; ///< No point in locating unknown road types (FIXME true?)

    switch (road.type) {
    case Europe:
        wayIds = &d->roads.european[Private::europeanRoadNumberToIndex(road.number)];
        break;
    case National:
        wayIds = &d->roads.national[road.number];
        break;
    case LanUnknown:
    {
        wayIds = new std::vector<uint64_t>();
        const size_t firstIndex = road.number / Private::regional_inner_len, secondIndex = road.number % Private::regional_inner_len;
        if (firstIndex < Private::regional_outer_len)
            for (size_t i = 0; i < Private::regional_len; ++i) {
                lanStartingIndex[i] = wayIds->size();
                if (d->roads.regional[i] != NULL && d->roads.regional[i][firstIndex] != NULL && d->roads.regional[i][firstIndex][secondIndex] != NULL)
                    wayIds->insert(wayIds->cend(), d->roads.regional[i][firstIndex][secondIndex]->cbegin(), d->roads.regional[i][firstIndex][secondIndex]->cend());
            }
        break;
    }
    default:
        const int idx = (int)road.type - 2;
        if (idx >= 0 && (size_t)idx < Private::regional_len && road.number > 0 && (size_t)road.number < Private::regional_outer_len * Private::regional_inner_len) {
            const int firstIndex = road.number / Private::regional_inner_len, secondIndex = road.number % Private::regional_inner_len;
            if (d->roads.regional[idx] != NULL && d->roads.regional[idx][firstIndex] != NULL && d->roads.regional[idx][firstIndex][secondIndex] != NULL)
                wayIds = d->roads.regional[idx][firstIndex][secondIndex];
        }
    }

    int bestNodeIndex = -1;
    if (wayIds != NULL) {
        bestNodeIndex = 0;
        int64_t minSqDistance = INT64_MAX;
        int i = 0;
        for (auto it = wayIds->cbegin(); it != wayIds->cend(); ++it, ++i) {
            const uint64_t &wayId = *it;
            uint64_t node = 0;
            int64_t sqDist = INT64_MAX;
            d->closestWayNodeToCoord(x, y, wayId, node, sqDist);
            if (sqDist < minSqDistance) {
                bestNodeIndex = i;
                minSqDistance = sqDist;
                bestNode = node;
            }
        }
        if (minSqDistance < INT64_MAX && bestNode > 0) {
            distance = sqrt(minSqDistance) / 10.0 + .5;
            Error::debug("Closest node of road %s %d to x=%d,y=%d is node %llu at distance %.1f km", roadTypeToString(road.type).c_str(), road.number, x, y, bestNode, distance / 1000.0);
        } else
            bestNode = 0;
    }

    if (road.type == LanUnknown && bestNodeIndex > -1) {
        if (wayIds != NULL) delete wayIds;
        for (size_t i = 0; i < Private::regional_len; ++i)
            if (lanStartingIndex[i] <= bestNodeIndex && (i == Private::regional_len - 1 || lanStartingIndex[i + 1] > bestNodeIndex))
                return (Sweden::RoadType)(i + 2);
        return Sweden::LanUnknown;
    } else
        return road.type;
}

std::vector<struct Sweden::Road> Sweden::identifyRoads(const std::vector<std::string> &words) const {
    static const std::string swedishWordRv("rv"); ///< as in Rv. 43
    static const std::string swedishWordWay("v\xc3\xa4g");
    static const std::string swedishWordTheWay("v\xc3\xa4gen");
    static const std::string swedishWordNationalWay("riksv\xc3\xa4g");
    static const std::string swedishWordTheNationalWay("riksv\xc3\xa4gen");
    static const std::string swedishWordCountyWay("l\xc3\xa4nsv\xc3\xa4g");
    static const std::string swedishWordTheCountyWay("l\xc3\xa4nsv\xc3\xa4gen");
    static const uint16_t invalidRoadNumber = 0;

    std::vector<struct Sweden::Road> result;

    /// For each element in 'words', i.e. each word ...
    for (size_t i = 0; i < words.size(); ++i) {
        /// Mark road number and type as unknown/unset
        uint16_t roadNumber = invalidRoadNumber;
        Sweden::RoadType roadType = Sweden::UnknownRoadType;

        /// Current word is one or two characters long and following word starts with digit 1 to 9
        if (i < words.size() - 1 && ((words[i][0] >= 'a' && words[i][0] <= 'z' && words[i][1] == '\0') || (words[i][0] >= 'a' && words[i][0] <= 'b' && words[i][1] >= 'a' && words[i][1] <= 'd' && words[i][2] == '\0')) && words[i + 1][0] >= '1' && words[i + 1][0] <= '9') {
            /// Get following word's numeric value as 'roadNumber'
            char *next;
            const char *cur = words[i + 1].c_str();
            roadNumber = (uint16_t)strtol(cur, &next, 10);
            /// If got a valid road number, try interpreting current word as road identifier
            if (roadNumber > 0 && next > cur)
                roadType = d->lettersToRoadType(words[i].c_str(), roadNumber);
            else {
                Error::debug("Not a road number: %s", cur);
                roadNumber = invalidRoadNumber;
            }
        }
        /// Current word starts with letter, followed by digit 1 to 9
        else if (words[i][0] >= 'a' && words[i][0] <= 'z' && words[i][1] >= '1' && words[i][1] <= '9') {
            /// Get numeric value of current word starting from second character as 'roadNumber'
            char *next;
            const char *cur = words[i].c_str() + 1;
            roadNumber = (uint16_t)strtol(cur, &next, 10);
            if (roadNumber > 0 && roadNumber < 9999 && next > cur) {
                /// If got a valid road number, try interpreting current word's first character as road identifier
                const char buffer[] = {words[i][0], '\0', '\0', '\0'};
                roadType = d->lettersToRoadType(buffer, roadNumber);
            } else {
                Error::debug("Not a road number: %s", cur);
                roadNumber = invalidRoadNumber;
            }
        }
        /// Current word starts with letter 'a' or 'b', followed by 'a'..'d', followed by digit 1 to 9
        else if (words[i][0] >= 'a' && words[i][0] <= 'b' && words[i][1] >= 'a' && words[i][1] <= 'd' && words[i][2] >= '1' && words[i][2] <= '9') {
            /// Get numeric value of current word starting from third character as 'roadNumber'
            char *next;
            const char *cur = words[i].c_str() + 2;
            roadNumber = (uint16_t)strtol(cur, &next, 10);
            if (roadNumber > 0 && next > cur) {
                /// If got a valid road number, try interpreting current word's first two characters as road identifier
                const char buffer[] = {words[i][0], words[i][1], '\0'};
                roadType = d->lettersToRoadType(buffer, roadNumber);
            } else {
                Error::debug("Not a road number: %s", cur);
                roadNumber = invalidRoadNumber;
            }
        }
        /// If current word looks like word describing a national road in Swedish and
        /// following word starts with digit 1 to 9 ...
        else if (i < words.size() - 1 && (swedishWordRv.compare(words[i]) == 0 || swedishWordWay.compare(words[i]) == 0 || swedishWordTheWay.compare(words[i]) == 0 || swedishWordNationalWay.compare(words[i]) == 0 || swedishWordTheNationalWay.compare(words[i]) == 0 || swedishWordCountyWay.compare(words[i]) == 0 || swedishWordTheCountyWay.compare(words[i]) == 0) && words[i + 1][0] >= '1' && words[i + 1][0] <= '9') {
            char *next;
            const char *cur = words[i + 1].c_str();
            roadNumber = (uint16_t)strtol(cur, &next, 10);
            if (roadNumber > 0 && next > cur) {
                /// Got a valid road number
                roadType = roadNumber < 500 ? Sweden::National : Sweden::LanUnknown;
            } else {
                Error::warn("Not a road number: %s", cur);
                roadNumber = invalidRoadNumber;
            }
        }

        if (roadNumber != invalidRoadNumber && roadType != Sweden::UnknownRoadType) {
            const Sweden::Road foundRoad(roadType, roadNumber);
#ifdef DEBUG
            Error::debug("Found road %s", foundRoad.operator std::string().c_str());
#endif // DEBUG

            /// Add only unique its to result list
            bool known = false;
            for (auto it = result.cbegin(); !known && it != result.cend(); ++it) {
                const Sweden::Road &road = *it;
                known = (road.type == roadType) && (road.number == roadNumber);
            }
            if (!known) result.push_back(foundRoad);
        }
    }

    return result;
}

void Sweden::fixUnlabeledRegionalRoads() {
    const int unknownLanIdx = (int)LanUnknown - 2;
    if (d->roads.regional[unknownLanIdx] != NULL) {
        for (size_t outer = 0; outer < Private::regional_outer_len; ++outer)
            if (d->roads.regional[unknownLanIdx][outer] != NULL) {
                for (size_t inner = 0; inner < Private::regional_inner_len; ++inner)
                    if (d->roads.regional[unknownLanIdx][outer][inner] != NULL) {
                        std::vector<uint64_t> *wayIds = d->roads.regional[unknownLanIdx][outer][inner];
                        for (auto it = wayIds->cbegin(); it != wayIds->cend();) {
                            WayNodes wn;
                            if (wayNodes->retrieve(*it, wn) && wn.num_nodes > 0) {
                                const uint64_t pivotNodeId = wn.nodes[wn.num_nodes / 2];
                                std::vector<int> scbAreas = insideSCBarea(pivotNodeId);
                                if (scbAreas.size() == 1) {
                                    const RoadType properLan = roadTypeForSCBarea(scbAreas.front());
                                    const int properLanIdx = (int)properLan - 2;
                                    if (d->roads.regional[properLanIdx] == NULL)
                                        d->roads.regional[properLanIdx] = (std::vector<uint64_t> ** *)calloc(Private::regional_outer_len, sizeof(std::vector<uint64_t> **));
                                    if (d->roads.regional[properLanIdx][outer] == NULL)
                                        d->roads.regional[properLanIdx][outer] = (std::vector<uint64_t> **)calloc(Private::regional_inner_len, sizeof(std::vector<uint64_t> *));
                                    if (d->roads.regional[properLanIdx][outer][inner] == NULL)
                                        d->roads.regional[properLanIdx][outer][inner] = new std::vector<uint64_t>();
                                    d->roads.regional[properLanIdx][outer][inner]->push_back(*it);

                                    Error::debug("Setting region %s to way %llu with road number %d", roadTypeToString(properLan).c_str(), *it, outer * Private::regional_inner_len + inner);

                                    it = wayIds->erase(it);
                                    continue;
                                }
                            }
                            /// No proper region found, keep road in "Unknown Lan"
                            ++it;
                        }

                        if (wayIds->empty()) {
                            delete wayIds;
                            d->roads.regional[unknownLanIdx][outer][inner] = NULL;
                        }
                    }

                bool allNull = true;
                for (size_t inner = 0; allNull && inner < Private::regional_inner_len; ++inner)
                    allNull &= d->roads.regional[unknownLanIdx][outer][inner] == NULL;
                if (allNull) {
                    free(d->roads.regional[unknownLanIdx][outer]);
                    d->roads.regional[unknownLanIdx][outer] = NULL;
                }
            }

        bool allNull = true;
        for (size_t outer = 0; allNull && outer < Private::regional_outer_len; ++outer)
            allNull &= d->roads.regional[unknownLanIdx][outer] == NULL;
        if (allNull) {
            free(d->roads.regional[unknownLanIdx]);
            d->roads.regional[unknownLanIdx] = NULL;
        }
    }
}

std::vector<struct OSMElement> Sweden::identifyPlaces(const std::vector<std::string> &word_combinations) const {
    std::vector<struct OSMElement> result;

    for (const std::string &combined : word_combinations) {
        const char *combined_cstr = combined.c_str();

        /// Retrieve all OSM elements matching a given word combination
        const std::vector<struct OSMElement> element_list = swedishTextTree->retrieve(combined_cstr, (SwedishTextTree::Warnings)(SwedishTextTree::WarningsAll & (~SwedishTextTree::WarningWordNotInTree)));
        for (const OSMElement &element : element_list) {
            if (element.realworld_type == OSMElement::PlaceLargeArea || element.realworld_type == OSMElement::PlaceLarge || element.realworld_type == OSMElement::PlaceMedium || element.realworld_type == OSMElement::PlaceSmall)
                result.push_back(element);
        }
    }

    /// Sort found places using this lambda expression,
    /// large places go first, small places go last
    std::sort(result.begin(), result.end(), [](struct OSMElement & a, struct OSMElement & b) {
        return a.realworld_type < b.realworld_type;
    });

#ifdef DEBUG
    static const size_t maxlen = 16128;
    char id_str[maxlen + 256];
    memset(id_str, 0, maxlen);
    char *p = id_str;
    for (auto it = result.cbegin(); it != result.cend() && (size_t)(p - id_str) < maxlen; ++it)
        p += snprintf(p, maxlen - (p - id_str), " %s (%s)", it->operator std::string().c_str(), it->name().c_str());
    Error::debug("Num of places: %d  List of node ids:%s", result.size(), id_str);
#endif

    return result;
}

void Sweden::insertAdministrativeRegion(const std::string &name, int admin_level, uint64_t relationId) {
    if (admin_level >= 8) return; ///< Skip too low-level administrative boundaries

    /// Some relation ids should be ignored, e.g. those right outside
    /// of Sweden which just happend to be included in the map data.
    static const uint64_t blacklistedRelIds[] = {38091, 50046, 52822, 54224, 404589, 406060, 406106, 406567, 406621, 407717, 408105, 412436, 1650407, 1724359, 1724456, 2000320, 2375170, 2375171, 2526815, 2541341, 2587236, 2978650, 4222805, 0};
    for (int i = 0; blacklistedRelIds[i] > 0; ++i)
        if (relationId == blacklistedRelIds[i]) return;

    d->administrativeRegion.insert(name, admin_level, relationId);
}

uint64_t Sweden::retrieveAdministrativeRegion(const std::string &name, int *admin_level) {
    return d->administrativeRegion.retrieve(name, admin_level);
}

std::vector<struct Sweden::KnownAdministrativeRegion> Sweden::identifyAdministrativeRegions(const std::vector<std::string> &word_combinations) {
    std::vector<struct KnownAdministrativeRegion> result;

    for (const std::string &combined : word_combinations) {
        int admin_level = -1;
        const uint64_t relationId = retrieveAdministrativeRegion(combined, &admin_level);
        if (relationId > 0) {
            if (admin_level < 0)
                Error::warn("Administrative region of name '%s' (relation id %llu) has invalid 'admin_level'", combined.c_str(), relationId);
            result.push_back(KnownAdministrativeRegion(relationId, combined, admin_level));
        }
    }

    return result;
}
