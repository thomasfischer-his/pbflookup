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

#define normalizeX(a) (((a)-3455178)/7580.764)
#define normalizeY(b) ((17001474-(b))/7580.764)

class SvgWriter::Private {
private:
    SvgWriter *p;

public:
    std::ofstream output;
    SvgWriter::Group previousGroup;

    Private(const std::string &filename, SvgWriter *parent)
        : p(parent), previousGroup(SvgWriter::InvalidGroup) {
        output.open(filename, std::ofstream::out | std::ofstream::trunc);
        if (!output.is_open() || !output.good()) {
            Error::err("Failed to open SVG file '%s'");
            /// Program will fail here
        }

        output << "<?xml version=\"1.0\" standalone=\"no\"?>" << std::endl;
        //output << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">" << std::endl << std::endl;
        output << "<svg width=\"1000\" height=\"2047\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:svg=\"http://www.w3.org/2000/svg\" version=\"1.1\">" << std::endl;
    }

    ~Private() {
        if (previousGroup != SvgWriter::InvalidGroup)
            output << "  </g>" << std::endl;
        output << "</svg>" << std::endl << std::endl;

        output.close();
    }

    void switchGroup(SvgWriter::Group group) {
        if (group == previousGroup) return;

        if (previousGroup != SvgWriter::InvalidGroup)
            output << "  </g>" << std::endl;

        switch (group) {
        case SvgWriter::BaseGroup:
            output << "  <g fill=\"white\" stroke=\"black\" stroke-width=\"1\" >" << std::endl;
            break;
        case SvgWriter::PoiGroup:
            output << "  <g fill=\"yellow\" stroke=\"red\" stroke-width=\"2\" >" << std::endl;
            break;
        case SvgWriter::TextGroup:
            output << "  <g fill=\"black\" stroke=\"black\" stroke-width=\"1\" >" << std::endl;
            break;
        case SvgWriter::InvalidGroup:
            /// nothing to do here
            break;
        }

        previousGroup = group;
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

void SvgWriter::drawCaption(const std::string &caption) const {
    d->switchGroup(SvgWriter::TextGroup);

    d->output << "    <text style=\"font-family:sans-serif;font-size:36;\" x=\"0\" y=\"36\">" << caption << "</text>" << std::endl;
}

void SvgWriter::drawDescription(const std::string &description) const {
    d->switchGroup(SvgWriter::TextGroup);

    d->output << "    <text style=\"font-family:sans-serif;font-size:16;\" x=\"0\" y=\"60\">" << description << "</text>" << std::endl;
}

void SvgWriter::drawLine(int x1, int y1, int x2, int y2, Group group, const std::string &comment) const {
    d->switchGroup(group);

    d->output << "    <line x1=\"" << normalizeX(x1) << "\" y1=\"" << normalizeY(y1) << "\" x2=\"" << normalizeX(x2) << "\" y2=\"" << normalizeY(y2) << "\" />";
    if (!comment.empty())
        d->output << "<!-- " << comment << " -->";
    d->output << std::endl;
}

void SvgWriter::drawPolygon(const std::vector<int> &x, const std::vector<int> &y, Group group, const std::string &comment) const {
    if (x.empty() || y.empty()) return;

    d->switchGroup(group);

    d->output << "    <polygon points=\"";
    bool first = true;
    for (auto itx = x.cbegin(), ity = y.cbegin(); itx != x.cend() && ity != y.cend(); ++itx, ++ity) {
        if (!first) {
            d->output << " ";
        }
        first = false;
        d->output << normalizeX(*itx) << "," << normalizeY(*ity);
    }
    d->output << "\" />";
    if (!comment.empty())
        d->output << "<!-- " << comment << " -->";
    d->output << std::endl;
}

void SvgWriter::drawPoint(int x, int y, Group group, const std::string &comment) const {
    d->switchGroup(group);

    d->output << "    <circle cx=\"" << normalizeX(x) << "\" cy=\"" << normalizeY(y) << "\" r=\"5\" />";
    if (!comment.empty())
        d->output << "<!-- " << comment << " -->";
    d->output << std::endl;
}
