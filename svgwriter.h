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

#ifndef SVGWRITER_H
#define SVGWRITER_H

#include <string>
#include <vector>

#include "global.h"

class SvgWriter
{
public:
    enum Group {InvalidGroup = 0, BaseGroup, PoiGroup, ImportantPoiGroup, TextGroup, RoadGroup};
    enum RoadImportance {RoadNoImportance = 0, RoadMinorImportance = 1, RoadAvgImportance = 2, RoadMajorImportance = 3};

    explicit SvgWriter(const std::string &filename, double zoom = 1.0);
    ~SvgWriter();

    void drawCaption(const std::string &caption) const;
    void drawDescription(const std::string &description) const;
    void drawLine(int x1, int y1, int x2, int y2, Group group = BaseGroup, const std::string &comment = std::string()) const;
    void drawPolygon(const std::vector<int> &x, const std::vector<int> &y, Group group = BaseGroup, const std::string &comment = std::string()) const;
    void drawPoint(int x, int y, Group group = PoiGroup, const std::string &color = std::string(), const std::string &comment = std::string()) const;
    void drawRoad(const std::vector<int> &x, const std::vector<int> &y, RoadImportance roadImportance, const std::string &comment = std::string()) const;

private:
    class Private;
    Private *const d;
};

#endif // SVGWRITER_H
