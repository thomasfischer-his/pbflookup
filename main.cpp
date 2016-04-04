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
            std::vector<std::pair<Coord, double> > results;
            Error::info("Test set: %s", it->name.c_str());
            const std::vector<Coord> &expected = it->coord;

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

            /// ===================================================================================
            /// Check if the test input contains road labels (e.g. 'E 20') and city/town names.
            /// Then determine the clostest distance between any city/town and any identified road.
            /// If distance is below an acceptable threshold, assume location on road closest to
            /// town as resulting position.
            /// -----------------------------------------------------------------------------------
            Error::info("=== Testing for roads close to cities/towns ===");

            timer.start();
            std::vector<struct Sweden::Road> identifiedRoads = sweden->identifyRoads(words);
            std::vector<struct TokenProcessor::RoadMatch> roadMatches = tokenProcessor.evaluteRoads(word_combinations, identifiedRoads);
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to identify roads in testset '%s': %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", it->name.c_str(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);

            for (const TokenProcessor::RoadMatch &roadMatch : roadMatches) {
                const int distance = roadMatch.distance;

                if (distance < 10000) {
                    /// Closer than 10km
                    Coord c;
                    if (node2Coord->retrieve(roadMatch.bestRoadNode, c)) {
                        Error::info("Distance between '%s' and road %s %d: %.1f km (between road node %llu and word's node %llu)", roadMatch.word_combination.c_str(), Sweden::roadTypeToString(roadMatch.road.type).c_str(), roadMatch.road.number, distance / 1000.0, roadMatch.bestRoadNode, roadMatch.bestWordNode);
                        results.push_back(std::pair<Coord, double>(c, roadMatch.quality));
                    }
                }
            }


            Error::info("=== Testing for places inside administrative boundaries ===");

            timer.start();
            const std::vector<struct Sweden::KnownAdministrativeRegion> adminReg = sweden->identifyAdministrativeRegions(word_combinations);
            if (!adminReg.empty()) {
                const std::vector<struct TokenProcessor::AdminRegionMatch> adminRegionMatches = tokenProcessor.evaluateAdministrativeRegions(adminReg, word_combinations);
                for (const struct TokenProcessor::AdminRegionMatch &adminRegionMatch : adminRegionMatches) {
                    Coord c;
                    if (getCenterOfOSMElement(adminRegionMatch.match, c)) {
                        Error::info("Found place %lld (%s) inside admin region %d (%s)", adminRegionMatch.match.id, adminRegionMatch.name.c_str(), adminRegionMatch.adminRegionId, adminRegionMatch.adminRegionName.c_str());
                        results.push_back(std::pair<Coord, double>(c, adminRegionMatch.quality * .95));
                    }
                }
            }
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to identify places inside administrative boundaries in testset '%s': %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", it->name.c_str(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);


            std::vector<struct OSMElement> places;
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
                for (const struct TokenProcessor::NearPlaceMatch &nearPlacesMatch : nearPlacesMatches) {
                    Coord c;
                    if (node2Coord->retrieve(nearPlacesMatch.local.id, c)) {
                        Error::info("Got a result for place %llu and local node %llu", nearPlacesMatch.global.id, nearPlacesMatch.local.id);
                        results.push_back(std::pair<Coord, double>(c, nearPlacesMatch.quality * .75));
                    }
                }
            }
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to identify nearby places in testset '%s': %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", it->name.c_str(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);


            Error::info("=== Testing word combination occurring only once (unique) in OSM data ===");

            timer.start();
            std::vector<struct TokenProcessor::UniqueMatch> uniqueMatches = tokenProcessor.evaluateUniqueMatches(word_combinations);
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to identify nearby places in testset '%s': %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", it->name.c_str(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);

            for (const struct TokenProcessor::UniqueMatch &uniqueMatch : uniqueMatches) {
                Coord c;
                if (node2Coord->retrieve(uniqueMatch.element.id, c)) {
                    Error::info("Got a result for name '%s'!", uniqueMatch.name.c_str());
                    results.push_back(std::pair<Coord, double>(c, uniqueMatch.quality * .8));
                }
            }

            if (results.empty()) {
                /// No good result found, but some places have been recognized in the process.
                /// Pick one of the larger places as result.
                Error::info("=== Several places are known, trying to pick a good one ===");
                // FIXME picking the right place from the list is rather ugly. Can do better?
                OSMElement::RealWorldType rwt = OSMElement::PlaceSmall;
                Coord c;
                for (auto it = places.cbegin(); it != places.cend(); ++it) {
                    if (it->realworld_type == OSMElement::PlaceMedium && rwt >= OSMElement::PlaceSmall) {
                        node2Coord->retrieve(it->id, c);
                        rwt = it->realworld_type;
                    } else if (it->realworld_type < OSMElement::PlaceMedium && rwt >= OSMElement::PlaceMedium) {
                        node2Coord->retrieve(it->id, c);
                        rwt = it->realworld_type;
                    } else if (rwt != OSMElement::PlaceLarge && it->realworld_type == OSMElement::PlaceLargeArea) {
                        node2Coord->retrieve(it->id, c);
                        rwt = it->realworld_type;
                    } else if (rwt == OSMElement::PlaceLargeArea && it->realworld_type == OSMElement::PlaceLarge) {
                        node2Coord->retrieve(it->id, c);
                        rwt = it->realworld_type;
                    }
                }

                if (c.isValid()) {
                    const double quality = rwt == OSMElement::PlaceLarge ? 1.0 : (rwt == OSMElement::PlaceMedium?.9 : (rwt == OSMElement::PlaceLargeArea?.6 : (rwt == OSMElement::PlaceSmall?.8 : .5)));
                    results.push_back(std::pair<Coord, double>(c, quality * .5));
                }
            }

            if (!results.empty()) {
                std::sort(results.begin(), results.end(), [](std::pair<Coord, double> &a, std::pair<Coord, double> &b) {
                    return a.second > b.second;
                });


                Error::info("Found %d many possible results:", results.size());
                for (const std::pair<Coord, double> p : results) {
                    const Coord &coord = p.first;
                    const double quality = p.second;
                    const double lon = Coord::toLongitude(coord.x);
                    const double lat = Coord::toLatitude(coord.y);
                    Error::info("Able to determine a likely position with quality %.5lf: lon=%.5f lat=%.5f", quality, lon, lat);
                    Error::debug("  http://www.openstreetmap.org/?mlat=%.5f&mlon=%.5f#map=12/%.5f/%.5f", lat, lon, lat, lon);
                    for (const Coord &exp : expected)
                        if (exp.isValid()) {
                            Error::info("Distance to expected result: %.1fkm", exp.distanceLatLon(coord) / 1000.0);
                            const double lon = Coord::toLongitude(exp.x);
                            const double lat = Coord::toLatitude(exp.y);
                            Error::debug("  http://www.openstreetmap.org/?mlat=%.5f&mlon=%.5f#map=12/%.5f/%.5f", lat, lon, lat, lon);
                        }
                }
            } else
                Error::warn("Unable to determine a likely position");

            if (svgwriter != NULL) {
                for (const Coord &exp : expected)
                    svgwriter->drawPoint(exp.x, exp.y, SvgWriter::ImportantPoiGroup, "green", "expected");
                for (const std::pair<Coord, double> p : results)
                    svgwriter->drawPoint(p.first.x, p.first.y, SvgWriter::ImportantPoiGroup, "red", "computed");
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

