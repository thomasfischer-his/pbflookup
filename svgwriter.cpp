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
            output << "  <g fill=\"white\" stroke=\"black\" stroke-width=\"1\">" << std::endl;
            break;
        case SvgWriter::PoiGroup:
        case SvgWriter::ImportantPoiGroup:
            output << "  <g fill=\"none\" stroke=\"red\" stroke-width=\"2\">" << std::endl;
            break;
        case SvgWriter::TextGroup:
            output << "  <g fill=\"black\" stroke=\"none\">" << std::endl;
            break;
        case SvgWriter::RoadGroup:
            output << "  <g fill=\"none\" stroke=\"#369\" stroke-width=\"0.3\">" << std::endl;
            break;
        case SvgWriter::InvalidGroup:
            /// nothing to do here
            break;
        }

        previousGroup = group;
    }

    std::string toHtml(const std::string &input) const {
        std::string result;
        const size_t len = input.length();
        for (size_t i = 0; i < len; ++i) {
            const unsigned char &c = input[i];
            if (c == 0x26)
                /// Ampersand
                result.append("&amp;");
            else  if (c == 0x3c)
                /// Less than
                result.append("&lt;");
            else  if (c == 0x3e)
                /// Greater than
                result.append("&gt;");
            else if (c >= 32 && c < 127)
                /// Regular characters
                result.push_back(c);
            else if (c == 0xc3 && i < len - 1) {
                /// Various Swedish umlauts consisting of two bytes
                result.push_back(c);
                ++i;
                result.push_back(input[i]);
            }
        }

        return result;
    }

    std::vector<std::string> splitIntoLines(const std::string &input) const {
        std::vector<std::string> result;

        std::string line;
        const size_t len = input.length();
        for (size_t pos = 0; pos < len; ++pos) {
            const unsigned char &c = input[pos];
            if ((c == ' ' || c == '\n' || c == '\r') && line.length() > 60) {
                result.push_back(line);
                line.clear();
            } else
                line.push_back(c);
        }

        if (!line.empty())
            result.push_back(line);

        return result;
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

    d->output << "    <text style=\"font-family:sans-serif;font-size:36;\" x=\"0\" y=\"36\">" << d->toHtml(caption) << "</text>" << std::endl;
}

void SvgWriter::drawDescription(const std::string &description) const {
    d->switchGroup(SvgWriter::TextGroup);

    const std::vector<std::string> lines = d->splitIntoLines(description);
    int y = 60;
    for (auto it = lines.cbegin(); it != lines.cend(); ++it, y += 20)
        d->output << "    <text style=\"font-family:sans-serif;font-size:16;\" x=\"0\" y=\"" << y << "\">" << d->toHtml(*it) << "</text>" << std::endl;
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

void SvgWriter::drawPoint(int x, int y, Group group, const std::string &color, const std::string &comment) const {
    d->switchGroup(group);

    const int radius = (group == ImportantPoiGroup) ? 8 : 4;
    const int sw = (group == ImportantPoiGroup) ? 3 : 2;
    d->output << "    <circle cx=\"" << normalizeX(x) << "\" cy=\"" << normalizeY(y) << "\" stroke-width=\"" << sw << "\" r=\"" << radius << "\"";
    if (!color.empty())
        d->output << " stroke=\"" << color << "\"";
    d->output << "/>";
    if (!comment.empty())
        d->output << "<!-- " << comment << " -->";
    d->output << std::endl;
}

void SvgWriter::drawRoad(const std::vector<int> &x, const std::vector<int> &y, RoadImportance roadImportance, const std::string &comment) const {
    if (x.empty() || y.empty()) return;

    d->switchGroup(RoadGroup);

    const double width = (int)roadImportance * 0.4 + 0.3;
    d->output << "    <polyline stroke-width=\"" << width << "\" points=\"";
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
