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
#include <signal.h>

#include "global.h"
#include "globalobjects.h"
#include "timer.h"
#include "resultgenerator.h"
#include "config.h"
#include "error.h"

#define MAX_BUFFER_LEN 16384

int serverSocket;

/// Internally used to quit server, may be set by signal handler
bool doexitserver;

void sigtermfn(int signal) {
    if (signal == SIGTERM) {
        Error::debug("Got SIGTERM");
        doexitserver = true;
    } else  if (signal == SIGINT) {
        Error::debug("Got SIGINT");
        doexitserver = true;
    } else
        Error::warn("Handling unknown signal %d", signal);
}

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

    fd_set readfds;
    Error::info("HTTP Server awaits connection attempts on port %d", http_port);
    /// Extract four bytes for IPv4 address; for ANY use '127.0.0.1'
    const unsigned char a = serverName.sin_addr.s_addr == 0x0 ? 127 : serverName.sin_addr.s_addr & 255;
    const unsigned char b = serverName.sin_addr.s_addr == 0x0 ? 0 : (serverName.sin_addr.s_addr >> 8) & 255;
    const unsigned char c = serverName.sin_addr.s_addr == 0x0 ? 0 : (serverName.sin_addr.s_addr >> 16) & 255;
    const unsigned char d = serverName.sin_addr.s_addr == 0x0 ? 1 : (serverName.sin_addr.s_addr >> 24) & 255;
    Error::debug("Try http://%d.%d.%d.%d:%d/ to reach it", a, b, c, d, htons(serverName.sin_port));


    doexitserver = false;
    /// Install the signal handler for SIGTERM and SIGINT
    struct sigaction s;
    s.sa_handler = sigtermfn;
    sigemptyset(&s.sa_mask);
    s.sa_flags = 0;
    sigaction(SIGTERM, &s, NULL);
    sigaction(SIGINT, &s, NULL);

    /// Block SIGTERM and SIGINT
    sigset_t sigset, oldsigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGINT);
    sigprocmask(SIG_BLOCK, &sigset, &oldsigset);

    Error::info("Press Ctrl+C or send SIGTERM or SIGINT to pid %d", getpid());
    while (!doexitserver) {
        Timer timerServer;
        int64_t cputimeServer, walltimeServer;
        timerServer.start();

        FD_ZERO(&readfds);
        FD_SET(serverSocket, &readfds);
        struct timespec timeout;
        timeout.tv_sec = 5;
        timeout.tv_nsec = 0;

        const int pselect_result = pselect(serverSocket + 1, &readfds, NULL /** writefds */, NULL /** errorfds */, &timeout, &oldsigset);
        if (pselect_result < 0) {
            if (errno == EINTR) {
                Error::debug("Got interrupted");
                doexitserver = true;
                break;
            } else
                Error::err("select(...)  errno=%d  select_result=%d", errno, pselect_result);
        } else if (pselect_result == 0) {
            /// Timeout in select(..), nothing happened
            continue;
        }

        if (FD_ISSET(serverSocket, &readfds)) {
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
                    if (cssfilename[0] != '\0' && strncmp(readbuffer + 4, "/default.css", 12) == 0) {
                        Error::info("CSS file requested");
                        dprintf(slaveSocket, "Content-Type: text/css; charset=utf-8\n");
                        FILE *f = fopen(cssfilename, "r");
                        if (f != NULL) {
                            static char cssbuffer[MAX_BUFFER_LEN];
                            size_t remaining = MAX_BUFFER_LEN - 1;
                            char *cur = cssbuffer;
                            size_t len = fread(cur, remaining, 1, f);
                            while (len > 0) {
                                remaining -= len;
                                cur += len;
                                len = fread(cur, remaining, 1, f);
                            }
                            fclose(f);
                            dprintf(slaveSocket, "\n%s\n", cssbuffer);
                        } else
                            Error::warn("Cannot open CSS file '%s'", cssfilename);
                        dprintf(slaveSocket, "\n");
                    } else {
                        dprintf(slaveSocket, "Content-Type: text/html; charset=utf-8\n");
                        dprintf(slaveSocket, "\n<html><head><link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" />\n");
                        dprintf(slaveSocket, "<title>PBFLookup: Search for Locations described in Swedish Text</title>\n");
                        dprintf(slaveSocket, "<script type=\"text/javascript\">\nfunction testsetChanged(combo) {\n  document.getElementById('textarea').value=combo.value;\n}\n</script>\n");
                        dprintf(slaveSocket, "</head>\n");
                        dprintf(slaveSocket, "<body>\n");
                        dprintf(slaveSocket, "<h1>Search for Locations described in Swedish Text</h1>\n");
                        dprintf(slaveSocket, "<form enctype=\"text/plain\" accept-charset=\"utf-8\" action=\".\" method=\"post\">\n");
                        if (!testsets.empty()) {
                            dprintf(slaveSocket, "<p>Either select a pre-configured text from this list of %lu examples:\n<select onchange=\"testsetChanged(this)\" id=\"testsets\">\n", testsets.size());
                            dprintf(slaveSocket, "<option selected=\"selected\" disabled=\"disabled\" hidden=\"hidden\" value=\"\"></option>");
                            for (const struct testset &t : testsets)
                                dprintf(slaveSocket, "<option value=\"%s\">%s</option>", t.text.c_str(), t.name.c_str());
                            dprintf(slaveSocket, "</select> or &hellip;</p>\n");
                        }
                        dprintf(slaveSocket, "<p>Enter a Swedish text to localize:<br/><textarea name=\"text\" id=\"textarea\" cols=\"60\" rows=\"8\" placeholder=\"Write your Swedish text here\"></textarea></p>\n");
                        dprintf(slaveSocket, "<p><input type=\"submit\" value=\"Find location for text\"></p>\n");
                        dprintf(slaveSocket, "</form>\n");
                        dprintf(slaveSocket, "<h2>Consumed Time</h2>\n");
                        dprintf(slaveSocket, "<h3>HTTP Server</h3>\n");
                        timerServer.elapsed(&cputimeServer, &walltimeServer);
                        dprintf(slaveSocket, "<p>Wall Time: %.1f&thinsp;ms</p>\n", walltimeServer / 1000.0);
                        dprintf(slaveSocket, "</body></html>\n\n\n");
                    }
                } else if (readbuffer[0] == 'P' && readbuffer[1] == 'O' && readbuffer[2] == 'S' && readbuffer[3] == 'T' && readbuffer[4] == ' ') {
                    strncpy(text, strstr(readbuffer, "\ntext=") + 6, readbuffer_size);

                    Timer timerSearch;
                    int64_t cputimeSearch, walltimeSearch;

                    timerSearch.start();
                    std::vector<Result> results = ResultGenerator::findResults(text, ResultGenerator::VerbositySilent);
                    timerSearch.elapsed(&cputimeSearch, &walltimeSearch);

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
                        dprintf(slaveSocket, "<html><head><title>PBFLookup: Results</title>\n");
                        dprintf(slaveSocket, "<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" />\n</head><body>\n");
                        dprintf(slaveSocket, "<h1>Results</h1><p>For the following input, results were located:</p>\n");
                        dprintf(slaveSocket, "<p><tt>%s</tt></p>\n", text);
                        dprintf(slaveSocket, "<p><a href=\".\">New search</a></p>\n");

                        dprintf(slaveSocket, "<h2>Found Locations</h2>\n");
                        dprintf(slaveSocket, "<p>Number of results: %lu</p>\n", results.size());
                        dprintf(slaveSocket, "<table id=\"results\">\n<thead><tr><th>Coordinates</th><th>Link to OpenStreetMap</th><th>Hint on Result</th></thead>\n<tbody>\n");
                        static const size_t maxCountResults = 20;
                        size_t resultCounter = maxCountResults;
                        for (const Result &result : results) {
                            if (--resultCounter <= 0) break; ///< Limit number of results

                            const double lon = Coord::toLongitude(result.coord.x);
                            const double lat = Coord::toLatitude(result.coord.y);
                            const std::vector<int> m = sweden->insideSCBarea(result.coord);
                            const int scbarea = m.empty() ? 0 : m.front();
                            dprintf(slaveSocket, "<tr><td>lat= %.4lf<br/>lon= %.4lf<br/>near %s, %s</td>", lat, lon, Sweden::nameOfSCBarea(scbarea).c_str(), Sweden::nameOfSCBarea(scbarea / 100).c_str());
                            static const int zoom = 15;
                            dprintf(slaveSocket, "<td><a href=\"http://www.openstreetmap.org/?mlat=%.5lf&mlon=%.5lf#map=%d/%.5lf/%.5lf\" target=\"_blank\">", lat, lon, zoom, lat, lon);
                            const int tileX = long2tilex(lon, zoom), tileY = lat2tiley(lat, zoom);
                            dprintf(slaveSocket, "<img src=\"http://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><br/>", zoom, tileX - 1, tileY - 1, zoom, tileX, tileY - 1, zoom, tileX + 1, tileY - 1);
                            dprintf(slaveSocket, "<img src=\"http://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><br/>", zoom, tileX - 1, tileY, zoom, tileX, tileY, zoom, tileX + 1, tileY);
                            dprintf(slaveSocket, "<img src=\"http://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"http://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" />", zoom, tileX - 1, tileY + 1, zoom, tileX, tileY + 1, zoom, tileX + 1, tileY + 1);
                            std::string hintText = result.origin;
                            if (!result.elements.empty()) {
                                hintText += "\n<small><ul>\n";
                                for (const OSMElement &e : result.elements) {
                                    hintText += "\n<li><a target=\"_top\" href=\"";
                                    const std::string eid = std::to_string(e.id);
                                    switch (e.type) {
                                    case OSMElement::Node: hintText += "https://www.openstreetmap.org/node/" + eid + "\">Node " + eid; break;
                                    case OSMElement::Way: hintText += "https://www.openstreetmap.org/way/" + eid + "\">Way " + eid; break;
                                    case OSMElement::Relation: hintText += "https://www.openstreetmap.org/way/" + eid + "\">Way " + eid; break;
                                    case OSMElement::UnknownElementType: hintText += "https://www.openstreetmap.org/\">Unknown element type with id " + eid; break;
                                    }
                                    hintText += "</li>\n";
                                }
                                hintText += "</ul></small>";
                            }
                            dprintf(slaveSocket, "</a></td><td>%s</td></tr>\n", hintText.c_str());
                        }
                        dprintf(slaveSocket, "</tbody></table>\n");
                    } else {
                        dprintf(slaveSocket, "Content-Type: text/html; charset=utf-8\n\n");
                        dprintf(slaveSocket, "<html><head><title>PBFLookup: Results</title>\n");
                        dprintf(slaveSocket, "<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" /></head>\n<body>\n");
                        dprintf(slaveSocket, "<h1>Results</h1><p>Sorry, no results could be found for the following input:</p>\n");
                        dprintf(slaveSocket, "<p><tt>%s</tt></p>\n", text);
                        dprintf(slaveSocket, "<p><a href=\".\">New search</a></p>\n");
                    }
                    dprintf(slaveSocket, "<h2>Consumed Time</h2>\n");
                    dprintf(slaveSocket, "<h3>Search</h3>\n");
                    dprintf(slaveSocket, "<p>CPU Time: %.1f&thinsp;ms<br/>", cputimeSearch / 1000.0);
                    dprintf(slaveSocket, "Wall Time: %.1f&thinsp;ms</p>\n", cputimeSearch / 1000.0);
                    dprintf(slaveSocket, "<h3>HTTP Server</h3>\n");
                    timerServer.elapsed(&cputimeServer, &walltimeServer);
                    dprintf(slaveSocket, "<p>Wall Time: %.1f&thinsp;ms</p>\n", walltimeServer / 1000.0);
                    dprintf(slaveSocket, "</body></html>\n\n\n");
                } else {
                    dprintf(slaveSocket, "HTTP/1.1 400 Bad Request\n");
                    dprintf(slaveSocket, "Content-Type: text/html; charset=utf-8\n\n");
                    dprintf(slaveSocket, "<html><head><title>PBFLookup: Bad Request</title>\n");
                    dprintf(slaveSocket, "<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" /></head>\n<body>\n");
                    dprintf(slaveSocket, "<h1>Bad Request</h1>\n");
                    dprintf(slaveSocket, "<h2>Consumed Time</h2>\n");
                    dprintf(slaveSocket, "<h3>HTTP Server</h3>\n");
                    timerServer.elapsed(&cputimeServer, &walltimeServer);
                    dprintf(slaveSocket, "<p>Wall Time: %.1f&thinsp;ms</p>\n", walltimeServer / 1000.0);
                    dprintf(slaveSocket, "</body></html>\n\n\n");
                }

                close(slaveSocket);
                exit(0);
            }
            default: {
                /// Parent process
                close(slaveSocket);
            }
            }
        } else {
            Error::warn("select() finished, but no data to process?  errno=%d  select_result=%d", errno, pselect_result);
        }
    }

    /// Restore old signal mask
    sigprocmask(SIG_SETMASK, &oldsigset, NULL);
}
