/***************************************************************************
 *   Copyright (C) 2016 by Thomas Fischer <thomas.fischer@his.se>          *
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

#include "testset.h"

#include <algorithm>

#include "global.h"
#include "globalobjects.h"
#include "config.h"
#include "svgwriter.h"
#include "resultgenerator.h"
#include "timer.h"
#include "helper.h"

void Testset::run() {
    int setNr = 0;
    ResultGenerator resultGenerator;
    for (auto it = testsets.cbegin(); it != testsets.cend(); ++it, ++setNr) {
        Error::info("Test set: %s (%d bytes)", it->name.c_str(), it->text.length());
        const std::vector<Coord> &expected = it->coord;

        SvgWriter *svgwriter = NULL;
        if (!it->svgoutputfilename.empty()) {
            /// If requested in configuration file, prepare to write SVG file
            svgwriter = new SvgWriter(it->svgoutputfilename, 2);
            sweden->drawSCBareas(*svgwriter);
            sweden->drawRoads(*svgwriter);
        }

        std::vector<Result> results = resultGenerator.findResults(it->text, 0, ResultGenerator::VerbosityTalking);

        if (!results.empty()) {
            /// Sort results by quality (highest first)
            std::sort(results.begin(), results.end(), [](Result & a, Result & b) {
                return a.quality > b.quality;
            });

            Error::info("Found %d many possible results for testset '%s':", results.size(), it->name.c_str());
            for (const Result &result : results) {
                const double lon = Coord::toLongitude(result.coord.x);
                const double lat = Coord::toLatitude(result.coord.y);
                const std::vector<int> m = sweden->insideSCBarea(result.coord);
                const int scbarea = m.empty() ? 0 : m.front();
                Error::info("Able to determine a likely position with quality %.5lf near %s (%s), found through '%s'", result.quality, Sweden::nameOfSCBarea(scbarea).c_str(), Sweden::nameOfSCBarea(scbarea / 100).c_str(), result.origin.c_str());
                Error::debug("  http://www.openstreetmap.org/?mlat=%.5f&mlon=%.5f#map=12/%.5f/%.5f", lat, lon, lat, lon);
                for (const Coord &exp : expected)
                    if (exp.isValid()) {
                        const double lon = Coord::toLongitude(exp.x);
                        const double lat = Coord::toLatitude(exp.y);
                        const std::vector<int> m = sweden->insideSCBarea(exp);
                        const int scbarea = m.empty() ? 0 : m.front();
                        Error::info("Distance to expected result: %.1fkm near %s (%s)", exp.distanceLatLon(result.coord) / 1000.0, Sweden::nameOfSCBarea(scbarea).c_str(), Sweden::nameOfSCBarea(scbarea / 100).c_str());
                        Error::debug("  http://www.openstreetmap.org/?mlat=%.5f&mlon=%.5f#map=12/%.5f/%.5f", lat, lon, lat, lon);
                    }
            }
        } else
            Error::warn("Unable to determine a likely position");

        if (svgwriter != NULL) {
            for (const Coord &exp : expected)
                svgwriter->drawPoint(exp.x, exp.y, SvgWriter::ImportantPoiGroup, "green", "expected");
            for (const Result &result : results)
                svgwriter->drawPoint(result.coord.x, result.coord.y, SvgWriter::ImportantPoiGroup, "red", "computed");
            svgwriter->drawCaption(it->name);
            svgwriter->drawDescription(it->text);

            delete svgwriter; ///< destructor will finalize SVG file
            svgwriter = NULL;
        }

        Error::info("======================================================");
    }
}
