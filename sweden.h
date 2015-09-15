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

extern const double decimeterDegreeLongitude;
extern const double decimeterDegreeLatitude;

class Sweden
{
public:
    enum RoadType {Europe = 0, National = 1, LanM = 2, LanK = 3, LanI = 4, LanH = 5, LanG = 6, LanN = 7, LanO = 8, LanF = 9, LanE = 10, LanD = 11, LanAB = 12, LanC = 13, LanU = 14, LanT = 15, LanS = 16, LanW = 17, LanX = 18, LanZ = 19, LanY = 20, LanAC = 21, LanBD = 22, UnknownRoadType = 23};

    explicit Sweden(IdTree<Coord> *coords, IdTree<WayNodes> *waynodes, IdTree<RelationMem> *relmem);
    explicit Sweden(std::istream &input, IdTree<Coord> *coords, IdTree<WayNodes> *waynodes, IdTree<RelationMem> *relmem);
    ~Sweden();

    void dump() const;
    void test();

    std::ostream &write(std::ostream &output);

    void insertSCBarea(const int code, uint64_t relid);
    std::vector<int> insideSCBarea(uint64_t nodeid);
    void insertNUTS3area(const int code, uint64_t relid);
    std::vector<int> insideNUTS3area(uint64_t nodeid);

    void insertWayAsRoad(uint64_t wayid, const char *refValue);
    void insertWayAsRoad(uint64_t wayid, RoadType roadType, uint16_t roadNumber);
    std::vector<uint64_t> waysForRoad(RoadType roadType, uint16_t roadNumber);
    /** Handle the case that E may be used for European roads
    * or roads in East Gothland
    */
    static RoadType identifyEroad(uint16_t roadNumber);

private:
    class Private;
    Private *const d;
};

#endif // SWEDEN_H
