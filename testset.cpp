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
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "testset.h"

#include <algorithm>
#include <random>
#ifdef LATEX_OUTPUT
#include <fstream>
#endif // LATEX_OUTPUT

#include "global.h"
#include "globalobjects.h"
#include "config.h"
#include "svgwriter.h"
#include "resultgenerator.h"
#include "timer.h"
#include "helper.h"

void Testset::run() {
    Error::info("Randomizing order of testsets");
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(testsets.begin(), testsets.end(), g);

    ResultGenerator resultGenerator;
    for (const auto &testset : testsets) {
        Error::info("Test set: %s (%d bytes)", testset.name.c_str(), testset.text.length());
        const std::vector<Coord> &expected = testset.coord;

        SvgWriter *svgwriter = nullptr;
        if (!testset.svgoutputfilename.empty()) {
            /// If requested in configuration file, prepare to write SVG file
            svgwriter = new SvgWriter(testset.svgoutputfilename, 2);
            sweden->drawSCBareas(*svgwriter);
            sweden->drawRoads(*svgwriter);
        }

        ResultGenerator::Statistics resultGeneratorStatistics;
        std::vector<Result> results = resultGenerator.findResults(testset.text, 0, ResultGenerator::VerbosityTalking, &resultGeneratorStatistics);

        if (!results.empty()) {
            /// Sort results by quality (highest first)
            std::sort(results.begin(), results.end(), [](Result & a, Result & b) {
                return a.quality > b.quality;
            });

            Error::info("Found %d many possible results for testset '%s'", results.size(), testset.name.c_str());

            for (const Result &result : results) {
                const double lon = Coord::toLongitude(result.coord.x);
                const double lat = Coord::toLatitude(result.coord.y);
                const int scbarea = sweden->insideSCBarea(result.coord, Sweden::LevelMunicipality);
                Error::info("Able to determine a likely position with quality %.5lf near %s (%s), found through '%s'", result.quality, Sweden::nameOfSCBarea(scbarea).c_str(), Sweden::nameOfSCBarea(scbarea / 100).c_str(), result.origin.c_str());
                Error::debug("  https://www.openstreetmap.org/?mlat=%.5f&mlon=%.5f#map=12/%.5f/%.5f", lat, lon, lat, lon);
                for (const Coord &exp : expected)
                    if (exp.isValid()) {
                        const double lon = Coord::toLongitude(exp.x);
                        const double lat = Coord::toLatitude(exp.y);
                        const int scbarea = sweden->insideSCBarea(exp, Sweden::LevelMunicipality);
                        Error::info("Distance to expected result: %.1fkm near %s (%s)", exp.distanceLatLon(result.coord) / 1000.0, Sweden::nameOfSCBarea(scbarea).c_str(), Sweden::nameOfSCBarea(scbarea / 100).c_str());
                        Error::debug("  https://www.openstreetmap.org/?mlat=%.5f&mlon=%.5f#map=12/%.5f/%.5f", lat, lon, lat, lon);
                    }
            }
        } else
            Error::warn("Unable to determine a likely position");

        if (svgwriter != nullptr) {
            for (const Coord &exp : expected)
                svgwriter->drawPoint(exp.x, exp.y, SvgWriter::ImportantPoiGroup, "green", "expected");
            for (const Result &result : results)
                svgwriter->drawPoint(result.coord.x, result.coord.y, SvgWriter::ImportantPoiGroup, "red", "computed");
            svgwriter->drawCaption(testset.name);
            svgwriter->drawDescription(testset.text);

            delete svgwriter; ///< destructor will finalize SVG file
            svgwriter = nullptr;
        }

        Error::info("======================================================");
    }

#ifdef LATEX_OUTPUT
    /// Sort testsets by their names
    std::sort(testsets.begin(), testsets.end(), [](struct testset & a, struct testset & b) {
        return a.name < b.name;
    });

    std::ofstream texTable("/tmp/testsets.tex");
    texTable << "\\begin{description}" << std::endl;
    for (const auto &testset : testsets) {
        texTable << "\\item[\\begingroup\\selectlanguage{swedish}" << testset.name << "\\endgroup] at " << testset.coord.front().latitude() << "~N, " << testset.coord.front().longitude() << "~E%";
        if (testset.coord.size() > 1) texTable << " \\begingroup\\relsize{-1}(first of " << testset.coord.size() << " coordinates)\\endgroup";
        texTable << std::endl << "\\par\\begingroup\\relsize{-1}\\begingroup\\slshape\\selectlanguage{swedish}%" << std::endl;
        if (testset.text.length() < 2048)
            texTable << rewrite_TeX_spaces(teXify(testset.text)) << "\\endgroup";
        else {
            std::istringstream iss(testset.text);
            const std::vector<std::string> words {std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{}};
            size_t len = 0;
            std::string text;
            for (const std::string &w : words) {
                if (len > 0) text.append(" ");
                len += w.length();
                text.append(w);
                if (len > 2048 - 64) break;
            }
            texTable << rewrite_TeX_spaces(teXify(text));
            texTable << "\\endgroup\\ \\hspace{1em plus 1em minus 0.9em}(remaining text omitted)";
        }
        texTable << "\\par\\endgroup" << std::endl;
    }
#endif // LATEX_OUTPUT
    texTable << "\\end{description}" << std::endl;
}
