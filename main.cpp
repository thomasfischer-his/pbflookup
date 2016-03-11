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
#include "helper.h"

inline bool ends_with(std::string const &value, std::string const &ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void init_rand() {
    FILE *devrandom = fopen("/dev/urandom", "r");
    unsigned int seed = time(NULL) ^ (getpid() << 8);
    if (devrandom != NULL) {
        fread((void *)&seed, sizeof(seed), 1, devrandom);
        fclose(devrandom);
    }

    Error::debug("seed=%08x", seed);
    srand(seed);
}

int main(int argc, char *argv[])
{
#ifdef DEBUG
    Error::debug("DEBUG flag enabled");
#endif // DEBUG
    init_rand();

    const char *defaultconfigfile = "sweden.config";
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
            Coord result;
            Error::info("Test set: %s", it->name.c_str());
            const Coord expected = Coord::fromLonLat(it->lon, it->lat);

            SvgWriter *svgwriter = NULL;
            if (!it->svgoutputfilename.empty()) {
                /// If requested in configuration file, prepare to write SVG file
                svgwriter = new SvgWriter(it->svgoutputfilename, 2);
                sweden->drawSCBareas(*svgwriter);
                sweden->drawRoads(*svgwriter);
            }

            timer.start();
            Tokenizer tokenizer;
            std::vector<std::string> words, word_combinations;
            tokenizer.read_words(it->text, words, Tokenizer::Duplicates);
            tokenizer.add_grammar_cases(words);
            tokenizer.generate_word_combinations(words, word_combinations, 3 /** TODO configurable */, Tokenizer::Unique);
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to tokenize text in testset '%s': %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", it->name.c_str(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);

            TokenProcessor tokenProcessor;

            if (!result.isValid() /** no valid result found yet */) {
                /// ===================================================================================
                /// Check if the test input contains road labels (e.g. 'E 20') and city/town names.
                /// Then determine the clostest distance between any city/town and any identified road.
                /// If distance is below an acceptable threshold, assume location on road closest to
                /// town as resulting position.
                /// -----------------------------------------------------------------------------------
                Error::info("=== Testing for roads close to cities/towns ===");

                timer.start();
                std::vector<struct Sweden::Road> identifiedRoads = sweden->identifyRoads(words);
                std::vector<struct TokenProcessor::RoadMatch> roadMatch = tokenProcessor.evaluteRoads(word_combinations, identifiedRoads);
                timer.elapsed(&cputime, &walltime);
                Error::info("Spent CPU time to identify roads in testset '%s': %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", it->name.c_str(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);

                if (!roadMatch.empty()) {
                    const TokenProcessor::RoadMatch &closestRoadMatch = roadMatch.front();
                    const int64_t closestDistance = closestRoadMatch.distance;

                    if (closestDistance < 10000) {
                        /// Closer than 10km
                        Error::info("Distance between '%s' and road %s %d: %.1f km (between road node %llu and word's node %llu)", closestRoadMatch.word_combination.c_str(), Sweden::roadTypeToString(closestRoadMatch.road.type).c_str(), closestRoadMatch.road.number, closestDistance / 1000.0, closestRoadMatch.bestRoadNode, closestRoadMatch.bestWordNode);
                        if (node2Coord->retrieve(closestRoadMatch.bestRoadNode, result))
                            Error::info("Got a result!");
                        else
                            result.invalidate();
                    }
                }
            }

            if (!result.isValid() /** no valid result found yet */) {
                Error::info("=== Testing for places inside administrative boundaries ===");

                timer.start();
                const std::vector<uint64_t> adminReg = sweden->identifyAdministrativeRegions(word_combinations);
                if (!adminReg.empty()) {
                    const std::vector<struct TokenProcessor::AdminRegionMatch> adminRegionMatches = tokenProcessor.evaluateAdministrativeRegions(adminReg, word_combinations);
                    if (!adminRegionMatches.empty()) {
                        if (getCenterOfOSMElement(adminRegionMatches.front().match, result))
                            Error::info("Got a result for name: %s", adminRegionMatches.front().name.c_str());
                        else
                            result.invalidate();
                    }
                }
                timer.elapsed(&cputime, &walltime);
                Error::info("Spent CPU time to identify places inside administrative boundaries in testset '%s': %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", it->name.c_str(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
            }

            std::vector<struct OSMElement> places;
            if (!result.isValid() /** no valid result found yet */) {
                Error::info("=== Testing for local-scope places near global-scope places ===");

                timer.start();
                places = sweden->identifyPlaces(word_combinations);
                if (!places.empty()) {
                    const OSMElement::RealWorldType firstRwt = places.front().realworld_type;
                    for (auto it = ++places.cbegin(); it != places.cend();) {
                        if (it->realworld_type != firstRwt)
                            it = places.erase(it);
                        else
                            ++it;
                    }
                    const std::vector<struct TokenProcessor::NearPlaceMatch> nearPlacesMatches = tokenProcessor.evaluateNearPlaces(word_combinations, places);
                    if (!nearPlacesMatches.empty()) {
                        if (getCenterOfOSMElement(nearPlacesMatches.front().place, result))
                            Error::info("Got a result for place %llu and local node %llu", nearPlacesMatches.front().place.id, nearPlacesMatches.front().node);
                        else
                            result.invalidate();
                    }
                }
                timer.elapsed(&cputime, &walltime);
                Error::info("Spent CPU time to identify nearby places in testset '%s': %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", it->name.c_str(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
            }

            if (!result.isValid() /** no valid result found yet */) {
                Error::info("=== Testing word combination occurring only once (unique) in OSM data ===");

                timer.start();
                std::vector<struct TokenProcessor::UniqueMatch> uniqueMatches = tokenProcessor.evaluateUniqueMatches(word_combinations);
                timer.elapsed(&cputime, &walltime);
                Error::info("Spent CPU time to identify nearby places in testset '%s': %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", it->name.c_str(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);

                if (!uniqueMatches.empty()) {
                    if (node2Coord->retrieve(uniqueMatches.front().id, result))
                        Error::info("Got a result!");
                    else
                        result.invalidate();
                }

            }

            if (!result.isValid() && !places.empty()) {
                /// No good result found, but some places have been recognized in the process.
                /// Pick one of the larger places as result.
                Error::info("Several places are known, trying to pick a good one ...");
                // FIXME picking the right place from the list is rather ugly. Can do better?
                OSMElement::RealWorldType rwt = OSMElement::PlaceSmall;
                for (auto it = places.cbegin(); it != places.cend(); ++it) {
                    if (it->realworld_type == OSMElement::PlaceMedium && rwt >= OSMElement::PlaceSmall) {
                        node2Coord->retrieve(it->id, result);
                        rwt = it->realworld_type;
                    } else if (it->realworld_type < OSMElement::PlaceMedium && rwt >= OSMElement::PlaceMedium) {
                        node2Coord->retrieve(it->id, result);
                        rwt = it->realworld_type;
                    } else if (rwt != OSMElement::PlaceLarge && it->realworld_type == OSMElement::PlaceLargeArea) {
                        node2Coord->retrieve(it->id, result);
                        rwt = it->realworld_type;
                    } else if (rwt == OSMElement::PlaceLargeArea && it->realworld_type == OSMElement::PlaceLarge) {
                        node2Coord->retrieve(it->id, result);
                        rwt = it->realworld_type;
                    }
                }

                if (result.isValid())
                    Error::info("Got a result!");
            }

            if (result.isValid()) {
                const double lon = Coord::toLongitude(result.x);
                const double lat = Coord::toLatitude(result.y);
                Error::info("Able to determine a likely position: lon=%.5f lat=%.5f", lon, lat);
                Error::debug("  http://www.openstreetmap.org/?mlat=%.5f&mlon=%.5f#map=12/%.5f/%.5f", lat, lon, lat, lon);
                if (expected.isValid()) {
                    Error::info("Distance to expected result: %.1fkm", expected.distanceLatLon(result) / 1000.0);
                    const double lon = Coord::toLongitude(expected.x);
                    const double lat = Coord::toLatitude(expected.y);
                    Error::debug("  http://www.openstreetmap.org/?mlat=%.5f&mlon=%.5f#map=12/%.5f/%.5f", lat, lon, lat, lon);
                }
            } else
                Error::warn("Unable to determine a likely position");

            if (svgwriter != NULL) {
                svgwriter->drawPoint(expected.x, expected.y, SvgWriter::ImportantPoiGroup, "green", "expected");
                if (result.isValid())
                    svgwriter->drawPoint(result.x, result.y, SvgWriter::ImportantPoiGroup, "red", "computed");
                svgwriter->drawCaption(it->name);
                svgwriter->drawDescription(it->text);

                delete svgwriter; ///< destructor will finalize SVG file
                svgwriter = NULL;
            }

            Error::info("======================================================");
        }
    } else
        Error::err("No all variables got initialized correctly: relMembers, wayNodes, node2Coord, nodeNames, swedishTextTree, sweden");

    return 0;
}

