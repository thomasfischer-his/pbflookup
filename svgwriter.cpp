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

#include "svgwriter.h"

#include <fstream>

#include "error.h"
#include "idtree.h"

class SvgWriter::Private {
private:
    SvgWriter *p;

    const int minX, minY, maxX, maxY;

public:
    std::ofstream output;

    Private(const std::string &filename, const int _minX, const int _maxX, const int _minY, const int _maxY, SvgWriter *parent)
        : p(parent), minX(_minX), minY(_minY), maxX(_maxX), maxY(_maxY) {
        output.open(filename, std::ofstream::out | std::ofstream::trunc);
        if (!output.is_open() || !output.good()) {
            Error::err("Failed to open SVG file '%s'");
            /// Program will fail here
        }

        output << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\">" << std::endl;
    }

    ~Private() {
        output << "</svg>" << std::endl << std::endl;

        output.close();
    }

    inline double normalize(int value, int min_in, int max_in, double max_out) const {
        double result = value;
        result -= min_in;
        result /= max_in - min_in;
        result *= max_out;
        return result;
    }
};

SvgWriter::SvgWriter(const std::string &filename, const double minlatitude, const double maxlatitude, const double minlongitude, const double maxlongitude)
    : d(new Private(filename, Coord::fromLatitude(minlatitude), Coord::fromLatitude(maxlatitude), Coord::fromLongitude(minlongitude), Coord::fromLongitude(maxlongitude), this))
{
    /// nothing
}

SvgWriter::SvgWriter(const std::string &filename, const int minX, const int maxX, const int minY, const int maxY)
    : d(new Private(filename, minX, maxX, minY, maxY, this))
{
    /// nothing
}

SvgWriter::~SvgWriter() {
    delete d;
}

void SvgWriter::drawLine(int x1, int y1, int x2, int y2, const std::string &comment) {
    d->output << "  <line x1=\"" << d->normalize(x1, 0, 20000000, 100000.0) << "\" x2=\"" << d->normalize(x2, 0, 20000000, 1000.0) << "\" y1=\"" << d->normalize(y1, 0, 30000000, 100000.0) << "\" y2=\"" <<  d->normalize(y2, 0, 30000000, 1000.0) << "\" stroke=\"blue\" stroke-width=\"1\" />";
    if (!comment.empty())
        d->output << "<!-- " << comment << " -->";
    d->output << std::endl;
}
