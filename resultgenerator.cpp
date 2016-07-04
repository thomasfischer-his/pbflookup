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
#include <unordered_set>

#include "tokenizer.h"
#include "tokenprocessor.h"
#include "globalobjects.h"
#include "helper.h"
#include "timer.h"

const bool Result::operator==(const Result &r) const {
    return coord.x == r.coord.x && coord.y == r.coord.y && quality == r.quality && elements.size() == r.elements.size() && origin.length() == r.origin.length() && origin == r.origin;
}

const bool Result::operator<(const Result &r) const {
    if (coord.x < r.coord.x) return false;
    else if (coord.x > r.coord.x) return true;
    if (coord.y < r.coord.y) return false;
    else if (coord.y > r.coord.y) return true;
    if (quality < r.quality) return false;
    else if (quality > r.quality) return true;
    if (elements.size() < r.elements.size()) return false;
    else if (elements.size() > r.elements.size()) return true;
    if (origin.length() < r.origin.length()) return false;
    else if (origin.length() > r.origin.length()) return true;
    if (origin < r.origin) return false;
    else if (origin > r.origin) return true;
    return false;
}

/// Hash function, necessary e.g. for usage in std::unordered_set.
namespace std {
template <>
class hash<Result> {
public:
    size_t operator()(const Result &r) const {
        return hash<int>()(r.coord.x)
               ^ hash<int>()(r.coord.x)
               ^ hash<double>()(r.quality)
               ^ hash<std::string>()(r.origin)
               ^ hash<std::size_t>()(r.elements.size());
    }
};
}
ResultGenerator::ResultGenerator() {
    tokenizer = new Tokenizer();
    tokenProcessor = new TokenProcessor();
}

ResultGenerator::~ResultGenerator() {
    delete tokenizer;
    delete tokenProcessor;
}

