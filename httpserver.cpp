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

#include <tuple>
#include <fstream>
#include <cstring>
#include <cstdlib>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
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

    struct SlaveConnection {
        SlaveConnection()
            : socket(-1), pos(0) {
            data[0] = '\0';
        }

        int socket;
        char data[maxBufferSize];
        size_t pos;
    };

    Private(HTTPServer *parent)
        : p(parent) {
        // TODO
    }

    struct HTTPrequest {
        enum Method {MethodGet, MethodPost};
        Method method;
        std::string filename;
    };

    std::string XMLize(const std::string &text)const {
        std::string result = text;
        for (size_t i = 0; i < result.length(); ++i)
            if (result[i] == '<') {
                result[i] = '&';
                result.insert(i + 1, "lt;");
                i += 2;
            } else if (result[i] == '>') {
                result[i] = '&';
                result.insert(i + 1, "gt;");
                i += 2;
            } else if (result[i] == '&') {
                result.insert(i + 1, "amp;");
                i += 3;
            }

        return result;
    }

    std::tuple<bool, struct Private::HTTPrequest> extractHTTPrequest(const std::string &headertext) const {
        Private::HTTPrequest result;
        if (headertext.substr(0, 4) == "GET ")
            result.method = HTTPrequest::MethodGet;
        else if (headertext.substr(0, 5) == "POST ")
            result.method = HTTPrequest::MethodPost;
        else
            return std::make_tuple(false, HTTPrequest());

        std::size_t pos1 = std::string::npos;
        static const std::size_t max_first_separator_pos = 16;
        for (pos1 = result.method == HTTPrequest::MethodGet ? 3 : 4; pos1 < max_first_separator_pos && headertext[pos1] == ' '; ++pos1);
        if (pos1 >= max_first_separator_pos)
            return std::make_tuple(false, HTTPrequest());

        const std::size_t pos2 = headertext.find(' ', pos1 + 1);
        if (pos2 == std::string::npos)
            return std::make_tuple(false, HTTPrequest());

        result.filename = headertext.substr(pos1, pos2 - pos1);

        return std::make_tuple(true, result);
    }

    /// Requested MIME type for result:
    ///   HTML: Web page to be viewed in browser, MIME type 'text/html', default
    ///   JSON: JSON-formatted result, MIME type 'application/json'
    ///   XML:  XML-formatted result, MIME type 'text/xml'
    enum RequestedMimeType {HTML = 0, JSON = 1, XML = 2};

    /**
     * Based on URL and header values, extract requested MIME type for result.
     * @param Header of HTTP request, already normalized to lower-case
     * @return requested MIME type, 'HTML' as default if nothing else specified
     */
    RequestedMimeType extractRequestedMimeType(const std::string &headertext) const {
        /// First check URL for '?accept=XXX/YYY'
        const std::size_t accept_in_url_pos = headertext.find("?accept=");
        if (accept_in_url_pos != std::string::npos && headertext.find("application/json", accept_in_url_pos + 6) < accept_in_url_pos + 16)
            return JSON;
        if (accept_in_url_pos != std::string::npos && headertext.find("text/xml", accept_in_url_pos + 6) < accept_in_url_pos + 16)
            return XML;

        /// Second check HTTP header for 'accept: XXX/YYY'
        const std::size_t accept_in_header_pos = headertext.find("\naccept:");
        if (accept_in_header_pos != std::string::npos && headertext.find("application/json", accept_in_header_pos + 7) < accept_in_header_pos + 18)
            return JSON;
        if (accept_in_header_pos != std::string::npos && headertext.find("text/xml", accept_in_header_pos + 7) < accept_in_header_pos + 18)
            return XML;

        /// No previous case triggered, fall back on HTML
        return HTML;
    }

    std::string extractTextToLocalize(const std::string &input) const {
        const std::size_t newline_text = input.find("\ntext=");
        if (newline_text != std::string::npos)
            return input.substr(newline_text + 6);
        else
            Error::debug("input= |%s|", input.c_str());

        return std::string();
    }

    void writeHTTPError(int fd, unsigned int error_code, const std::string &msg = std::string(), const std::string &filename = std::string()) {
        std::string error_code_message("Unknown Error");
        if (error_code == 403)
            error_code_message = "Forbidden";
        else if (error_code == 404)
            error_code_message = "Not Found";
        else if (error_code >= 400 && error_code < 500)
            error_code_message = "Bad Request";
        else if (error_code >= 500 && error_code < 600)
            error_code_message = "Internal Server Error";

        const std::string internal_msg = msg.empty() ? "Could not serve your request." : msg;

        dprintf(fd, "HTTP/1.1 %d %s\n", error_code, error_code_message.c_str());
        dprintf(fd, "Content-Type: text/html; charset=utf-8\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");
        dprintf(fd, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n<title>PBFLookup: %d &ndash; %s</title>\n<link rel=\"icon\" type=\"image/x-icon\" href=\"/favicon.ico\" />\n</head>\n<body><h1><img src=\"/favicon.ico\" style=\"width:0.8em;height:0.8em;margin-right:0.5em;\" />%d &ndash; %s</h1>\n", error_code, error_code_message.c_str(), error_code, error_code_message.c_str());
        dprintf(fd, "<p>%s</p>\n", internal_msg.c_str());
        if (!filename.empty())
            dprintf(fd, "<pre>%s</pre>\n", filename.c_str());
        dprintf(fd, "</body>\n</html>\n\n");
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

        const std::string localfilename = http_public_files + filename;
        std::ifstream localfile(localfilename);
        if (localfile.good()) {
            /// Important: file size is limited to maxBufferSize
            static char buffer[maxBufferSize];
            localfile.read(buffer, maxBufferSize - 2);
            const size_t data_count = localfile.gcount();
            buffer[data_count] = '\0';

            if (data_count < 4 /** less than 4 Bytes doesn't sound right ... */) {
                Error::warn("Cannot read from file: '%s'", localfilename.c_str());
                writeHTTPError(fd, 404, "Could not serve your request for this file:", filename);
            } else {
                dprintf(fd, "HTTP/1.1 200 OK\n");
                const size_t lflen = localfilename.length();
                if (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'c' && localfilename[lflen - 2] == 's' && localfilename[lflen - 1] == 's')
                    dprintf(fd, "Content-Type: text/css; charset=utf-8\n");
                else if ((lflen > 6 && localfilename[lflen - 5] == '.' && localfilename[lflen - 4] == 'h' && localfilename[lflen - 3] == 't' && localfilename[lflen - 2] == 'm' && localfilename[lflen - 1] == 'l')
                         || (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'h' && localfilename[lflen - 2] == 't' && localfilename[lflen - 1] == 'm'))
                    dprintf(fd, "Content-Type: text/html; charset=utf-8\n");
                else if (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 't' && localfilename[lflen - 2] == 'x' && localfilename[lflen - 1] == 't')
                    dprintf(fd, "Content-Type: text/plain; charset=utf-8\n");
                else if ((lflen > 6 && localfilename[lflen - 5] == '.' && localfilename[lflen - 4] == 'j' && localfilename[lflen - 3] == 'p' && localfilename[lflen - 2] == 'e' && localfilename[lflen - 1] == 'g')
                         || (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'j' && localfilename[lflen - 2] == 'p' && localfilename[lflen - 1] == 'g'))
                    dprintf(fd, "Content-Type: image/jpeg; charset=utf-8\n");
                else if (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'p' && localfilename[lflen - 2] == 'n' && localfilename[lflen - 1] == 'g')
                    dprintf(fd, "Content-Type: image/png; charset=utf-8\n");
                else if (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'i' && localfilename[lflen - 2] == 'c' && localfilename[lflen - 1] == 'o')
                    dprintf(fd, "Content-Type: image/x-icon; charset=utf-8\n");
                else
                    dprintf(fd, "Content-Type: application/octet-stream\n");
                dprintf(fd, "Cache-Control: public\n");
                dprintf(fd, "Content-Length: %ld\n", data_count);
                dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");
                write(fd, buffer, data_count);
                dprintf(fd, "\n");
            }
        } else {
            Error::warn("Cannot open file for reading: '%s'", localfilename.c_str());
            writeHTTPError(fd, 404, "Could not serve your request for this file:", filename);
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
        dprintf(fd, "<!DOCTYPE html>\n<html>\n<head>\n<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" />\n");
        dprintf(fd, "<meta charset=\"UTF-8\">\n<title>PBFLookup: Search for Locations described in Swedish Text</title>\n");
        dprintf(fd, "<script type=\"text/javascript\">\nfunction testsetChanged(combo) {\n  document.getElementById('textarea').value=combo.value;\n}\n");
        dprintf(fd, "function resultMimetypeChanged(combo) {\n  document.getElementById('queryForm').setAttribute(\"action\",\"/?accept=\"+combo.value);\n}\n</script>\n");
        dprintf(fd, "<link rel=\"icon\" type=\"image/x-icon\" href=\"/favicon.ico\" />\n</head>\n");
        dprintf(fd, "<body>\n");
        dprintf(fd, "<h1><img src=\"/favicon.ico\" style=\"width:0.8em;height:0.8em;margin-right:0.5em;\" />Search for Locations described in Swedish Text</h1>\n");
        dprintf(fd, "<form enctype=\"text/plain\" accept-charset=\"utf-8\" action=\".\" method=\"post\" id=\"queryForm\">\n");
        if (!testsets.empty()) {
            dprintf(fd, "<p>Either select a pre-configured text from this list of %lu examples:\n<select onchange=\"testsetChanged(this)\" id=\"testsets\">\n", testsets.size());
            dprintf(fd, "<option selected=\"selected\" disabled=\"disabled\" hidden=\"hidden\" value=\"\"></option>");
            for (const struct testset &t : testsets)
                dprintf(fd, "<option value=\"%s\">%s</option>", t.text.c_str(), t.name.c_str());
            dprintf(fd, "</select> or &hellip;</p>\n");
        }
        dprintf(fd, "<p>Enter a Swedish text to localize:<br/><textarea name=\"text\" id=\"textarea\" cols=\"60\" rows=\"8\" placeholder=\"Write your Swedish text here\"></textarea></p>\n");
        dprintf(fd, "<p><input type=\"submit\" value=\"Find location for text\"> and return result as ");
        dprintf(fd, "<select onchange=\"resultMimetypeChanged(this)\" id=\"resultMimetype\">");
        dprintf(fd, "<option selected=\"selected\" value=\"text/html\">Website (HTML)</option>");
        dprintf(fd, "<option value=\"text/xml\">XML</option>");
        dprintf(fd, "<option value=\"application/json\">JSON</option>");
        dprintf(fd, "</select></p></form>\n");
        printTimer(fd, &timerServer, NULL);
        dprintf(fd, "</body>\n</html>\n\n");
    }

    void writeResultsHTML(int fd, const std::string &textToLocalize, const std::vector<Result> &results) {
        dprintf(fd, "HTTP/1.1 200 OK\n");
        dprintf(fd, "Content-Type: text/html; charset=utf-8\n");
        dprintf(fd, "Cache-Control: private, max-age=0, no-cache, no-store\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");

        if (!results.empty()) {
            dprintf(fd, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n<title>PBFLookup: %lu Results</title>\n", results.size());
            dprintf(fd, "<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" />\n<link rel=\"icon\" type=\"image/x-icon\" href=\"/favicon.ico\" />\n</head>\n<body>\n");
            dprintf(fd, "<h1><img src=\"/favicon.ico\" style=\"width:0.8em;height:0.8em;margin-right:0.5em;\" />Results</h1><p>For the following input, <strong>%lu results</strong> were located:</p>\n", results.size());
            dprintf(fd, "<p><tt>%s</tt></p>\n", XMLize(textToLocalize).c_str());
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
                static const int zoom = 15;
                dprintf(fd, "<tr><td><a href=\"https://www.openstreetmap.org/?mlat=%.5lf&amp;mlon=%.5lf#map=%d/%.5lf/%.5lf\" target=\"_blank\">lat= %.4lf<br/>lon= %.4lf</a><br/>near %s, %s</td>", lat, lon, zoom, lat, lon, lat, lon, Sweden::nameOfSCBarea(scbarea).c_str(), Sweden::nameOfSCBarea(scbarea / 100).c_str());
                dprintf(fd, "<td><a href=\"https://www.openstreetmap.org/?mlat=%.5lf&amp;mlon=%.5lf#map=%d/%.5lf/%.5lf\" target=\"_blank\">", lat, lon, zoom, lat, lon);
                const int tileX = long2tilex(lon, zoom), tileY = lat2tiley(lat, zoom);
                dprintf(fd, "<img class=\"extratile\" src=\"https://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img class=\"extratile\" src=\"https://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img class=\"extratile\" src=\"https://a.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><br/>", zoom, tileX - 1, tileY - 1, zoom, tileX, tileY - 1, zoom, tileX + 1, tileY - 1);
                dprintf(fd, "<img class=\"extratile\" src=\"https://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img src=\"https://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img class=\"extratile\" src=\"https://b.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><br/>", zoom, tileX - 1, tileY, zoom, tileX, tileY, zoom, tileX + 1, tileY);
                dprintf(fd, "<img class=\"extratile\" src=\"https://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img class=\"extratile\" src=\"https://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" /><img class=\"extratile\" src=\"https://c.tile.openstreetmap.org/%d/%d/%d.png\" width=\"256\" height=\"256\" />", zoom, tileX - 1, tileY + 1, zoom, tileX, tileY + 1, zoom, tileX + 1, tileY + 1);
                std::string hintText = XMLize(result.origin);
                if (!result.elements.empty()) {
                    hintText += "\n<small><ul>\n";
                    for (const OSMElement &e : result.elements) {
                        hintText += "<li><a target=\"_top\" href=\"";
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

            dprintf(fd, "<h2>License</h2>\n");
            dprintf(fd, "<p>Map data license: &copy; OpenStreetMap contributors, licensed under the <a href=\"http://opendatacommons.org/licenses/odbl/\" target=\"_top\">Open Data Commons Open Database License</a> (OBdL)<br/>Map tiles: OpenStreetMap, licensed under the <a href=\"http://creativecommons.org/licenses/by-sa/2.0/\" target=\"_top\">Creative Commons Attribution-ShareAlike&nbsp;2.0 License</a> (CC BY-SA 2.0)<br/>See <a target=\"_top\" href=\"www.openstreetmap.org/copyright\">www.openstreetmap.org/copyright</a> and <a target=\"_top\" href=\"http://wiki.openstreetmap.org/wiki/Legal_FAQ\">http://wiki.openstreetmap.org/wiki/Legal_FAQ</a> for details.</p>\n");

        } else {
            dprintf(fd, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n<title>PBFLookup: No Results</title>\n");
            dprintf(fd, "<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" />\n<link rel=\"icon\" type=\"image/x-icon\" href=\"/favicon.ico\" />\n</head>\n<body>\n");
            dprintf(fd, "<h1><img src=\"/favicon.ico\" style=\"width:0.8em;height:0.8em;margin-right:0.5em;\" />Results</h1><p>Sorry, <strong>no results</strong> could be found for the following input:</p>\n");
            dprintf(fd, "<p><tt>%s</tt></p>\n", XMLize(textToLocalize).c_str());
            dprintf(fd, "<p><a href=\".\">New search</a></p>\n");
        }
        printTimer(fd, &timerServer, &timerSearch);
        dprintf(fd, "</body>\n</html>\n\n");
    }

    void writeResultsJSON(int fd, const std::vector<Result> &results) {
        int64_t cputime, walltime;
        dprintf(fd, "HTTP/1.1 200 OK\n");
        dprintf(fd, "Content-Type: application/json; charset=utf-8\n");
        dprintf(fd, "Cache-Control: private, max-age=0, no-cache, no-store\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");

        dprintf(fd, "{\n");
        timerSearch.elapsed(&cputime, &walltime);
        dprintf(fd, "  \"cputime[ms]\": %.3f,\n", cputime / 1000.0);
        dprintf(fd, "  \"license\": {\n    \"map\": \"OpenStreetMap contributors, licensed under the Open Data Commons Open Database License (ODbL)\",\n    \"tiles\": \"OpenStreetMap, licensed under the Creative Commons Attribution-ShareAlike 2.0 License (CC BY-SA 2.0)\"\n  },\n");

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
            dprintf(fd, "      \"quality\": %.3lf,\n", result.quality);
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

    void writeResultsXML(int fd, const std::vector<Result> &results) {
        int64_t cputime, walltime;
        dprintf(fd, "HTTP/1.1 200 OK\n");
        dprintf(fd, "Content-Type: text/xml; charset=utf-8\n");
        dprintf(fd, "Cache-Control: private, max-age=0, no-cache, no-store\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\n\n");

        dprintf(fd, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>\n");

        dprintf(fd, "<pbflookup>\n");
        timerSearch.elapsed(&cputime, &walltime);
        dprintf(fd, "  <cputime unit=\"ms\">%.3f</cputime>\n", cputime / 1000.0);
        dprintf(fd, "  <licenses>\n    <license for=\"map\">OpenStreetMap contributors, licensed under the Open Data Commons Open Database License (ODbL)</license>\n    <license for=\"tiles\">OpenStreetMap, licensed under the Creative Commons Attribution-ShareAlike 2.0 License (CC BY-SA 2.0)</license>\n  </licenses>\n");

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
            dprintf(fd, "      <quality>%.3lf</quality>\n", result.quality);
            dprintf(fd, "      <scbareacode>%d</scbareacode>\n", scbarea);
            dprintf(fd, "      <municipality>%s</municipality>\n", Sweden::nameOfSCBarea(scbarea).c_str());
            dprintf(fd, "      <county>%s</county>\n", Sweden::nameOfSCBarea(scbarea / 100).c_str());
            static const int zoom = 13;
            dprintf(fd, "      <url rel=\"openstreetmap\">https://www.openstreetmap.org/?mlat=%.5lf&amp;mlon=%.5lf#map=%d/%.5lf/%.5lf</url>\n", lat, lon, zoom, lat, lon);
            const int tileX = long2tilex(lon, zoom), tileY = lat2tiley(lat, zoom);
            dprintf(fd, "      <image rel=\"tile\">https://%c.tile.openstreetmap.org/%d/%d/%d.png</image>\n", (unsigned char)('a' + resultCounter % 3), zoom, tileX, tileY);
            dprintf(fd, "      <origin>\n");
            dprintf(fd, "        <description>%s</description>\n", XMLize(result.origin).c_str());
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

    static bool case_insensitive_strequal(const char *a, const char *b, const size_t len) {
        for (size_t i = 0; i < len; ++i) {
            if (a[i] == 0x0 || b[i] == 0x0) return true; /// one of both strings terminate, must have been equal so far
            if ((a[i] & 128) > 0 || (b[i] & 128) > 0) return false; /// 8-bit, must be UTF-8 or alike, not supported
            if ((a[i] | 0x20) != (b[i] | 0x20)) return false; /// two characters do not match
        }
        /// No issues so far, strings must be equal
        return true;
    }

    static bool atEndOfHTTPrequest(const char *data, const size_t len) {
        enum RequestType {Unknown = 0, GET = 1, POST = 2};
        RequestType rt = Unknown;
        size_t content_length = 0;
        size_t first_blank_line_pos = 0, blank_line_counter = 0;
        for (size_t p = 1; p < len - 3; ++p) {
            if (blank_line_counter == 0) {
                if (p < len - 6 && case_insensitive_strequal(data + p, " get /", 6))
                    rt = GET;
                if (p < len - 7 && case_insensitive_strequal(data + p, " post /", 7))
                    rt = POST;
                else if (p < len - 16 && case_insensitive_strequal(data + p, "content-length: ", 16)) {
                    content_length = strtol(data + p + 16, NULL, 10);
                    if (errno != 0) content_length = 0;
                }
            }

            if (data[p] == 0x0d && data[p + 1] == 0x0a && data[p + 2] == 0x0d && data[p + 3] == 0x0a) {
                ++blank_line_counter;
                if (first_blank_line_pos == 0)
                    first_blank_line_pos = p;
            }
        }

        if (content_length > 0) {
            /// There has been a content-length field and its value equals received payload data
            return len - first_blank_line_pos - 4 == content_length;
        } else if (rt > Unknown)
            return first_blank_line_pos > 0 && content_length >= (int)rt /** numeric value of RequestType coincides with number of expected blank lines */;
        else
            return true; ///< Unknown RequestType, always ends
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
    /// An interface name like 'LOCAL', 'local', 'LOOP', or 'loop' means INADDR_LOOPBACK
    if ((http_interface.length() == 5 && (http_interface[0] | 0x20) == 'l' && (http_interface[1] | 0x20) == 'o' && (http_interface[2] | 0x20) == 'c' && (http_interface[3] | 0x20) == 'a' && (http_interface[4] | 0x20) == 'l')
            || (http_interface.length() >= 4 && (http_interface[0] | 0x20) == 'l' && (http_interface[1] | 0x20) == 'o' && (http_interface[2] | 0x20) == 'o' && (http_interface[3] | 0x20) == 'p'))
        serverName.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    else
        /// An interface name like 'ANY' or 'any' means INADDR_ANY
        if (http_interface.length() == 3 && (http_interface[0] | 0x20) == 'a' && (http_interface[1] | 0x20) == 'n' && (http_interface[2] | 0x20) == 'y')
            serverName.sin_addr.s_addr = htonl(INADDR_ANY);
        else {
            if (inet_aton(http_interface.c_str(), &serverName.sin_addr) == 0) {
                Error::warn("Provided http_interface '%s' is invalid, using local loopback instead", http_interface.c_str());
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
    static const size_t maxNumberSlaveSockets = 64;
    size_t countNumberSlaveSockets = 0;
    HTTPServer::Private::SlaveConnection slaveConnections[maxNumberSlaveSockets];

    Error::info("Press Ctrl+C or send SIGTERM or SIGINT to pid %d", getpid());
    while (!doexitserver) {
        d->timerServer.start();

        /// It is necessary to re-initialize the file descriptor sets
        /// in each loop iteration, as pselect(..) may modify them
        FD_ZERO(&readfds);
        int maxSocket = serverSocket;
        FD_SET(serverSocket, &readfds); ///< Watch server socket for incoming requests
        for (size_t i = 0; i < maxNumberSlaveSockets; ++i) {
            if (slaveConnections[i].socket < 0) continue;
            FD_SET(slaveConnections[i].socket, &readfds);
            if (slaveConnections[i].socket > maxSocket)
                maxSocket = slaveConnections[i].socket;
        }

        /// It is necessary to re-initialize this struct in each loop iteration,
        /// as pselect(..) may modify it (to tell us how long it waited)
        struct timespec timeout;
        /// Wait up to 60 seconds
        timeout.tv_sec = 60;
        timeout.tv_nsec = 0;

        const int pselect_result = pselect(maxSocket + 1, &readfds, NULL /** writefds */, NULL /** errorfds */, &timeout, &oldsigset);
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
                Error::warn("Slave socket is invalid from %d.%d.%d.%d", their_addr.sin_addr.s_addr & 255, (their_addr.sin_addr.s_addr >> 8) & 255, (their_addr.sin_addr.s_addr >> 16) & 255, (their_addr.sin_addr.s_addr >> 24) & 255);
                continue;
            } else
                Error::info("Incoming connection from %d.%d.%d.%d", their_addr.sin_addr.s_addr & 255, (their_addr.sin_addr.s_addr >> 8) & 255, (their_addr.sin_addr.s_addr >> 16) & 255, (their_addr.sin_addr.s_addr >> 24) & 255);

            if (countNumberSlaveSockets < maxNumberSlaveSockets) {
                size_t idx = INT_MAX;
                for (idx = 0; idx < maxNumberSlaveSockets; ++idx)
                    if (slaveConnections[idx].socket < 0) break;
                if (idx >= maxNumberSlaveSockets) {
                    Error::warn("Too many slave connections (%d)", maxNumberSlaveSockets);
                    d->writeHTTPError(slaveSocket, 500);
                    close(slaveSocket);
                } else {
                    slaveConnections[idx].socket = slaveSocket;
                    slaveConnections[idx].data[0] = '\0';
                    slaveConnections[idx].pos = 0;
                }
            }
        } else {
            for (size_t i = 0; i < maxNumberSlaveSockets; ++i)
                if (slaveConnections[i].socket >= 0 && FD_ISSET(slaveConnections[i].socket, &readfds)) {
                    ssize_t data_size = recv(slaveConnections[i].socket, slaveConnections[i].data + slaveConnections[i].pos, maxBufferSize - slaveConnections[i].pos - 1, MSG_DONTWAIT);
                    if (data_size == -1) {
                        /// Some error to handle ...
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            /// Try again later
                            Error::info("Got EAGAIN or EWOULDBLOCK");
                            continue;
                        } else {
                            /// Other unspecific error
                            Error::warn("Got errno=%d", errno);
                            d->writeHTTPError(slaveConnections[i].socket, 500);
                            close(slaveConnections[i].socket);
                            slaveConnections[i].socket = -1;
                            continue;
                        }
                    }

                    if (data_size > 0) {
                        /// Some data has been received, more may come
                        slaveConnections[i].pos += data_size;
                        slaveConnections[i].data[slaveConnections[i].pos] = '\0'; ///< ensure string termination
                        Error::debug("Received %lu bytes of data in total on socket %u", slaveConnections[i].pos, slaveConnections[i].socket);

                        /// If not auto-detecting end-of-request (two newlines), wait for more data
                        if (Private::atEndOfHTTPrequest(slaveConnections[i].data, slaveConnections[i].pos))
                            Error::debug("Auto-detecting end-of-request");
                        else
                            continue;
                    }

                    /// Remember number of bytes received
                    data_size = slaveConnections[i].pos;
                    /// A valid request should have some minimum number of bytes
                    if (data_size < 4 /** less than 4 Bytes doesn't sound right ... */) {
                        d->writeHTTPError(slaveConnections[i].socket, 400);
                        close(slaveConnections[i].socket);
                        slaveConnections[i].socket = -1;
                        Error::warn("Too few bytes read from slave socket: %d bytes only", data_size);
                        continue;
                    } else
                        Error::debug("Processing %d Bytes", data_size);

                    const std::string readtext(slaveConnections[i].data);
                    std::string lowercasetext = readtext;
                    utf8tolower(lowercasetext);

                    const auto request = d->extractHTTPrequest(readtext);
                    if (!std::get<0>(request)) {
                        d->writeHTTPError(slaveConnections[i].socket, 400);
                        close(slaveConnections[i].socket);
                        slaveConnections[i].socket = -1;
                        Error::warn("Too few bytes read from slave socket: %d bytes only", data_size);
                        continue;
                    }

                    if (std::get<1>(request).method == Private::HTTPrequest::MethodGet) {
                        const std::string &getfilename = std::get<1>(request).filename;
                        if (getfilename == "/")
                            /// Serve default search form
                            d->writeFormHTML(slaveConnections[i].socket);
                        else if (!http_public_files.empty())
                            d->deliverFile(slaveConnections[i].socket, getfilename.c_str());
                        else
                            d->writeHTTPError(slaveConnections[i].socket, 404, "Could not serve your request for this file:", getfilename);
                    } else if (std::get<1>(request).method == Private::HTTPrequest::MethodPost) {
                        const Private::RequestedMimeType requestedMime = d->extractRequestedMimeType(lowercasetext);
                        const std::string text = d->extractTextToLocalize(lowercasetext);

                        d->timerSearch.start();
                        const std::vector<Result> results = text.length() > 3 ? resultGenerator.findResults(text, 1000, ResultGenerator::VerbositySilent) : std::vector<Result>();
                        d->timerSearch.stop();
                        Error::debug("%d results", results.size());

                        switch (requestedMime) {
                        case Private::HTML: d->writeResultsHTML(slaveConnections[i].socket, text, results); break;
                        case Private::JSON: d->writeResultsJSON(slaveConnections[i].socket, results); break;
                        case Private::XML: d->writeResultsXML(slaveConnections[i].socket, results); break;
                        }
                    } else {
                        d->writeHTTPError(slaveConnections[i].socket, 400);
                    }

                    close(slaveConnections[i].socket);
                    slaveConnections[i].socket = -1;
                }
        }
    }

    for (size_t i = 0; i < maxNumberSlaveSockets; ++i)
        if (slaveConnections[i].socket >= 0) {
            d->writeHTTPError(slaveConnections[i].socket, 500);
            close(slaveConnections[i].socket);
            slaveConnections[i].socket = -1;
        }

    /// Restore old signal mask
    sigprocmask(SIG_SETMASK, &oldsigset, NULL);

    return;
}
