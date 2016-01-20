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

#include <cstdlib>
#include <sstream>
#include <fstream>

#include "swedishtexttree.h"
#include "tokenizer.h"
#include "timer.h"
#include "weightednodeset.h"
#include "sweden.h"
#include "tokenprocessor.h"
#include "htmloutput.h"
#include "svgwriter.h"
#include "global.h"
#include "globalobjects.h"
#include "config.h"

inline bool ends_with(std::string const &value, std::string const &ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

int main(int argc, char *argv[])
{
#ifdef DEBUG
    Error::debug("DEBUG flag enabled");
#endif // DEBUG

    char defaultconfigfile[1024];
    snprintf(defaultconfigfile, 1024, "%s/git/pbflookup/sweden.config", getenv("HOME"));
    if (!init_configuration((argc < 2) ? defaultconfigfile : argv[argc - 1])) {
        Error::err("Cannot continue without properly parsing configuration file");
        return 1;
    }

    /// std::unique_ptr will take care of destroying the unique instance of
    /// GlobalObjectManager when this function exists.
    /// Note: 'gom' is not used correctly. Rather, it will initialize various
    /// global variables/objects during creation and free those global variables/
    /// objects during its destruction.
    std::unique_ptr<GlobalObjectManager> gom(GlobalObjectManager::instance());

    Timer timer;
    int64_t cputime, walltime;

    if (relMembers != NULL && wayNodes != NULL && node2Coord != NULL && nodeNames != NULL && swedishTextTree != NULL && sweden != NULL) {
        int setNr = 0;
        for (auto it = testsets.cbegin(); it != testsets.cend(); ++it, ++setNr) {
            Error::info("Test set: %s", it->name.c_str());
            const Coord expected = Coord::fromLonLat(it->lon, it->lat);

            SvgWriter *svgwriter = NULL;
            if (!it->svgoutputfilename.empty()) {
                svgwriter = new SvgWriter(it->svgoutputfilename, 2);
                sweden->drawSCBareas(*svgwriter);
                sweden->drawRoads(*svgwriter);
            }

            std::stringstream ss(it->text);
            std::vector<std::string> words;
            Timer timer;
            Tokenizer tokenizer;
            tokenizer.read_words(ss, words, Tokenizer::Unique);
            WeightedNodeSet wns;
            TokenProcessor tokenProcessor;
            tokenProcessor.evaluteWordCombinations(words, wns);
            tokenProcessor.evaluteRoads(words, wns);
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to evaluate data in testset '%s': %lius == %.1fs  (wall time: %lius == %.1fs)", it->name.c_str(), cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);

            for (auto itWns = wns.cbegin(); itWns != wns.cend(); ++itWns) {
                const WeightedNode &wn = *itWns;
                if (svgwriter != NULL)
                    svgwriter->drawPoint(wn.x, wn.y, SvgWriter::PoiGroup, "#0006", std::to_string(wn.id) + " " + std::to_string(wn.weight));
            }

            std::sort(wns.begin(), wns.end(), std::greater<WeightedNode>());

            Coord center = wns.weightedCenter();
            if (svgwriter != NULL)
                svgwriter->drawPoint(center.x, center.y, SvgWriter::ImportantPoiGroup, "blue", "weightedCenter");
            Error::debug("Expected location:  http://www.openstreetmap.org/#map=17/%.4f/%.4f", it->lat, it->lon);
            Error::debug("Computed location:  http://www.openstreetmap.org/#map=17/%.4f/%.4f", Coord::toLatitude(center.y), Coord::toLongitude(center.x));
            Error::info("  Distance Lat/Lon: %i m", center.distanceLatLon(expected));
            int maxwns = 10;
            for (auto wnsit = wns.cbegin(); maxwns > 0 && wnsit != wns.cend(); ++wnsit, --maxwns) {
                const WeightedNode &wn = *wnsit;
                Error::debug("Computed location:  http://www.openstreetmap.org/#map=17/%.4f/%.4f", Coord::toLatitude(wn.y), Coord::toLongitude(wn.x));
                Error::debug("  Distance Lat/Lon: %i m", expected.distanceLatLon(Coord(wn.x, wn.y)));
            }

            Error::info(" --- sortByEstimatedInternodeDistance ---");
            wns.sortByEstimatedDistanceToNeigbors();
            maxwns = 5;
            for (auto wnsit = wns.cbegin(); maxwns > 0 && wnsit != wns.cend(); ++wnsit, --maxwns) {
                const WeightedNode &wn = *wnsit;
                Error::debug("Computed location:  http://www.openstreetmap.org/#map=17/%.4f/%.4f", Coord::toLatitude(wn.y), Coord::toLongitude(wn.x));
                Error::debug("  Distance Lat/Lon: %i m", expected.distanceLatLon(Coord(wn.x, wn.y)));
            }

            timer.start();
            Error::debug("Running 'powerCluster'");
            wns.powerCluster(2.0, 2.0 / wns.size());
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to run powerCluster on testset '%s': %lius == %.1fs  (wall time: %lius == %.1fs)", it->name.c_str(), cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);

            std::sort(wns.begin(), wns.end(), std::greater<WeightedNode>());

            center = wns.weightedCenter();
            if (svgwriter != NULL)
                svgwriter->drawPoint(center.x, center.y, SvgWriter::ImportantPoiGroup, "red", "powerCluster weightedCenter");
            Error::debug("Expected location:  http://www.openstreetmap.org/#map=17/%.4f/%.4f", it->lat, it->lon);
            Error::debug("Computed location:  http://www.openstreetmap.org/#map=17/%.4f/%.4f", Coord::toLatitude(center.y), Coord::toLongitude(center.x));
            Error::info("  Distance Lat/Lon: %i m", center.distanceLatLon(expected));
            maxwns = 10;
            for (auto wnsit = wns.cbegin(); maxwns > 0 && wnsit != wns.cend(); ++wnsit, --maxwns) {
                const WeightedNode &wn = *wnsit;
                Error::debug("Computed location:  http://www.openstreetmap.org/#map=17/%.4f/%.4f", Coord::toLatitude(wn.y), Coord::toLongitude(wn.x));
                Error::debug("  Distance Lat/Lon: %i m", expected.distanceLatLon(Coord(wn.x, wn.y)));
            }

            Error::info(" --- sortByEstimatedInternodeDistance ---");
            wns.sortByEstimatedDistanceToNeigbors();
            maxwns = 5;
            for (auto wnsit = wns.cbegin(); maxwns > 0 && wnsit != wns.cend(); ++wnsit, --maxwns) {
                const WeightedNode &wn = *wnsit;
                Error::debug("Computed location:  http://www.openstreetmap.org/#map=17/%.4f/%.4f", Coord::toLatitude(wn.y), Coord::toLongitude(wn.x));
                Error::debug("  Distance Lat/Lon: %i m", expected.distanceLatLon(Coord(wn.x, wn.y)));
            }

            if (svgwriter != NULL) {
                svgwriter->drawPoint(expected.x, expected.y, SvgWriter::ImportantPoiGroup, "green", "expected");
                svgwriter->drawCaption(it->name);
                svgwriter->drawDescription(it->text);

                delete svgwriter; ///< destructor will finalize SVG file
                svgwriter = NULL;
            }

            Error::info("======================================================");
        }

        if (inputextfilename != NULL && inputextfilename[0] != '\0') {
            std::ifstream textfile(inputextfilename);
            if (textfile.is_open()) {
                Error::info("Reading token from '%s'", inputextfilename);
                timer.start();

                std::vector<std::string> words;
                Tokenizer tokenizer;
                tokenizer.read_words(textfile, words, Tokenizer::Unique);
                textfile.close();

                WeightedNodeSet wns;
                TokenProcessor tokenProcessor;
                tokenProcessor.evaluteWordCombinations(words, wns);
                tokenProcessor.evaluteRoads(words, wns);

                Error::debug("Running 'powerCluster'");
                wns.powerCluster(2.0, 2.0 / wns.size());
                Error::debug("Running 'buildRingCluster'");
                wns.buildRingCluster();
                wns.dumpRingCluster();

                if (!wns.ringClusters.empty()) {
                    if (tokenProcessor.knownRoads().empty())
                        Error::warn("No roads known");
                    else
                        for (auto it = tokenProcessor.knownRoads().cbegin(); it != tokenProcessor.knownRoads().cend(); ++it) {
                            const Sweden::Road &road = *it;
                            int64_t minSqDistance = INT64_MAX;
                            uint64_t bestNode = 0;
                            sweden->closestPointToRoad(wns.ringClusters.front().weightedCenterX, wns.ringClusters.front().weightedCenterY, road, bestNode, minSqDistance);
                        }
                } else
                    Error::warn("wns.ringClusters is empty");

                timer.elapsed(&cputime, &walltime);
                Error::info("Spent CPU time to tokenize and to search in data: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);

                HtmlOutput htmlOutput(tokenizer, wns);
                htmlOutput.write(words, std::string("/tmp/html"));
            }
        } else
            Error::info("No valid input text filename provided");

        /*
        timer.start();
        sweden->test();
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to search SCB/NUTS3 in data: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);
        */
    } else
        Error::err("No all variables got initialized correctly: relMembers, wayNodes, node2Coord, nodeNames, swedishTextTree, sweden");

    return 0;
}