std::vector<Result> ResultGenerator::findResults(const std::string &text, int duplicateProximity, ResultGenerator::Verbosity verbosity, ResultGenerator::Statistics *statistics) {
    std::vector<Result> results;
#ifdef CPUTIMER
    Timer timer, timerOverFunction;
    int64_t cputime, walltime;
#endif // CPUTIMER

#ifdef CPUTIMER
    timer.start();
    timerOverFunction.start();
#endif // CPUTIMER
    const std::vector<std::string> words = tokenizer->read_words(text, Tokenizer::Duplicates);
    const std::vector<std::string> word_combinations = tokenizer->generate_word_combinations(words, 3 /** TODO configurable */);
    Error::info("Identified %d words, resulting in %d word combinations", words.size(), word_combinations.size());
    if (statistics != nullptr) {
        statistics->word_count = words.size();
        statistics->word_combinations_count = word_combinations.size();
    }
#ifdef CPUTIMER
    if (verbosity > VerbositySilent) {
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to tokenize text of length %d: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", text.length(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
    }
#endif // CPUTIMER

    /// ===================================================================================
    /// Check if the test input contains road labels (e.g. 'E 20') and city/town names.
    /// Then determine the clostest distance between any city/town and any identified road.
    /// If distance is below an acceptable threshold, assume location on road closest to
    /// town as resulting position.
    /// -----------------------------------------------------------------------------------
    if (verbosity > VerbositySilent) {
        Error::info("=== Testing for roads close to cities/towns ===");
#ifdef CPUTIMER
        timer.start();
#endif // CPUTIMER
    }

    const std::vector<struct Sweden::Road> identifiedRoads = sweden->identifyRoads(words);
    Error::info("Identified roads: %d", identifiedRoads.size());
    const std::vector<struct TokenProcessor::RoadMatch> roadMatches = tokenProcessor->evaluteRoads(word_combinations, identifiedRoads);
    Error::info("Identified road matches: %d", roadMatches.size());

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
#ifdef CPUTIMER
    if (verbosity > VerbositySilent) {
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to identify roads close to cities/towns: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
    }
#endif // CPUTIMER


    if (verbosity > VerbositySilent) {
        Error::info("=== Testing for places inside administrative boundaries ===");
#ifdef CPUTIMER
        timer.start();
#endif // CPUTIMER
    }
    const std::vector<struct Sweden::KnownAdministrativeRegion> adminReg = sweden->identifyAdministrativeRegions(word_combinations);
    Error::info("Identified administrative regions: %d", adminReg.size());
    if (!adminReg.empty()) {
        const std::vector<struct TokenProcessor::AdminRegionMatch> adminRegionMatches = tokenProcessor->evaluateAdministrativeRegions(adminReg, word_combinations);
        Error::info("Identified administrative region matches: %d", adminReg.size());
        for (const struct TokenProcessor::AdminRegionMatch &adminRegionMatch : adminRegionMatches) {
            Coord c;
            if (getCenterOfOSMElement(adminRegionMatch.match, c)) {
                WriteableString matchName("UNSET");
                switch (adminRegionMatch.match.type) {
                case OSMElement::Node: nodeNames->retrieve(adminRegionMatch.match.id, matchName); break;
                case OSMElement::Way: wayNames->retrieve(adminRegionMatch.match.id, matchName); break;
                case OSMElement::Relation: relationNames->retrieve(adminRegionMatch.match.id, matchName); break;
                case OSMElement::UnknownElementType: matchName = WriteableString("Unknown"); break;
                }

                Result r(c, adminRegionMatch.quality * .95, std::string("Places inside admin bound: ") + adminRegionMatch.adminRegion.name + " (relation " + std::to_string(adminRegionMatch.adminRegion.relationId) + ") > '" + matchName + "' (" + adminRegionMatch.match.operator std::string() + ", found via: '" + adminRegionMatch.combined + "')");
                r.elements.push_back(OSMElement(adminRegionMatch.adminRegion.relationId, OSMElement::Relation));
                r.elements.push_back(adminRegionMatch.match);
                results.push_back(r);
                if (verbosity > VerbositySilent)
                    Error::debug("Found place '%s' (%s) inside admin region '%s' (%d) via combination '%s'", matchName.c_str(), adminRegionMatch.match.operator std::string().c_str(), adminRegionMatch.adminRegion.name.c_str(), adminRegionMatch.adminRegion.relationId, adminRegionMatch.combined.c_str(), adminRegionMatch.combined.c_str());
            }
        }
    }
#ifdef CPUTIMER
    if (verbosity > VerbositySilent) {
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to identify places inside administrative boundaries: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
    }
#endif // CPUTIMER

    if (verbosity > VerbositySilent) {
        Error::info("=== Testing for local-scope places near global-scope places ===");
#ifdef CPUTIMER
        timer.start();
#endif // CPUTIMER
    }
    std::vector<struct OSMElement> globalPlaces = sweden->identifyPlaces(word_combinations);
    Error::info("Identified global places: %d", globalPlaces.size());
    if (!globalPlaces.empty()) {
        const OSMElement::RealWorldType firstRwt = globalPlaces.front().realworld_type;
        for (auto it = ++globalPlaces.cbegin(); it != globalPlaces.cend();) {
            if (it->realworld_type != firstRwt)
                it = globalPlaces.erase(it);
            else
                ++it;
        }
        const std::vector<struct TokenProcessor::LocalPlaceMatch> localPlacesMatches = tokenProcessor->evaluateNearPlaces(word_combinations, globalPlaces);
        Error::info("Identified local places matches: %d", localPlacesMatches.size());
        for (const struct TokenProcessor::LocalPlaceMatch &localPlacesMatch : localPlacesMatches) {
            Coord c;
            if (getCenterOfOSMElement(localPlacesMatch.local, c)) {
                Result r(c, localPlacesMatch.quality * .75, std::string("Local near global place: ") + localPlacesMatch.local.operator std::string() + " ('" + localPlacesMatch.local.name() + "') near " + localPlacesMatch.global.operator std::string() + " ('" + localPlacesMatch.global.name() + "')");
                r.elements.push_back(localPlacesMatch.global);
                r.elements.push_back(localPlacesMatch.local);
                results.push_back(r);
                if (verbosity > VerbositySilent)
                    Error::debug("Got a result for global place '%s' and local place '%s'", localPlacesMatch.global.operator std::string().c_str(), localPlacesMatch.local.operator std::string().c_str());
            }
        }
    }
#ifdef CPUTIMER
    if (verbosity > VerbositySilent) {
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to identify local/global places: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
    }
#endif // CPUTIMER


    if (verbosity > VerbositySilent) {
        Error::info("=== Testing word combination occurring only once (unique) in OSM data ===");
#ifdef CPUTIMER
        timer.start();
#endif // CPUTIMER
    }
    std::vector<struct TokenProcessor::UniqueMatch> uniqueMatches = tokenProcessor->evaluateUniqueMatches(word_combinations);
    Error::info("Identified unique matches: %d", uniqueMatches.size());
    for (const struct TokenProcessor::UniqueMatch &uniqueMatch : uniqueMatches) {
        Coord c;
        if (getCenterOfOSMElement(uniqueMatch.element, c)) {
            Result r(c, uniqueMatch.quality * .8, std::string("Unique name '") + uniqueMatch.element.name().c_str() + "' (" + uniqueMatch.element.operator std::string() + ") found via '" + uniqueMatch.combined + "'");
            r.elements.push_back(uniqueMatch.element);
            results.push_back(r);
            if (verbosity > VerbositySilent)
                Error::debug("Got a result for combined word '%s': %s (%s)", uniqueMatch.combined.c_str(), uniqueMatch.element.name().c_str(), uniqueMatch.element.operator std::string().c_str());
        }
    }
#ifdef CPUTIMER
    if (verbosity > VerbositySilent) {
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to identify unique places: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
    }
#endif // CPUTIMER


    if (!globalPlaces.empty()) {
        /// No good result found, but some places have been recognized in the process.
        /// Pick one of the larger places as result.
        if (verbosity > VerbositySilent) {
            Error::info("=== Testing any known places, trying to pick a good one ===");
#ifdef CPUTIMER
            timer.start();
#endif // CPUTIMER
        }
        // FIXME picking the right place from the list is rather ugly. Can do better?
        OSMElement bestPlace;
        OSMElement::RealWorldType rwt = OSMElement::PlaceSmall;
        for (const OSMElement &place : globalPlaces) {
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
            Coord c;
            if (getCenterOfOSMElement(bestPlace, c)) {
                Result r(c, quality * .5, std::string("Large place: ") + bestPlace.name() + " (" + bestPlace.operator std::string() + ")");
                r.elements.push_back(bestPlace);
                results.push_back(r);
                if (verbosity > VerbositySilent)
                    Error::debug("Best place is %s (%s)", bestPlace.name().c_str(), bestPlace.operator std::string().c_str());
            }
        }
#ifdef CPUTIMER
        if (verbosity > VerbositySilent) {
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to identify known places: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
        }
#endif // CPUTIMER
    }


    if (!results.empty()) {
        if (verbosity > VerbositySilent) {
            Error::info("=== Sorting and cleaning results ===");
#ifdef CPUTIMER
            timer.start();
#endif // CPUTIMER
        }
        if (duplicateProximity > 0) {
            const int64_t duplicateProximitySquare = (int64_t)duplicateProximity * (int64_t)duplicateProximity;
            /// Remove results close to even better results
            std::unordered_set<Result> result_set(results.cbegin(), results.cend());
            for (auto outer = result_set.cbegin(); outer != result_set.cend();) {
                bool removedOuter = false;
                const Result &outerR = *outer;
                for (auto inner = result_set.cbegin(); !removedOuter && inner != result_set.cend(); ++inner) {
                    if (inner == outer) continue;
                    const Result &innerR = *inner;
                    if (outerR.quality > innerR.quality) continue; ///< avoid removing results of higher quality
                    const auto d = Coord::distanceXYsquare(outerR.coord, innerR.coord);
                    if (d < duplicateProximitySquare) {
                        /// Less than x meters away? Remove this result!
                        outer = result_set.erase(outer);
                        removedOuter = true;
                    }
                }
                if (!removedOuter)
                    ++outer;
            }
            results.assign(result_set.cbegin(), result_set.cend());
        }
        /// Sort results by quality (highest first)
        std::sort(results.begin(), results.end(), [](Result & a, Result & b) {
            return a.quality > b.quality;
        });
#ifdef CPUTIMER
        if (verbosity > VerbositySilent) {
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to clean/sort results: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
        }
#endif // CPUTIMER
    }

#ifdef CPUTIMER
    timerOverFunction.elapsed(&cputime, &walltime);
    Error::info("%d results, time %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", results.size(), cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
#else // CPUTIMER
    Error::debug("%d results", results.size());
#endif // CPUTIMER

    if (verbosity > VerbositySilent)
        Error::info("=== Done generating results ===");

    return results;
}
