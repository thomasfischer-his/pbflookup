/***************************************************************************
 *   Copyright (C) 2015-2016 by Thomas Fischer <thomas.fischer@his.se>     *
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

#ifndef TOKENPROCESSOR_H
#define TOKENPROCESSOR_H

#include "sweden.h"

class TokenProcessor
{
public:
    explicit TokenProcessor();
    ~TokenProcessor();

    struct RoadMatch {
        RoadMatch(const std::string &_word_combination, const Sweden::Road &_road, uint64_t _bestRoadNode, uint64_t _bestWordNode, int _distance, double _quality = -1.0)
            : word_combination(_word_combination), road(_road), bestRoadNode(_bestRoadNode), bestWordNode(_bestWordNode), distance(_distance), quality(_quality) {
            /// nothing
        }

        std::string word_combination;
        Sweden::Road road;
        uint64_t bestRoadNode, bestWordNode;
        int distance; ///< Distance in meters as measured on decimeter grid

        /** Assessment on match's quality, ranging from
         *    1.0  -> very good
         *  to
         *    0.0  -> very poor
         *  Negative value means quality not set.
         */
        double quality;
    };

    std::vector<struct RoadMatch> evaluteRoads(const std::vector<std::string> &word_combinations, const std::vector<struct Sweden::Road> knownRoads);

    struct LocalPlaceMatch {
        LocalPlaceMatch(const std::string &_word_combination, const struct OSMElement &_global, const struct OSMElement &_local, int _distance, double _quality = -1.0)
            : word_combination(_word_combination), global(_global), local(_local), distance(_distance), quality(_quality) {
            /// nothing
        }

        std::string word_combination;
        struct OSMElement global;
        struct OSMElement local;
        int distance; ///< in meter

        /** Assessment on match's quality, ranging from
         *    1.0  -> very good
         *  to
         *    0.0  -> very poor
         *  Negative value means quality not set.
         */
        double quality;
    };

    /**
     * Based on a list of word combinations and a list of places,
     * determine a list of place-word combo pairs that match
     * local-scope locations (as described by word combinations)
     * and global-scope locations (places).
     * The resulting list will be sorted by distance between place
     * and word combination's closest location, shorter distances
     * first.
     * @param word_combinations Word combinations describing local-scope locations
     * @param places Places of cities, towns, hamlets, ...
     * @return List of matching pairs of place-word combinations
     */
    std::vector<struct LocalPlaceMatch> evaluateNearPlaces(const std::vector<std::string> &word_combinations, const std::vector<struct OSMElement> &places);

    struct UniqueMatch {
        UniqueMatch(std::string _combined, const OSMElement &_element, double _quality)
            : combined(_combined), element(_element), quality(_quality) {
            /// nothing
        }

        std::string combined;
        OSMElement element;

        /** Assessment on match's quality, ranging from
         *    1.0  -> very good
         *  to
         *    0.0  -> very poor
         *  Negative value means quality not set.
         */
        double quality;
    };

    std::vector<struct UniqueMatch> evaluateUniqueMatches(const std::vector<std::string> &word_combinations) const;

    struct AdminRegionMatch {
        AdminRegionMatch(const std::string &_combined, const OSMElement &_match, const Sweden::KnownAdministrativeRegion &_adminRegion, double _quality = -1.0)
            : combined(_combined), match(_match), adminRegion(_adminRegion), quality(_quality) {
            /// nothing
        }

        std::string combined;
        OSMElement match;
        Sweden::KnownAdministrativeRegion adminRegion;

        /** Assessment on match's quality, ranging from
         *    1.0  -> very good
         *  to
         *    0.0  -> very poor
         *  Negative value means quality not set.
         */
        double quality;
    };

    /**
     * Cross-referencing all known administrative regions
     * (municipalities, counties, ...) and all matching
     * nodes/ways/relations for each item taken from a list
     * of word combinations (usually 1..3 words combined).
     *
     * The list of matches will be sorted by the number of
     * spaces in the word combination that returned matches,
     * where word combinations with more spaces are preferred
     * (it is assumed that more words in a combination make
     * the combination more specific and as such a better hit).
     *
     * @param adminRegions List of relation ids referring to administrative regions
     * @param word_combinations List of word combinations
     * @return Matches of admin region and word combination, sorted by as described above
     */
    std::vector<struct AdminRegionMatch> evaluateAdministrativeRegions(const std::vector<struct Sweden::KnownAdministrativeRegion> adminRegions, const std::vector<std::string> &word_combinations) const;

private:
    class Private;
    Private *const d;
};

#endif // TOKENPROCESSOR_H
