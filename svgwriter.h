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

#include "global.h"

class SvgWriter
{
public:
    explicit SvgWriter(const std::string &filename, const double minlatitude = minlat, const double maxlatitude = maxlat, const double minlongitude = minlon, const double maxlongitude = maxlon);
    explicit SvgWriter(const std::string &filename, const int minX, const int maxX, const int minY, const int maxY);
    ~SvgWriter();

    void drawLine(int x1, int y1, int x2, int y2, const std::string &comment = std::string());

private:
    class Private;
    Private *const d;
};

#endif // SVGWRITER_H
