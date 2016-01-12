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

public:
    std::ofstream output;
    const double zoom;
    SvgWriter::Group previousGroup;

    Private(const std::string &filename, double _zoom, SvgWriter *parent)
        : p(parent), zoom(_zoom), previousGroup(SvgWriter::InvalidGroup) {
        output.open(filename, std::ofstream::out | std::ofstream::trunc);
        if (!output.is_open() || !output.good()) {
            Error::err("Failed to open SVG file '%s'");
            /// Program will fail here
        }
        output.setf(std::ofstream::fixed);
        output.precision(3);

        output << "<?xml version=\"1.0\" standalone=\"no\"?>" << std::endl;
        //output << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">" << std::endl << std::endl;
        output << "<svg width=\"" << (1000 * zoom) << "\" height=\"" << (2047 * zoom) << "\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:svg=\"http://www.w3.org/2000/svg\" version=\"1.1\">" << std::endl;
    }

    ~Private() {
        if (previousGroup != SvgWriter::InvalidGroup)
            output << "  </g>" << std::endl;
        output << "</svg>" << std::endl << std::endl;

        output.close();
    }

    static inline double normalizeX(const int &x) {
        return (x - 3455178) / 7580.764;
    }

    static inline double normalizeY(const int &y) {
        return (17001474 - y) / 7580.764;
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

    void splitColorAndTransparency(const std::string &inputColor, std::string &justColor, float &transparency)const {
        transparency = 1.0;
        justColor = inputColor;
        if (inputColor[0] == '#') {
            if (inputColor.length() == 5) { /// pattern #rgba
                justColor = inputColor.substr(0, 4);
                const unsigned char transparencyChar = inputColor[4] | 0x20;
                if (transparencyChar >= '0' && transparencyChar <= '9')
                    transparency = (transparencyChar - '0') / 15.0;
                else if (transparencyChar >= 'a' && transparencyChar <= 'f')
                    transparency = (transparencyChar - 'a' + 10) / 15.0;
            } else if (inputColor.length() == 9) { /// pattern #rgba
                justColor = inputColor.substr(0, 7);
                const unsigned char transparencyCharA = inputColor[7] | 0x20;
                if (transparencyCharA >= '0' && transparencyCharA <= '9')
                    transparency = (transparencyCharA - '0') / 15.0;
                else if (transparencyCharA >= 'a' && transparencyCharA <= 'f')
                    transparency = (transparencyCharA - 'a' + 10) / 15.0;
                const unsigned char transparencyCharB = inputColor[8] | 0x20;
                if (transparencyCharB >= '0' && transparencyCharB <= '9')
                    transparency += (transparencyCharB - '0') / 15.0 / 16.0;
                else if (transparencyCharB >= 'a' && transparencyCharB <= 'f')
                    transparency = (transparencyCharB - 'a' + 10) / 15.0 / 16.0;
            }
        }
    }
};


SvgWriter::SvgWriter(const std::string &filename, double zoom)
    : d(new Private(filename, zoom, this))
{
    /// nothing
}

SvgWriter::~SvgWriter() {
    delete d;
}

void SvgWriter::drawCaption(const std::string &caption) const {
    d->switchGroup(SvgWriter::TextGroup);

    d->output << "    <text style=\"font-family:sans-serif;font-size:" << (36 * d->zoom) << ";\" x=\"0\" y=\"" << (36 * d->zoom) << "\">" << d->toHtml(caption) << "</text>" << std::endl;
}

void SvgWriter::drawDescription(const std::string &description) const {
    d->switchGroup(SvgWriter::TextGroup);

    const std::vector<std::string> lines = d->splitIntoLines(description);
    int y = 60;
    for (auto it = lines.cbegin(); it != lines.cend(); ++it, y += 20)
        d->output << "    <text style=\"font-family:sans-serif;font-size:" << (16 * d->zoom) << ";\" x=\"0\" y=\"" << (y * d->zoom) << "\">" << d->toHtml(*it) << "</text>" << std::endl;
}

void SvgWriter::drawLine(int x1, int y1, int x2, int y2, Group group, const std::string &comment) const {
    d->switchGroup(group);

    d->output << "    <line x1=\"" << (Private::normalizeX(x1) * d->zoom) << "\" y1=\"" << (Private::normalizeY(y1) * d->zoom) << "\" x2=\"" << (Private::normalizeX(x2) * d->zoom) << "\" y2=\"" << (Private::normalizeY(y2) * d->zoom) << "\" />";
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
        d->output << (Private::normalizeX(*itx) * d->zoom) << "," << (Private::normalizeY(*ity) * d->zoom);
    }
    d->output << "\" />";
    if (!comment.empty())
        d->output << "<!-- " << comment << " -->";
    d->output << std::endl;
}

void SvgWriter::drawPoint(int x, int y, Group group, const std::string &color, const std::string &comment) const {
    d->switchGroup(group);

    std::string justColor = color;
    float transparency = 1.0;
    d->splitColorAndTransparency(color, justColor, transparency);

    const int radius = (group == ImportantPoiGroup) ? 8 : 4;
    const int sw = (group == ImportantPoiGroup) ? 3 : 2;
    d->output << "    <circle cx=\"" << (Private::normalizeX(x) * d->zoom) << "\" cy=\"" << (Private::normalizeY(y) * d->zoom) << "\" r=\"" << radius << "\" style=\"stroke-width:" << sw << ";";
    if (!justColor.empty())
        d->output << "stroke:" << justColor << ";";
    if (transparency < 1.0)
        d->output << "stroke-opacity:" << transparency << ";";
    d->output << "\" />";
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
        d->output << (Private::normalizeX(*itx) * d->zoom) << "," << (Private::normalizeY(*ity) * d->zoom);
    }
    d->output << "\" />";
    if (!comment.empty())
        d->output << "<!-- " << comment << " -->";
    d->output << std::endl;
}
