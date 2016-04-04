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

#ifndef SWEDEN_H
#define SWEDEN_H

#include <string>
#include <map>

#include "idtree.h"
#include "global.h"

class SvgWriter;

class Sweden
{
public:
    enum RoadType {Europe = 0, National = 1, LanM = 2, LanK = 3, LanI = 4, LanH = 5, LanG = 6, LanN = 7, LanO = 8, LanF = 9, LanE = 10, LanD = 11, LanAB = 12, LanC = 13, LanU = 14, LanT = 15, LanS = 16, LanW = 17, LanX = 18, LanZ = 19, LanY = 20, LanAC = 21, LanBD = 22, LanUnknown = 23, UnknownRoadType = 24};
    struct Road {
        Road(Sweden::RoadType _type, int _number)
            : type(_type), number(_number) {
            /// nothing
        }

        Sweden::RoadType type;
        int number;
    };

    explicit Sweden();
    explicit Sweden(std::istream &input);
    ~Sweden();

    void dump() const;
    void test();

    std::ostream &write(std::ostream &output);

    bool nodeInsideRelationRegion(uint64_t nodeId, uint64_t relationId);
    void insertSCBarea(const int code, uint64_t relid);
    std::vector<int> insideSCBarea(uint64_t nodeid);
    static Sweden::RoadType roadTypeForSCBarea(int scbarea);
    static std::string nameOfSCBarea(int scbarea);
    void insertNUTS3area(const int code, uint64_t relid);
    std::vector<int> insideNUTS3area(uint64_t nodeid);

    void drawSCBareas(const std::string &filename);
    void drawSCBareas(SvgWriter &svgWriter);
    void drawRoads(SvgWriter &svgWriter);

    /**
     * Insert an administrative region such as a county or a
     * municipality as identified by its administrative level
     * (numeric value), name, and relation id.
     * @param name name of the administrative region to insert
     * @param admin_level administrative level as used in OSM data
     * @param relationId relation id of the administration region to add
     */
    void insertAdministrativeRegion(const std::string &name, int admin_level, uint64_t relationId);

    /**
     * Retrieve the relation id of an administrative region such
     * as a county or a municipality by its name.
     * @param name name of the administrative region to look up
     * @param admin_level administrative level as used in OSM data
     * @return relation id of the administration region if found, else 0
     */
    uint64_t retrieveAdministrativeRegion(const std::string &name, int *admin_level = NULL);

    struct KnownAdministrativeRegion {
        KnownAdministrativeRegion(uint64_t _relationId, const std::string &_name, int _admin_level)
            : relationId(_relationId), name(_name), admin_level(_admin_level) {
            /// nothing
        }

        uint64_t relationId;
        std::string name;
        int admin_level;
    };

    std::vector<struct KnownAdministrativeRegion> identifyAdministrativeRegions(const std::vector<std::string> &word_combinations);

    void insertWayAsRoad(uint64_t wayid, const char *refValue);
    void insertWayAsRoad(uint64_t wayid, RoadType roadType, uint16_t roadNumber);
    std::vector<uint64_t> waysForRoad(RoadType roadType, uint16_t roadNumber);

    /**
     * Determine a short textual representation for a road type.
     * Example: LanAB will return 'AB'.
     * @param roadType road type to represent
     * @return short text string
     */
    static std::string roadTypeToString(RoadType roadType);

    /** Handle the case that E may be used for European roads
    * or roads in East Gothland
    */
    static RoadType identifyEroad(uint16_t roadNumber);

    /**
     * For a given position (x,y) and a given Swedish road, determine the node in this road
     * closest to the given position and the distance between the node and the position in
     * meters.
     *
     * @param x
     * @param y
     * @param road
     * @param bestNode
     * @param distance Distance in meters as measured on decimeter grid
     */
    Sweden::RoadType closestRoadNodeToCoord(int x, int y, const Sweden::Road &road, uint64_t &bestNode, int &distance) const;

    /**
     * Process the provided list of words and see if there is
     * a sequence of words that looks like a road label (e.g.
     * 'E 20').
     * @param words tokenized list of single words, not combinations
     * @return List of roads (type and number per road)
     */
    std::vector<struct Sweden::Road> identifyRoads(const std::vector<std::string> &words) const;

    /**
     * OpenStreetMap data contains regional roads without proper
     * region codes. This roads are by default assigned to an
     * "unknown" region.
     * This function processes those roads by picking a pivot
     * node from each way and then determines in which region
     * the node is located. Based on this localization, the road
     * is reclassified and migrated to the proper region.
     */
    void fixUnlabeledRegionalRoads();

    /**
     * Process the provided list of word combination and see if there
     * are known places (cities, towns, hamlets, ...) referred to.
     * The resulting list is sorted by the places' size/importance.
     * Cities and counties go first, hamlets or small settlements go last.
     * @param word_combinations Word combinations to check for places
     * @return List of places, sorted by places' importance (important ones go first)
     */
    std::vector<struct OSMElement> identifyPlaces(const std::vector<std::string> &word_combinations) const;

private:
    class Private;
    Private *const d;
};

#endif // SWEDEN_H
