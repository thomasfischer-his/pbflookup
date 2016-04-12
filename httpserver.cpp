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

#include "httpserver.h"

#include <cstring>
#include <cstdlib>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "global.h"
#include "globalobjects.h"
#include "helper.h"
#include "timer.h"
#include "tokenizer.h"
#include "tokenprocessor.h"
#include "config.h"
#include "error.h"

int serverSocket;

/// Taken from
///  http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames#C.2FC.2B.2B
int long2tilex(double lon, int z)
{
    return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z)));
}

int lat2tiley(double lat, int z)
{
    return (int)(floor((1.0 - log(tan(lat * M_PI / 180.0) + 1.0 / cos(lat * M_PI / 180.0)) / M_PI) / 2.0 * pow(2.0, z)));
}


void HTTPServer::run() {
    /** Turn off bind address checking, and allow port numbers to be reused
     *  - otherwise the TIME_WAIT phenomenon will prevent binding to these
     *  address.port combinations for (2 * MSL) seconds.
     */

    int on = 1;
    int status = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char *) &on, sizeof(on));
    if (status == -1)
        Error::err("setsockopt(...,SO_REUSEADDR,...)");

    struct sockaddr_in serverName;
    memset(&serverName, 0, sizeof(serverName));
    serverName.sin_family = AF_INET;
    serverName.sin_port = htons(http_port);
    if (http_interface[0] == 'L' && http_interface[1] == 'O' && http_interface[2] == 'C' && http_interface[3] == 'A' && http_interface[4] == 'L')
        serverName.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    else if (http_interface[0] == 'A' && http_interface[1] == 'N' && http_interface[2] == 'Y')
        serverName.sin_addr.s_addr = htonl(INADDR_ANY);
    else {
        if (inet_aton(http_interface, &serverName.sin_addr) == 0) {
            Error::warn("Provided http_interface '%s' is invalid, using local loopback instead", http_interface);
            serverName.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
    }
    /// 'my_addr.sin_zero' set to zeros by above memset command

    status = bind(serverSocket, (struct sockaddr *) &serverName, sizeof(serverName));
    if (status == -1)
        Error::err("bind()");

    status = listen(serverSocket, 10);
    if (status == -1)
        Error::err("listen()");

    bool doexitserver = false;
    fd_set readfds;
    Error::info("HTTP Server awaits connection attempts on port %d", http_port);
    /// Extract four bytes for IPv4 address; for ANY use '127.0.0.1'
    const unsigned char a = serverName.sin_addr.s_addr == 0x0 ? 127 : serverName.sin_addr.s_addr & 255;
    const unsigned char b = serverName.sin_addr.s_addr == 0x0 ? 0 : (serverName.sin_addr.s_addr >> 8) & 255;
    const unsigned char c = serverName.sin_addr.s_addr == 0x0 ? 0 : (serverName.sin_addr.s_addr >> 16) & 255;
    const unsigned char d = serverName.sin_addr.s_addr == 0x0 ? 1 : (serverName.sin_addr.s_addr >> 24) & 255;
    Error::debug("Try http://%d.%d.%d.%d:%d/ to reach it", a, b, c, d, htons(serverName.sin_port));

    Error::info("Enter 'quit' to quit HTTP server");
    while (!doexitserver) {
        FD_ZERO(&readfds);
        FD_SET(serverSocket, &readfds);
        FD_SET(0, &readfds);
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        if (select(serverSocket + 1, &readfds, NULL /** writefds */, NULL /** errorfds */, &timeout) < 0) {
            Error::err("select(...)");
        }

        if (FD_ISSET(0, &readfds)) {
            /// User input from stdin
            static const size_t buffer_size = 1024;
            char buffer[buffer_size];
            fgets(buffer, buffer_size, stdin);
            if (buffer[0] == 'q' && buffer[1] == 'u' && buffer[2] == 'i' && buffer[3] == 't') {
                /// User wants to quit server
                doexitserver = true;
                Error::info("Will quit HTTP server");
            }
        } else if (FD_ISSET(serverSocket, &readfds)) {
            /// Connection attempt on server
            socklen_t sockaddr_in_size = sizeof(struct sockaddr_in);
            struct sockaddr_in their_addr;
            int slaveSocket = accept(serverSocket, (struct sockaddr *) &their_addr, &sockaddr_in_size);
            if (slaveSocket == -1)
                continue;

            switch (fork()) {
            case -1:
            {
                Error::err("Error while forking");
                exit(1);
            }
            case 0:
            {
                /// Child process
                close(serverSocket);

                static const size_t readbuffer_size = 16384;
                char readbuffer[readbuffer_size], text[readbuffer_size];
                read(slaveSocket, readbuffer, readbuffer_size);

                if (readbuffer[0] == 'G' && readbuffer[1] == 'E' && readbuffer[2] == 'T' && readbuffer[3] == ' ') {
                    dprintf(slaveSocket, "HTTP/1.1 200 OK\n");
                    dprintf(slaveSocket, "Content-Type: text/html; charset=utf-8\n");
                    dprintf(slaveSocket, "\n<html><head>\n");
                    dprintf(slaveSocket, "<title>Search for Locations described in Swedish Text</title>\n");
                    dprintf(slaveSocket, "<script type=\"text/javascript\">\nfunction testsetChanged(combo) {\n  document.getElementById('textarea').value=combo.value;\n}\n</script>\n");
                    dprintf(slaveSocket, "</head>\n");
                    dprintf(slaveSocket, "<body>\n");
                    dprintf(slaveSocket, "<h1>Search for Locations described in Swedish Text</h1>\n");
                    dprintf(slaveSocket, "<form enctype=\"text/plain\" accept-charset=\"utf-8\" action=\".\" method=\"post\">\n");
                    if (!testsets.empty()) {
                        dprintf(slaveSocket, "<p>Either select a pre-configured text from this list: <select onchange=\"testsetChanged(this)\" id=\"testsets\">\n");
                        dprintf(slaveSocket, "<option selected=\"selected\" disabled=\"disabled\" hidden=\"hidden\" value=\"\"></option>");
                        for (const struct testset &t : testsets)
                            dprintf(slaveSocket, "<option value=\"%s\">%s</option>", t.text.c_str(), t.name.c_str());
                        dprintf(slaveSocket, "</select> or &hellip;</p>\n");
                    }
                    dprintf(slaveSocket, "<p>Enter a Swedish text to localize:<br/><textarea name=\"text\" id=\"textarea\" cols=\"60\" rows=\"8\" placeholder=\"Write your Swedish text here\"></textarea></p>\n");
                    dprintf(slaveSocket, "<p><input type=\"submit\" value=\"Find location for text\"></p>\n");
                    dprintf(slaveSocket, "</form>\n");
                    dprintf(slaveSocket, "</body></html>\n\n\n");
                } else if (readbuffer[0] == 'P' && readbuffer[1] == 'O' && readbuffer[2] == 'S' && readbuffer[3] == 'T' && readbuffer[4] == ' ') {
                    strncpy(text, strstr(readbuffer, "\ntext=") + 6, readbuffer_size);

                    Timer timer;
                    int64_t cputime, walltime;

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

                    timer.start();

                    Tokenizer tokenizer;
                    TokenProcessor tokenProcessor;

                    std::vector<std::string> words, word_combinations;
                    tokenizer.read_words(text, words, Tokenizer::Duplicates);
                    tokenizer.add_grammar_cases(words);
                    tokenizer.generate_word_combinations(words, word_combinations, 3 /** TODO configurable */, Tokenizer::Unique);

                    std::vector<struct Sweden::Road> identifiedRoads = sweden->identifyRoads(words);
                    std::vector<struct TokenProcessor::RoadMatch> roadMatches = tokenProcessor.evaluteRoads(word_combinations, identifiedRoads);

                    for (const TokenProcessor::RoadMatch &roadMatch : roadMatches) {
                        const int distance = roadMatch.distance;

                        if (distance < 10000) {
                            /// Closer than 10km
                            Coord c;
                            if (node2Coord->retrieve(roadMatch.bestRoadNode, c)) {
                                results.push_back(Result(c, roadMatch.quality, std::string("roadMatch: road:") + static_cast<std::string>(roadMatch.road) + " near " + roadMatch.word_combination));
                            }
                        }
                    }


                    const std::vector<struct Sweden::KnownAdministrativeRegion> adminReg = sweden->identifyAdministrativeRegions(word_combinations);
                    if (!adminReg.empty()) {
                        const std::vector<struct TokenProcessor::AdminRegionMatch> adminRegionMatches = tokenProcessor.evaluateAdministrativeRegions(adminReg, word_combinations);
                        for (const struct TokenProcessor::AdminRegionMatch &adminRegionMatch : adminRegionMatches) {
                            Coord c;
                            if (getCenterOfOSMElement(adminRegionMatch.match, c)) {
                                results.push_back(Result(c, adminRegionMatch.quality * .95, std::string("Places inside admin bound: ") + adminRegionMatch.adminRegionName + " > " + adminRegionMatch.name));
                            }
                        }
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
                        const std::vector<struct TokenProcessor::NearPlaceMatch> nearPlacesMatches = tokenProcessor.evaluateNearPlaces(word_combinations, places);
                        for (const struct TokenProcessor::NearPlaceMatch &nearPlacesMatch : nearPlacesMatches) {
                            Coord c;
                            if (node2Coord->retrieve(nearPlacesMatch.local.id, c)) {
                                WriteableString globalName, localName;
                                nodeNames->retrieve(nearPlacesMatch.global.id, globalName);
                                nodeNames->retrieve(nearPlacesMatch.local.id, localName);
                                results.push_back(Result(c, nearPlacesMatch.quality * .75, std::string("Local/global places: ") + std::to_string(nearPlacesMatch.global.id) + " (" + globalName.c_str() + ") > " + std::to_string(nearPlacesMatch.local.id) + " (" + localName.c_str() + ")"));
                            }
                        }
                    }

                    std::vector<struct TokenProcessor::UniqueMatch> uniqueMatches = tokenProcessor.evaluateUniqueMatches(word_combinations);

                    for (const struct TokenProcessor::UniqueMatch &uniqueMatch : uniqueMatches) {
                        Coord c;
                        if (node2Coord->retrieve(uniqueMatch.element.id, c)) {
                            results.push_back(Result(c, uniqueMatch.quality * .8, std::string("Unique name: ") + uniqueMatch.name));
                        }
                    }

                    if (!places.empty()) {
                        /// No good result found, but some places have been recognized in the process.
                        /// Pick one of the larger places as result.
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
                    timer.elapsed(&cputime, &walltime);

                    dprintf(slaveSocket, "HTTP/1.1 200 OK\n");
                    if (!results.empty()) {
                        /// Sort results by quality (highest first)
                        std::sort(results.begin(), results.end(), [](Result & a, Result & b) {
                            return a.quality > b.quality;
                        });
                        /// Remove results close to even better results
                        for (auto outer = results.begin(); outer != results.end();) {
                            bool removedOuter = false;
                            const Result &outerR = *outer;
                            for (auto inner = results.begin(); !removedOuter && inner != outer && inner != results.end(); ++inner) {
                                const Result &innerR = *inner;
                                const auto d = outerR.coord.distanceLatLon(innerR.coord);
                                if (d < 1000) {
                                    /// Less than 1km away? Remove this result;
                                    outer = results.erase(outer);
                                    removedOuter = true;
                                }
                            }
                            if (!removedOuter)
                                ++outer;
                        }

                        dprintf(slaveSocket, "Cache-Control: private, max-age=0, no-cache, no-store\n");
                        dprintf(slaveSocket, "Content-Type: text/html; charset=utf-8\n\n");
                        dprintf(slaveSocket, "<html><head><title>Results</title></head>\n<body>\n");
                        dprintf(slaveSocket, "<h1>Results</h1><p>For the following input, results were located:</p>\n");
                        dprintf(slaveSocket, "<p><tt>%s</tt></p>\n", text);
                        dprintf(slaveSocket, "<p><a href=\".\">New search</a></p>\n");

                        dprintf(slaveSocket, "<h2>Found Locations</h2>\n");
                        dprintf(slaveSocket, "<p>Number of results: %lu</p>\n", results.size());
                        dprintf(slaveSocket, "<table>\n<thead><tr><th>Coordinates</th><th>Link to OpenStreetMap</th><th>Hint on Result</th></thead>\n<tbody>\n");
                        for (const Result &result : results) {
                            const double lon = Coord::toLongitude(result.coord.x);
                            const double lat = Coord::toLatitude(result.coord.y);
                            const std::vector<int> m = sweden->insideSCBarea(result.coord);
                            const int scbarea = m.empty() ? 0 : m.front();
                            dprintf(slaveSocket, "<tr><td>lat= %.4lf<br/>lon= %.4lf<br/>near %s, %s</td>", lat, lon, Sweden::nameOfSCBarea(scbarea).c_str(), Sweden::nameOfSCBarea(scbarea / 100).c_str());
                            static const int zoom = 16;
                            dprintf(slaveSocket, "<td><a href=\"http://www.openstreetmap.org/?mlat=%.5lf&mlon=%.5lf#map=%d/%.5lf/%.5lf\" target=\"_blank\">", lat, lon, zoom, lat, lon);
                            const int tileX = long2tilex(lon, zoom), tileY = lat2tiley(lat, zoom);
                            dprintf(slaveSocket, "<img src=\"http://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><br/>", zoom, tileX - 1, tileY - 1, zoom, tileX, tileY - 1, zoom, tileX + 1, tileY - 1);
                            dprintf(slaveSocket, "<img src=\"http://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><br/>", zoom, tileX - 1, tileY, zoom, tileX, tileY, zoom, tileX + 1, tileY);
                            dprintf(slaveSocket, "<img src=\"http://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" />", zoom, tileX - 1, tileY + 1, zoom, tileX, tileY + 1, zoom, tileX + 1, tileY + 1);
                            dprintf(slaveSocket, "</a></td><td>%s</td></tr>\n", result.origin.c_str());
                        }
                        dprintf(slaveSocket, "</tbody></table>\n");

                        dprintf(slaveSocket, "<h2>Consumed Time</h2>\n");
                        dprintf(slaveSocket, "<p>CPU Time: %.1f&thinsp;ms<br/>", cputime / 1000.0);
                        dprintf(slaveSocket, "Wall Time: %.1f&thinsp;ms</p>\n", walltime / 1000.0);
                        dprintf(slaveSocket, "</body></html>\n\n\n");
                    } else {
                        dprintf(slaveSocket, "Content-Type: text/html; charset=utf-8\n\n");
                        dprintf(slaveSocket, "<html><head><title>Results</title></head>\n<body>\n");
                        dprintf(slaveSocket, "<h1>Results</h1><p>Sorry, no results could be found for the following input:</p>\n");
                        dprintf(slaveSocket, "<p><tt>%s</tt></p>\n", text);
                        dprintf(slaveSocket, "<p><a href=\".\">New search</a></p>\n");
                        dprintf(slaveSocket, "<h2>Consumed Time</h2>\n");
                        dprintf(slaveSocket, "<p>CPU Time: %.1f&thinsp;ms<br/>", cputime / 1000.0);
                        dprintf(slaveSocket, "Wall Time: %.1f&thinsp;ms</p>\n", walltime / 1000.0);
                        dprintf(slaveSocket, "</body></html>\n\n\n");
                    }
                } else
                    dprintf(slaveSocket, "HTTP/1.1 400 Bad Request\n\n");

                close(slaveSocket);
                exit(0);
            }
            default: {
                /// Parent process
                close(slaveSocket);
            }
            }
        }
    }
}
