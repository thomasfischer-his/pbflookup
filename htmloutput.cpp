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

#include "htmloutput.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <ostream>
#include <sstream>

#include "error.h"
#include "globalobjects.h"

class HtmlOutput::Private
{
private:
    HtmlOutput *p;

public:
    const Tokenizer &tokenizer;
    const WeightedNodeSet &wns;

    explicit Private(HtmlOutput *parent, const Tokenizer &_tokenizer, const WeightedNodeSet &_wns)
        : p(parent), tokenizer(_tokenizer), wns(_wns) {
        /// nothing
    }

    std::string openstreetmapUrl(double lat, double lon) const {
        static const double deltaLat = .1;
        static const double deltaLon = .1;
        std::ostringstream stringStream;
        stringStream << "http://www.openstreetmap.org/export/embed.html?bbox=" << (lon - deltaLon) << "," << (lat - deltaLat) << "," << (lon + deltaLon) << "," << (lat + deltaLat) << "&amp;layer=mapnik";
        return stringStream.str();
    }
};

HtmlOutput::HtmlOutput(const Tokenizer &tokenizer, const WeightedNodeSet &wns)
    : d(new HtmlOutput::Private(this, tokenizer, wns)) {
    /// nothing
}

HtmlOutput::~HtmlOutput() {
    delete d;
}

bool HtmlOutput::write(const std::vector<std::string> &tokenizedWords, const std::string &outputDirectory) const {
    const int mkdirResult = mkdir(outputDirectory.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    if (mkdirResult != 0 && mkdirResult != EEXIST) {
        Error::warn("Could not create directory '%s' (mkdirResult=%d)", outputDirectory.c_str(), mkdirResult);
        //return false;
    }


    {
        std::ofstream output("/tmp/html/osmgeoref.css");
        if (!output.good()) {
            output.close();
            return false;
        }

        output << ".inputtext {" << std::endl;
        output << "  background:#fed;" << std::endl;
        output << "}" << std::endl;

        output << ".tokenizedword {" << std::endl;
        output << "  font-family:monospace;" << std::endl;
        output << "  background:#def;" << std::endl;
        output << "}" << std::endl;

        output << "th {" << std::endl;
        output << "  text-align:left;" << std::endl;
        output << "}" << std::endl;

        output.close();
    }

    {
        std::ofstream output("/tmp/html/index.html");
        if (!output.good()) {
            output.close();
            return false;
        }

        output << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Frameset//EN\"" << std::endl;
        output << "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-frameset.dtd\">" << std::endl;
        output << "<html>" << std::endl;

        output << "<head>" << std::endl;
        output << "<title>OSM GeoRef</title>" << std::endl;
        output << "</head>" << std::endl << std::endl;

        output << "<frameset cols=\"50%,50%\">" << std::endl;
        output << "<frameset rows=\"25%,25%,25%,25%\" />" << std::endl;
        output << "<frame src=\"inputtext.html\" />" << std::endl;
        output << "<frame src=\"tokenizedwords.html\" />" << std::endl;
        output << "<frame src=\"ringcluster.html\" />" << std::endl;
        output << "</frameset>" << std::endl << std::endl;
        output << "<frame name=\"osmmap\" src=\"" << d->openstreetmapUrl(58.3929, 13.8494) << "\" />" << std::endl;
        output << "</frameset>" << std::endl << std::endl;

        output << "</html>" << std::endl << std::endl;
        output.close();
    }

    {
        std::ofstream output("/tmp/html/inputtext.html");
        if (!output.good()) {
            output.close();
            return false;
        }

        output << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"" << std::endl;
        output << "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">" << std::endl;
        output << "<html>" << std::endl;

        output << "<head>" << std::endl;
        output << "<link rel=\"stylesheet\" type=\"text/css\" href=\"osmgeoref.css\" />" << std::endl;
        output << "<title>Input Text</title>" << std::endl;
        output << "<meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\" />" << std::endl;
        output << "</head>" << std::endl << std::endl;

        output << "<body><p class=\"inputtext\">";
        output << d->tokenizer.input_text();
        output << "</p></body>" << std::endl << std::endl;

        output << "</html>" << std::endl << std::endl;
        output.close();
    }

    {
        std::ofstream output("/tmp/html/tokenizedwords.html");
        if (!output.good()) {
            output.close();
            return false;
        }

        output << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"" << std::endl;
        output << "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">" << std::endl;
        output << "<html>" << std::endl;

        output << "<head>" << std::endl;
        output << "<link rel=\"stylesheet\" type=\"text/css\" href=\"osmgeoref.css\" />" << std::endl;
        output << "<title>Tokenized Words</title>" << std::endl;
        output << "<meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\" />" << std::endl;
        output << "</head>" << std::endl << std::endl;

        output << "<body><p>";
        for (auto it = tokenizedWords.cbegin(); it != tokenizedWords.cend();) {
            output << "<span class=\"tokenizedword\">" << *it << "</span>";
            ++it;
            if (it != tokenizedWords.cend())
                output << std::endl;
            else
                break;
        }
        output << "</p></body>" << std::endl << std::endl;

        output << "</html>" << std::endl << std::endl;
        output.close();
    }

    {
        std::ofstream output("/tmp/html/ringcluster.html");
        if (!output.good()) {
            output.close();
            return false;
        }

        output << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"" << std::endl;
        output << "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">" << std::endl;
        output << "<html>" << std::endl;

        output << "<head>" << std::endl;
        output << "<link rel=\"stylesheet\" type=\"text/css\" href=\"osmgeoref.css\" />" << std::endl;
        output << "<title>Ring Cluster</title>" << std::endl;
        output << "<meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\" />" << std::endl;
        output << "</head>" << std::endl << std::endl;

        output << "<body><table width=\"100%\">" << std::endl;
        output << "<thead><tr><th width=\"20%\">Weight</th><th width=\"10%\"># Nodes</th><th width=\"10%\">Center Coord</th><th width=\"60%\">Label</th></tr></thead>" << std::endl;
        output << "<tbody>" << std::endl;
        for (auto it = d->wns.ringClusters.cbegin(); it != d->wns.ringClusters.cend(); ++it) {
            const RingCluster &rc = *it;
            output << "<tr>";
            output << "<td>" << rc.sumWeight << "</td>";
            output << "<td>" << rc.neighbourNodeIds.size() << "</td>";
            const double lat = Coord::toLatitude(rc.weightedCenterY);
            const double lon = Coord::toLongitude(rc.weightedCenterX);
            output << "<td><a target=\"osmmap\" href=\"" << d->openstreetmapUrl(lat, lon) << "\">OSM</a></td>";
            WriteableString nodeName;
            nodeNames->retrieve(rc.centerNodeId, nodeName);
            output << "<td style=\"font-size:80%;\">" << (nodeName.empty() ? "[" : "") << "<a target=\"_blank\" href=\"https://www.openstreetmap.org/node/" << rc.centerNodeId << "\">";
            if (nodeName.empty())
                output << rc.centerNodeId;
            else
                output << nodeName;
            output << "</a>" << (nodeName.empty() ? "]" : "") << "</td>";
            output << "</tr>" << std::endl;
        }
        output << "</tbody>" << std::endl;
        output << "</table></body>" << std::endl << std::endl;

        output << "</html>" << std::endl << std::endl;
        output.close();
    }

    return true;
}
