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

#include "resultgenerator.h"

#include <algorithm>

#include "tokenizer.h"
#include "tokenprocessor.h"
#include "globalobjects.h"
#include "helper.h"
#include "timer.h"

ResultGenerator::ResultGenerator() {
    tokenizer = new Tokenizer();
    tokenProcessor = new TokenProcessor();
}

ResultGenerator::~ResultGenerator() {
    delete tokenizer;
    delete tokenProcessor;
}

std::vector<Result> ResultGenerator::findResults(const std::string &text, int duplicateProximity, ResultGenerator::Verbosity verbosity) {
    std::vector<Result> results;
    Timer timer;
    int64_t cputime, walltime;

    timer.start();
    std::vector<std::string> words = tokenizer->read_words(text, Tokenizer::Duplicates);
    tokenizer->add_grammar_cases(words);
    const std::vector<std::string> word_combinations = tokenizer->generate_word_combinations(words, 3 /** TODO configurable */, Tokenizer::Unique);
    if (verbosity > VerbositySilent) {
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to tokenize text of length %d: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", text.length(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
    }

    /// ===================================================================================
    /// Check if the test input contains road labels (e.g. 'E 20') and city/town names.
    /// Then determine the clostest distance between any city/town and any identified road.
    /// If distance is below an acceptable threshold, assume location on road closest to
    /// town as resulting position.
    /// -----------------------------------------------------------------------------------
    if (verbosity > VerbositySilent)
        Error::info("=== Testing for roads close to cities/towns ===");

    const std::vector<struct Sweden::Road> identifiedRoads = sweden->identifyRoads(words);
    const std::vector<struct TokenProcessor::RoadMatch> roadMatches = tokenProcessor->evaluteRoads(word_combinations, identifiedRoads);

    for (const TokenProcessor::RoadMatch &roadMatch : roadMatches) {
        const int distance = roadMatch.distance;

        if (distance < 10000) {
            /// Closer than 10km
            Coord c;
            if (node2Coord->retrieve(roadMatch.bestRoadNode, c)) {
                Result r(c, roadMatch.quality, std::string("roadMatch: road:") + static_cast<std::string>(roadMatch.road) + " near " + roadMatch.word_combination);
                r.elements.push_back(OSMElement(roadMatch.bestRoadNode, OSMElement::Node, OSMElement::UnknownRealWorldType));
                r.elements.push_back(OSMElement(roadMatch.bestWordNode, OSMElement::Node, OSMElement::UnknownRealWorldType));
                results.push_back(r);
                if (verbosity > VerbositySilent)
                    Error::debug("Distance between '%s' and road %s: %.1f km (between road node %llu and word's node %llu)", roadMatch.word_combination.c_str(), roadMatch.road.operator std::string().c_str(), distance / 1000.0, roadMatch.bestRoadNode, roadMatch.bestWordNode);
            }
        }
    }


    if (verbosity > VerbositySilent) {
        Error::info("=== Testing for places inside administrative boundaries ===");
        timer.start();
    }
    const std::vector<struct Sweden::KnownAdministrativeRegion> adminReg = sweden->identifyAdministrativeRegions(word_combinations);
    if (!adminReg.empty()) {
        const std::vector<struct TokenProcessor::AdminRegionMatch> adminRegionMatches = tokenProcessor->evaluateAdministrativeRegions(adminReg, word_combinations);
        for (const struct TokenProcessor::AdminRegionMatch &adminRegionMatch : adminRegionMatches) {
            Coord c;
            if (getCenterOfOSMElement(adminRegionMatch.match, c)) {
                Result r(c, adminRegionMatch.quality * .95, std::string("Places inside admin bound: ") + adminRegionMatch.adminRegionName + " > " + adminRegionMatch.name);
                r.elements.push_back(adminRegionMatch.match);
                results.push_back(r);
                if (verbosity > VerbositySilent)
                    Error::debug("Found place %lld (%s) inside admin region %d (%s)", adminRegionMatch.match.id, adminRegionMatch.name.c_str(), adminRegionMatch.adminRegionId, adminRegionMatch.adminRegionName.c_str());
            }
        }
    }
    if (verbosity > VerbositySilent) {
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to identify places inside administrative boundaries: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
    }

    if (verbosity > VerbositySilent) {
        Error::info("=== Testing for local-scope places near global-scope places ===");
        timer.start();
    }
    std::vector<struct OSMElement> places;
    places = sweden->identifyPlaces(word_combinations);
    if (!places.empty()) {
        const OSMElement::RealWorldType firstRwt = places.front().realworld_type;
        for (auto it = ++places.cbegin(); it != places.cend();) {
            if (it->realworld_type != firstRwt)
                it = places.erase(it);
            else
                ++it;
        }
        const std::vector<struct TokenProcessor::NearPlaceMatch> nearPlacesMatches = tokenProcessor->evaluateNearPlaces(word_combinations, places);
        for (const struct TokenProcessor::NearPlaceMatch &nearPlacesMatch : nearPlacesMatches) {
            Coord c;
            if (node2Coord->retrieve(getNodeInOSMElement(nearPlacesMatch.local).id, c)) {
                Result r(c, nearPlacesMatch.quality * .75, std::string("Local/global places: ") + nearPlacesMatch.global.operator std::string() + " (" + nearPlacesMatch.global.name().c_str() + ") > " + nearPlacesMatch.local.operator std::string() + " (" + nearPlacesMatch.local.name().c_str() + ")");
                r.elements.push_back(nearPlacesMatch.global);
                r.elements.push_back(nearPlacesMatch.local);
                results.push_back(r);
                if (verbosity > VerbositySilent)
                    Error::debug("Got a result for place %s and local %s", nearPlacesMatch.global.operator std::string().c_str(), nearPlacesMatch.local.operator std::string().c_str());
            }
        }
    }
    if (verbosity > VerbositySilent) {
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to identify nearby places: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
    }


    if (verbosity > VerbositySilent) {
        Error::info("=== Testing word combination occurring only once (unique) in OSM data ===");
        timer.start();
    }
    std::vector<struct TokenProcessor::UniqueMatch> uniqueMatches = tokenProcessor->evaluateUniqueMatches(word_combinations);
    for (const struct TokenProcessor::UniqueMatch &uniqueMatch : uniqueMatches) {
        Coord c;
        if (node2Coord->retrieve(getNodeInOSMElement(uniqueMatch.element).id, c)) {
            Result r(c, uniqueMatch.quality * .8, std::string("Unique name: ") + uniqueMatch.name);
            r.elements.push_back(uniqueMatch.element);
            results.push_back(r);
            if (verbosity > VerbositySilent)
                Error::debug("Got a result for name '%s'!", uniqueMatch.name.c_str());
        }
    }
    if (verbosity > VerbositySilent) {
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to identify nearby places: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
    }


    if (!places.empty()) {
        /// No good result found, but some places have been recognized in the process.
        /// Pick one of the larger places as result.
        if (verbosity > VerbositySilent)
            Error::info("=== Several places are known, trying to pick a good one ===");
        // FIXME picking the right place from the list is rather ugly. Can do better?
        OSMElement bestPlace;
        OSMElement::RealWorldType rwt = OSMElement::PlaceSmall;
        for (const OSMElement &place : places) {
            if (place.realworld_type == OSMElement::PlaceMedium && rwt >= OSMElement::PlaceSmall) {
                bestPlace = place;
                rwt = place.realworld_type;
            } else if (place.realworld_type < OSMElement::PlaceMedium && rwt >= OSMElement::PlaceMedium) {
                bestPlace = place;
                rwt = place.realworld_type;
            } else if (rwt != OSMElement::PlaceLarge && place.realworld_type == OSMElement::PlaceLargeArea) {
                bestPlace = place;
                rwt = place.realworld_type;
            } else if (rwt == OSMElement::PlaceLargeArea && place.realworld_type == OSMElement::PlaceLarge) {
                bestPlace = place;
                rwt = place.realworld_type;
            }
        }

        if (bestPlace.isValid()) {
            const double quality = rwt == OSMElement::PlaceLarge ? 1.0 : (rwt == OSMElement::PlaceMedium?.9 : (rwt == OSMElement::PlaceLargeArea?.6 : (rwt == OSMElement::PlaceSmall?.8 : .5)));
            OSMElement node = getNodeInOSMElement(bestPlace);
            Coord c;
            if (node2Coord->retrieve(node.id, c)) {
                Result r(c, quality * .5, std::string("Large place: ") + bestPlace.name() + " (" + bestPlace.operator std::string() + ")");
                r.elements.push_back(bestPlace);
                results.push_back(r);
            }
        }
    }


    if (!results.empty()) {
        /// Sort results by quality (highest first)
        std::sort(results.begin(), results.end(), [](Result & a, Result & b) {
            return a.quality > b.quality;
        });
        if (duplicateProximity > 0)
            /// Remove results close to even better results
            for (auto outer = results.begin(); outer != results.end();) {
                bool removedOuter = false;
                const Result &outerR = *outer;
                for (auto inner = results.begin(); !removedOuter && inner != outer && inner != results.end(); ++inner) {
                    const Result &innerR = *inner;
                    const auto d = outerR.coord.distanceLatLon(innerR.coord);
                    if (d < duplicateProximity) {
                        /// Less than x meters away? Remove this result!
                        outer = results.erase(outer);
                        removedOuter = true;
                    }
                }
                if (!removedOuter)
                    ++outer;
            }
    }


    return results;
}
