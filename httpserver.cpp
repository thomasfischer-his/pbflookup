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
#include "helper.h"

static const size_t maxBufferSize = 16384;
static const size_t maxStringLen = 1024;

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
///  https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames#C.2FC.2B.2B
int long2tilex(double lon, int z)
{
    return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z)));
}

int lat2tiley(double lat, int z)
{
    return (int)(floor((1.0 - log(tan(lat * M_PI / 180.0) + 1.0 / cos(lat * M_PI / 180.0)) / M_PI) / 2.0 * pow(2.0, z)));
}

class HTTPServer::Private {
private:
    HTTPServer *p;

public:
    Timer timerServer, timerSearch;

    Private(HTTPServer *parent)
        : p(parent) {
        // TODO
    }

    void deliverFile(int fd, const char *filename) {
        /// Check for valid filenames
        bool valid_filename = filename[0] == '/'; ///< filename must start with a slash
        const size_t len = strlen(filename);
        static const char acceptable_chars[] = "0123456789-_./aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ";
        static char needle[] = {'\0', '\0'};
        for (size_t i = 0; valid_filename && i < len; ++i) {
            if (filename[i] == '.' && i > 0 && filename[i - 1] == '.')
                valid_filename = false; ///< someone tries to escape confinement
            else if ((filename[i] & 128) > 0)
                valid_filename = false; ///< UTF-8 or any other 8-bit filenames not accepted
            else {
                needle[0] = filename[i];
                if (strstr(acceptable_chars, needle) == NULL)
                    valid_filename = false; ///< not an acceptable character
            }
        }

        if (!valid_filename) {
            Error::warn("Got invalid filename: '%s'", filename);
            writeHTTPError(fd, 403);
            return;
        }

        char localfilename[maxStringLen];
        snprintf(localfilename, maxStringLen - 1, "public/%s", filename);

        FILE *localfile = valid_filename ? fopen(localfilename, "r") : NULL;
        if (localfile != NULL) {
            dprintf(fd, "HTTP/1.1 200 OK\n");
            const size_t lflen = strlen(localfilename);
            if (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'c' && localfilename[lflen - 2] == 's' && localfilename[lflen - 1] == 's')
                dprintf(fd, "Content-Type: text/css; charset=utf-8\n");
            else if (lflen > 6 && localfilename[lflen - 5] == '.' && localfilename[lflen - 4] == 'h' && localfilename[lflen - 3] == 't' && localfilename[lflen - 2] == 'm' && localfilename[lflen - 1] == 'l')
                dprintf(fd, "Content-Type: text/html; charset=utf-8\n");
            else if (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 't' && localfilename[lflen - 2] == 'x' && localfilename[lflen - 1] == 't')
                dprintf(fd, "Content-Type: text/plain; charset=utf-8\n");
            else
                dprintf(fd, "Content-Type: application/octet-stream\n");
            dprintf(fd, "Cache-Control: public\n");
            dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");

            /// Important: file size is limited to maxBufferSize
            static char buffer[maxBufferSize];
            size_t remaining = maxBufferSize - 1;
            char *cur = buffer;
            size_t len = fread(cur, remaining, 1, localfile);
            while (len > 0) {
                remaining -= len;
                cur += len;
                len = fread(cur, remaining, 1, localfile);
            }
            dprintf(fd, "\n%s\n", buffer);

            fclose(localfile);
        } else {
            dprintf(fd, "HTTP/1.1 404 Not Found\n");
            dprintf(fd, "Content-Type: text/html; charset=utf-8\n");
            dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");
            dprintf(fd, "<html><head><link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" /><title>PBFLookup: 404 &ndash; Not Found</title></head><body><h1>404 &ndash; Not Found</h1><p>Could not serve your request for this file:</p><pre>%s</pre></body></html>\n\n\n", filename);
        }
    }

    void printTimer(int fd, Timer *timerServer, Timer *timerSearch) {
        int64_t cputime, walltime;
        if (timerServer != NULL) {
            dprintf(fd, "<h2>Consumed Time</h2>\n");
            if (timerSearch != NULL) {
                dprintf(fd, "<h3>Search</h3>\n");
                timerSearch->elapsed(&cputime, &walltime);
                dprintf(fd, "<p>CPU Time: %.1f&thinsp;ms<br/>", cputime / 1000.0);
                dprintf(fd, "Wall Time: %.1f&thinsp;ms</p>\n", walltime / 1000.0);
            }
            dprintf(fd, "<h3>HTTP Server</h3>\n");
            timerServer->elapsed(&cputime, &walltime);
            dprintf(fd, "<p>Wall Time: %.1f&thinsp;ms</p>\n", walltime / 1000.0);
        }
    }

    void writeFormHTML(int fd) {
        dprintf(fd, "HTTP/1.1 200 OK\n");
        dprintf(fd, "Content-Type: text/html; charset=utf-8\n");
        dprintf(fd, "Cache-Control: public\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");
        dprintf(fd, "<html><head><link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" />\n");
        dprintf(fd, "<title>PBFLookup: Search for Locations described in Swedish Text</title>\n");
        dprintf(fd, "<script type=\"text/javascript\">\nfunction testsetChanged(combo) {\n  document.getElementById('textarea').value=combo.value;\n}\n</script>\n");
        dprintf(fd, "</head>\n");
        dprintf(fd, "<body>\n");
        dprintf(fd, "<h1>Search for Locations described in Swedish Text</h1>\n");
        dprintf(fd, "<form enctype=\"text/plain\" accept-charset=\"utf-8\" action=\".\" method=\"post\">\n");
        if (!testsets.empty()) {
            dprintf(fd, "<p>Either select a pre-configured text from this list of %lu examples:\n<select onchange=\"testsetChanged(this)\" id=\"testsets\">\n", testsets.size());
            dprintf(fd, "<option selected=\"selected\" disabled=\"disabled\" hidden=\"hidden\" value=\"\"></option>");
            for (const struct testset &t : testsets)
                dprintf(fd, "<option value=\"%s\">%s</option>", t.text.c_str(), t.name.c_str());
            dprintf(fd, "</select> or &hellip;</p>\n");
        }
        dprintf(fd, "<p>Enter a Swedish text to localize:<br/><textarea name=\"text\" id=\"textarea\" cols=\"60\" rows=\"8\" placeholder=\"Write your Swedish text here\"></textarea></p>\n");
        dprintf(fd, "<p><input type=\"submit\" value=\"Find location for text\"></p>\n");
        dprintf(fd, "</form>\n");
        printTimer(fd, &timerServer, NULL);
        dprintf(fd, "</body></html>\n\n\n");
    }

    void writeResultsHTML(int fd, const char *text, const std::vector<Result> &results) {
        dprintf(fd, "HTTP/1.1 200 OK\n");
        dprintf(fd, "Content-Type: text/html; charset=utf-8\n");
        dprintf(fd, "Cache-Control: private, max-age=0, no-cache, no-store\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");

        if (!results.empty()) {
            dprintf(fd, "<html><head><title>PBFLookup: %lu Results</title>\n", results.size());
            dprintf(fd, "<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" />\n</head><body>\n");
            dprintf(fd, "<h1>Results</h1><p>For the following input, <strong>%lu results</strong> were located:</p>\n", results.size());
            dprintf(fd, "<p><tt>%s</tt></p>\n", text);
            dprintf(fd, "<p><a href=\".\">New search</a></p>\n");

            dprintf(fd, "<h2>Found Locations</h2>\n");
            dprintf(fd, "<p>Number of results: %lu</p>\n", results.size());
            dprintf(fd, "<table id=\"results\">\n<thead><tr><th>Coordinates</th><th>Link to OpenStreetMap</th><th>Hint on Result</th></thead>\n<tbody>\n");
            static const size_t maxCountResults = 20;
            size_t resultCounter = maxCountResults;
            for (const Result &result : results) {
                if (--resultCounter <= 0) break; ///< Limit number of results

                const double lon = Coord::toLongitude(result.coord.x);
                const double lat = Coord::toLatitude(result.coord.y);
                const std::vector<int> m = sweden->insideSCBarea(result.coord);
                const int scbarea = m.empty() ? 0 : m.front();
                dprintf(fd, "<tr><td>lat= %.4lf<br/>lon= %.4lf<br/>near %s, %s</td>", lat, lon, Sweden::nameOfSCBarea(scbarea).c_str(), Sweden::nameOfSCBarea(scbarea / 100).c_str());
                static const int zoom = 15;
                dprintf(fd, "<td><a href=\"https://www.openstreetmap.org/?mlat=%.5lf&amp;mlon=%.5lf#map=%d/%.5lf/%.5lf\" target=\"_blank\">", lat, lon, zoom, lat, lon);
                const int tileX = long2tilex(lon, zoom), tileY = lat2tiley(lat, zoom);
                dprintf(fd, "<img src=\"https://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"https://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"https://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><br/>", zoom, tileX - 1, tileY - 1, zoom, tileX, tileY - 1, zoom, tileX + 1, tileY - 1);
                dprintf(fd, "<img src=\"https://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"https://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"https://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><br/>", zoom, tileX - 1, tileY, zoom, tileX, tileY, zoom, tileX + 1, tileY);
                dprintf(fd, "<img src=\"https://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"https://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"https://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" />", zoom, tileX - 1, tileY + 1, zoom, tileX, tileY + 1, zoom, tileX + 1, tileY + 1);
                std::string hintText = result.origin;
                if (!result.elements.empty()) {
                    hintText += "\n<small><ul>\n";
                    for (const OSMElement &e : result.elements) {
                        hintText += "\n<li><a target=\"_top\" href=\"";
                        const std::string eid = std::to_string(e.id);
                        switch (e.type) {
                        case OSMElement::Node: hintText += "https://www.openstreetmap.org/node/" + eid + "\">" + e.operator std::string(); break;
                        case OSMElement::Way: hintText += "https://www.openstreetmap.org/way/" + eid + "\">" + e.operator std::string(); break;
                        case OSMElement::Relation: hintText += "https://www.openstreetmap.org/relation/" + eid + "\">" + e.operator std::string(); break;
                        case OSMElement::UnknownElementType: hintText += "https://www.openstreetmap.org/\">Unknown element type with id " + eid; break;
                        }
                        const std::string name = e.name();
                        if (!name.empty())
                            hintText += " (" + e.name() + ")";
                        hintText += "</a></li>\n";
                    }
                    hintText += "</ul></small>";
                }
                dprintf(fd, "</a></td><td>%s</td></tr>\n", hintText.c_str());
            }
            dprintf(fd, "</tbody></table>\n");
        } else {
            dprintf(fd, "<html><head><title>PBFLookup: No Results</title>\n");
            dprintf(fd, "<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" /></head>\n<body>\n");
            dprintf(fd, "<h1>Results</h1><p>Sorry, <strong>no results</strong> could be found for the following input:</p>\n");
            dprintf(fd, "<p><tt>%s</tt></p>\n", text);
            dprintf(fd, "<p><a href=\".\">New search</a></p>\n");
        }
        printTimer(fd, &timerServer, &timerSearch);
        dprintf(fd, "</body></html>\n\n\n");
    }

    void writeResultsJSON(int fd, const char *text, const std::vector<Result> &results) {
        int64_t cputime, walltime;
        dprintf(fd, "HTTP/1.1 200 OK\n");
        dprintf(fd, "Content-Type: application/json; charset=utf-8\n");
        dprintf(fd, "Cache-Control: private, max-age=0, no-cache, no-store\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");

        dprintf(fd, "{\n");
        timerSearch.elapsed(&cputime, &walltime);
        dprintf(fd, "  \"cputime[ms]\": %.3f,\n", cputime / 1000.0);

        static const size_t maxCountResults = results.size() > 20 ? 20 : results.size();
        size_t resultCounter = maxCountResults;
        dprintf(fd, "  \"results\": [\n");
        for (const Result &result : results) {
            if (--resultCounter <= 0) break; ///< Limit number of results
            dprintf(fd, "    {\n");

            const double lon = Coord::toLongitude(result.coord.x);
            const double lat = Coord::toLatitude(result.coord.y);
            const std::vector<int> m = sweden->insideSCBarea(result.coord);
            const int scbarea = m.empty() ? 0 : m.front();

            dprintf(fd, "      \"latitude\": %.4lf,\n", lat);
            dprintf(fd, "      \"longitude\": %.4lf,\n", lon);
            dprintf(fd, "      \"scbareacode\": %d,\n", scbarea);
            dprintf(fd, "      \"municipality\": \"%s\",\n", Sweden::nameOfSCBarea(scbarea).c_str());
            dprintf(fd, "      \"county\": \"%s\",\n", Sweden::nameOfSCBarea(scbarea / 100).c_str());
            static const int zoom = 13;
            dprintf(fd, "      \"url\": \"https://www.openstreetmap.org/?mlat=%.5lf&mlon=%.5lf#map=%d/%.5lf/%.5lf\",\n", lat, lon, zoom, lat, lon);
            const int tileX = long2tilex(lon, zoom), tileY = lat2tiley(lat, zoom);
            dprintf(fd, "      \"image\": \"https://%c.tile.openstreetmap.org/%d/%d/%d.png\",\n", (unsigned char)('a' + resultCounter % 3), zoom, tileX, tileY);
            dprintf(fd, "      \"origin\": {\n");
            dprintf(fd, "        \"description\": \"%s\",\n", result.origin.c_str());
            dprintf(fd, "        \"elements\": [");
            bool first = true;
            for (const OSMElement &e : result.elements) {
                if (!first)
                    dprintf(fd, ",");
                switch (e.type) {
                case OSMElement::Node: dprintf(fd, "\n          \"node/%lu\"", e.id); break;
                case OSMElement::Way: dprintf(fd, "\n          \"way/%lu\"", e.id); break;
                case OSMElement::Relation: dprintf(fd, "\n          \"relation/%lu\"", e.id); break;
                default: {
                    /// ignore everything else
                }
                }
                first = false;
            }
            dprintf(fd, "\n        ]\n");
            dprintf(fd, "      }\n");

            if (resultCounter <= 1)
                dprintf(fd, "    }\n");
            else
                dprintf(fd, "    },\n");
        }
        dprintf(fd, "  ]\n");

        dprintf(fd, "}\n\n\n");
    }

    void writeResultsXML(int fd, const char *text, const std::vector<Result> &results) {
        int64_t cputime, walltime;
        dprintf(fd, "HTTP/1.1 200 OK\n");
        dprintf(fd, "Content-Type: text/xml; charset=utf-8\n");
        dprintf(fd, "Cache-Control: private, max-age=0, no-cache, no-store\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");

        dprintf(fd, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>\n");

        dprintf(fd, "<pbflookup>\n");
        timerSearch.elapsed(&cputime, &walltime);
        dprintf(fd, "  <cputime unit=\"ms\">%.3f</cputime>\n", cputime / 1000.0);

        static const size_t maxCountResults = results.size() > 20 ? 20 : results.size();
        size_t resultCounter = maxCountResults;
        dprintf(fd, "  <results>\n");
        for (const Result &result : results) {
            if (--resultCounter <= 0) break; ///< Limit number of results
            dprintf(fd, "    <result>\n");

            const double lon = Coord::toLongitude(result.coord.x);
            const double lat = Coord::toLatitude(result.coord.y);
            const std::vector<int> m = sweden->insideSCBarea(result.coord);
            const int scbarea = m.empty() ? 0 : m.front();

            dprintf(fd, "      <latitude format=\"decimal\">%.4lf</latitude>\n", lat);
            dprintf(fd, "      <longitude format=\"decimal\">%.4lf</longitude>\n", lon);
            dprintf(fd, "      <scbareacode>%d</scbareacode>\n", scbarea);
            dprintf(fd, "      <municipality>%s</municipality>\n", Sweden::nameOfSCBarea(scbarea).c_str());
            dprintf(fd, "      <county>%s</county>\n", Sweden::nameOfSCBarea(scbarea / 100).c_str());
            static const int zoom = 13;
            dprintf(fd, "      <url rel=\"openstreetmap\">https://www.openstreetmap.org/?mlat=%.5lf&amp;mlon=%.5lf#map=%d/%.5lf/%.5lf</url>\n", lat, lon, zoom, lat, lon);
            const int tileX = long2tilex(lon, zoom), tileY = lat2tiley(lat, zoom);
            dprintf(fd, "      <image rel=\"tile\">https://%c.tile.openstreetmap.org/%d/%d/%d.png</image>\n", (unsigned char)('a' + resultCounter % 3), zoom, tileX, tileY);
            dprintf(fd, "      <origin>\n");
            dprintf(fd, "        <description>%s</description>\n", result.origin.c_str());
            dprintf(fd, "        <elements>");
            for (const OSMElement &e : result.elements) {
                switch (e.type) {
                case OSMElement::Node: dprintf(fd, "\n          <node>%lu</node>", e.id); break;
                case OSMElement::Way: dprintf(fd, "\n          <way>%lu</way>", e.id); break;
                case OSMElement::Relation: dprintf(fd, "\n          <relation>%lu</relation>", e.id); break;
                default: {
                    /// ignore everything else
                }
                }
            }
            dprintf(fd, "\n        </elements>\n");
            dprintf(fd, "      </origin>\n");
            dprintf(fd, "    </result>\n");
        }
        dprintf(fd, "  </results>\n");

        dprintf(fd, "</pbflookup>\n\n\n");
    }
};

HTTPServer::HTTPServer()
    : d(new Private(this))
{
    // TODO
}

HTTPServer::~HTTPServer() {
    delete d;
}

HTTPServer::ProcessIdentity HTTPServer::run() {
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
    /// An interface name like 'LOCAL', 'local', 'LOOP', or 'loop' means INADDR_LOOPBACK
    if (((http_interface[0] | 0x20) == 'l' && (http_interface[1] | 0x20) == 'o' && (http_interface[2] | 0x20) == 'c' && (http_interface[3] | 0x20) == 'a' && (http_interface[4] | 0x20) == 'l')
            || ((http_interface[0] | 0x20) == 'l' && (http_interface[1] | 0x20) == 'o' && (http_interface[2] | 0x20) == 'o' && (http_interface[3] | 0x20) == 'p'))
        serverName.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    else
        /// An interface name like 'ANY' or 'any' means INADDR_ANY
        if ((http_interface[0] | 0x20) == 'a' && (http_interface[1] | 0x20) == 'n' && (http_interface[2] | 0x20) == 'y')
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
    const unsigned char a1 = serverName.sin_addr.s_addr == 0x0 ? 127 : serverName.sin_addr.s_addr & 255;
    const unsigned char a2 = serverName.sin_addr.s_addr == 0x0 ? 0 : (serverName.sin_addr.s_addr >> 8) & 255;
    const unsigned char a3 = serverName.sin_addr.s_addr == 0x0 ? 0 : (serverName.sin_addr.s_addr >> 16) & 255;
    const unsigned char a4 = serverName.sin_addr.s_addr == 0x0 ? 1 : (serverName.sin_addr.s_addr >> 24) & 255;
    Error::debug("Try http://%d.%d.%d.%d:%d/ to reach it", a1, a2, a3, a4, htons(serverName.sin_port));

    ProcessIdentity result = ForkParent;
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

    ResultGenerator resultGenerator;

    Error::info("Press Ctrl+C or send SIGTERM or SIGINT to pid %d", getpid());
    while (!doexitserver) {
        d->timerServer.start();

        /// It is necessary to re-initialize the file descriptor sets
        /// in each loop iteration, as pselect(..) may modify them
        FD_ZERO(&readfds);
        FD_SET(serverSocket, &readfds); ///< Watch server socket for incoming requests

        /// It is necessary to re-initialize this struct in each loop iteration,
        /// as pselect(..) may modify it (to tell us how long it waited)
        struct timespec timeout;
        /// Wait up to 60 seconds
        timeout.tv_sec = 60;
        timeout.tv_nsec = 0;

        const int pselect_result = pselect(serverSocket + 1, &readfds, NULL /** writefds */, NULL /** errorfds */, &timeout, &oldsigset);
        if (pselect_result < 0) {
            if (errno == EINTR)
                Error::debug("pselect(..) received signal: doexitserver=%s", doexitserver ? "true" : "false");
            else
                Error::err("pselect(...)  errno=%d  select_result=%d", errno, pselect_result);
        } else if (pselect_result == 0) {
            /// Timeout in pselect(..), nothing happened
            continue;
        }

        if (doexitserver)
            break;
        else if (FD_ISSET(serverSocket, &readfds)) {
            /// Connection attempt on server
            socklen_t sockaddr_in_size = sizeof(struct sockaddr_in);
            struct sockaddr_in their_addr;
            int slaveSocket = accept(serverSocket, (struct sockaddr *) &their_addr, &sockaddr_in_size);
            if (slaveSocket == -1) {
                Error::warn("Slave socket is invalid");
                continue;
            }

            switch (fork()) {
            case -1:
            {
                Error::err("Error while forking");
                exit(1);
            }
            case 0:
            {
                /// Child process
                result = ForkChild;
                close(serverSocket);

                char readbuffer[maxBufferSize], text[maxBufferSize];
                read(slaveSocket, readbuffer, maxBufferSize);

                if (readbuffer[0] == 'G' && readbuffer[1] == 'E' && readbuffer[2] == 'T' && readbuffer[3] == ' ') {
                    char getfilename[maxStringLen];
                    strncpy(getfilename, readbuffer + 4, maxStringLen - 1);
                    for (size_t i = 0; i < maxStringLen; ++i)
                        if (getfilename[i] <= 32 || getfilename[i] >= 128) {
                            getfilename[i] = '\0';
                            break;
                        }

                    if (getfilename[0] == '/' && getfilename[1] == '\0')
                        /// Serve default search form
                        d->writeFormHTML(slaveSocket);
                    else
                        d->deliverFile(slaveSocket, getfilename);
                } else if (readbuffer[0] == 'P' && readbuffer[1] == 'O' && readbuffer[2] == 'S' && readbuffer[3] == 'T' && readbuffer[4] == ' ') {
                    enum RequestedMime {HTML, JSON, XML};
                    std::string headerbuffer(readbuffer);
                    utf8tolower(headerbuffer);
                    const RequestedMime requestedMime = headerbuffer.find("\naccept: application/json") != std::string::npos ? JSON : ((headerbuffer.find("\naccept: text/xml") != std::string::npos || headerbuffer.find("\naccept: application/xml") != std::string::npos) ? XML : HTML);

                    strncpy(text, strstr(readbuffer, "\ntext=") + 6, maxBufferSize);

                    d->timerSearch.start();
                    std::vector<Result> results = resultGenerator.findResults(text, ResultGenerator::VerbositySilent);
                    d->timerSearch.stop();

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
                                    /// Less than 1km away? Remove this result!
                                    outer = results.erase(outer);
                                    removedOuter = true;
                                }
                            }
                            if (!removedOuter)
                                ++outer;
                        }
                    }

                    switch (requestedMime) {
                    case HTML: d->writeResultsHTML(slaveSocket, text, results); break;
                    case JSON: d->writeResultsJSON(slaveSocket, text, results); break;
                    case XML: d->writeResultsXML(slaveSocket, text, results); break;
                    }
                } else {
                    dprintf(slaveSocket, "HTTP/1.1 400 Bad Request\n");
                    dprintf(slaveSocket, "Content-Type: text/html; charset=utf-8\n");
                    dprintf(slaveSocket, "Content-Transfer-Encoding: 8bit\n\n");
                    dprintf(slaveSocket, "<html><head><link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" /><title>PBFLookup: 400 &ndash; Bad Request</title></head><body><h1>400 &ndash; Bad Request</h1><p>Could not serve your request.</p></body></html>\n\n\n");
                }

                close(slaveSocket);
                break; /// leave while loop
            }
            default: {
                /// Parent process
                result = ForkParent; ///< just for redundancy
                close(slaveSocket);
            }
            }
        } else {
            Error::warn("pselect() finished, but no data to process?  errno=%d  select_result=%d", errno, pselect_result);
        }
    }

    /// Restore old signal mask
    sigprocmask(SIG_SETMASK, &oldsigset, NULL);

    return result;
}
