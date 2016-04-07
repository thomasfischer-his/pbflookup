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

#include "global.h"
#include "globalobjects.h"
#include "config.h"
#include "svgwriter.h"
#include "tokenprocessor.h"
#include "tokenizer.h"
#include "timer.h"
#include "helper.h"

void Testset::run() {
    Timer timer;
    int64_t cputime, walltime;

    int setNr = 0;
    for (auto it = testsets.cbegin(); it != testsets.cend(); ++it, ++setNr) {
        struct Result {
            Result(const Coord &_coord, double _quality, const std::string &_origin)
                : coord(_coord), quality(_quality), origin(_origin) {
                /** nothing */
            }

            Coord coord;
            double quality;
            std::string origin;
        };
        std::vector<Result> results;
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
                    Error::debug("Distance between '%s' and road %s: %.1f km (between road node %llu and word's node %llu)", roadMatch.word_combination.c_str(), roadMatch.road.operator std::string().c_str(), distance / 1000.0, roadMatch.bestRoadNode, roadMatch.bestWordNode);
                    results.push_back(Result(c, roadMatch.quality, std::string("roadMatch: road:") + static_cast<std::string>(roadMatch.road) + " near " + roadMatch.word_combination));
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
                    Error::debug("Found place %lld (%s) inside admin region %d (%s)", adminRegionMatch.match.id, adminRegionMatch.name.c_str(), adminRegionMatch.adminRegionId, adminRegionMatch.adminRegionName.c_str());
                    results.push_back(Result(c, adminRegionMatch.quality * .95, std::string("Places inside admin bound: ") + adminRegionMatch.adminRegionName + " > " + adminRegionMatch.name));
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
                    Error::debug("Got a result for place %llu and local node %llu", nearPlacesMatch.global.id, nearPlacesMatch.local.id);
                    WriteableString globalName, localName;
                    nodeNames->retrieve(nearPlacesMatch.global.id, globalName);
                    nodeNames->retrieve(nearPlacesMatch.local.id, localName);
                    results.push_back(Result(c, nearPlacesMatch.quality * .75, std::string("Local/global places: ") + std::to_string(nearPlacesMatch.global.id) + " (" + globalName.c_str() + ") > " + std::to_string(nearPlacesMatch.local.id) + " (" + localName.c_str() + ")"));
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
                Error::debug("Got a result for name '%s'!", uniqueMatch.name.c_str());
                results.push_back(Result(c, uniqueMatch.quality * .8, std::string("Unique name: ") + uniqueMatch.name));
            }
        }

        if (!places.empty()) {
            /// No good result found, but some places have been recognized in the process.
            /// Pick one of the larger places as result.
            Error::info("=== Several places are known, trying to pick a good one ===");
            // FIXME picking the right place from the list is rather ugly. Can do better?
            OSMElement::RealWorldType rwt = OSMElement::PlaceSmall;
            Coord c;
            uint64_t bestId = 0;
            for (auto it = places.cbegin(); it != places.cend(); ++it) {
                if (it->realworld_type == OSMElement::PlaceMedium && rwt >= OSMElement::PlaceSmall) {
                    bestId = it->id;
                    node2Coord->retrieve(bestId, c);
                    rwt = it->realworld_type;
                } else if (it->realworld_type < OSMElement::PlaceMedium && rwt >= OSMElement::PlaceMedium) {
                    bestId = it->id;
                    node2Coord->retrieve(bestId, c);
                    rwt = it->realworld_type;
                } else if (rwt != OSMElement::PlaceLarge && it->realworld_type == OSMElement::PlaceLargeArea) {
                    bestId = it->id;
                    node2Coord->retrieve(bestId, c);
                    rwt = it->realworld_type;
                } else if (rwt == OSMElement::PlaceLargeArea && it->realworld_type == OSMElement::PlaceLarge) {
                    bestId = it->id;
                    node2Coord->retrieve(bestId, c);
                    rwt = it->realworld_type;
                }
            }

            if (c.isValid()) {
                const double quality = rwt == OSMElement::PlaceLarge ? 1.0 : (rwt == OSMElement::PlaceMedium?.9 : (rwt == OSMElement::PlaceLargeArea?.6 : (rwt == OSMElement::PlaceSmall?.8 : .5)));
                WriteableString placeName;
                nodeNames->retrieve(bestId, placeName);
                results.push_back(Result(c, quality * .5, std::string("Large place: ") + placeName));
            }
        }

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
