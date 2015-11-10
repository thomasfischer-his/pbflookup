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

#define scale(a) ((a)/10000.0f)

class SvgWriter::Private {
private:
    SvgWriter *p;

public:
    std::ofstream output;

    Private(const std::string &filename, SvgWriter *parent)
        : p(parent) {
        output.open(filename, std::ofstream::out | std::ofstream::trunc);
        if (!output.is_open() || !output.good()) {
            Error::err("Failed to open SVG file '%s'");
            /// Program will fail here
        }

        output << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\">" << std::endl;
        output << "  <g fill=\"none\" stroke=\"black\" stroke-width=\"1\" >" << std::endl;
    }

    ~Private() {
        output << "  </g>" << std::endl;
        output << "</svg>" << std::endl << std::endl;

        output.close();
    }
};


SvgWriter::SvgWriter(const std::string &filename)
    : d(new Private(filename, this))
{
    /// nothing
}

SvgWriter::~SvgWriter() {
    delete d;
}

void SvgWriter::drawLine(int x1, int y1, int x2, int y2, const std::string &comment) const {
    d->output << "    <line x1=\"" << scale(x1) << "\" y1=\"-" << scale(y1) << "\" x2=\"" << scale(x2) << "\" y2=\"-" << scale(y2) << "\" />";
    if (!comment.empty())
        d->output << "<!-- " << comment << " -->";
    d->output << std::endl;
}

void SvgWriter::drawPolygon(const std::vector<int> &x, const std::vector<int> &y, const std::string &comment) const {
    if (x.empty() || y.empty()) return;

    d->output << "    <polygon points=\"";
    bool first = true;
    for (auto itx = x.cbegin(), ity = y.cbegin(); itx != x.cend() && ity != y.cend(); ++itx, ++ity) {
        if (!first) {
            d->output << " ";
        }
        first = false;
        d->output << scale(*itx) << ",-" << scale(*ity);
    }
    d->output << "\" />";
    if (!comment.empty())
        d->output << "<!-- " << comment << " -->";
    d->output << std::endl;
}
