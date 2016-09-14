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
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <signal.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "global.h"
#include "globalobjects.h"
#include "timer.h"
#include "resultgenerator.h"
#include "config.h"
#include "error.h"
#include "helper.h"

static const size_t maxBufferSize = 131072;
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
    const char *start_time;

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
        const time_t curtime = time(NULL);
        struct tm *brokentime = localtime(&curtime);
        start_time = asctime(brokentime);
    }

    enum HTTPstate {NeedMoreData, Bad, Good};
    struct HTTPrequest {
        enum Method {MethodUnknown = 0, MethodGet = 1, MethodPost = 2};
        Method method;
        int content_length;
        int content_start;
        std::string filename;

        HTTPrequest()
            : method(MethodUnknown), content_length(0), content_start(-1) {
            /// nothing
        }
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

    std::tuple<HTTPstate, struct Private::HTTPrequest> extractHTTPrequest(const std::string &headertext) const {
        Private::HTTPrequest result;
        /// Determine requests method (GET or POST)
        if (headertext.substr(0, 4) == "GET ")
            result.method = HTTPrequest::MethodGet;
        else if (headertext.substr(0, 5) == "POST ")
            result.method = HTTPrequest::MethodPost;
        else
            return std::make_tuple(Bad, HTTPrequest());

        /// Determine requests filename
        /// First, find first non-whitespace character after method string
        std::size_t pos1 = std::string::npos;
        static const std::size_t max_first_separator_pos = 16;
        for (pos1 = result.method == HTTPrequest::MethodGet ? 3 : 4; pos1 < max_first_separator_pos && headertext[pos1] == ' '; ++pos1);
        if (pos1 >= max_first_separator_pos)
            return std::make_tuple(Bad, HTTPrequest());
        /// Second, find end of filename, denoted by whitespace
        const std::size_t pos2 = headertext.find(' ', pos1 + 1);
        if (pos2 == std::string::npos)
            return std::make_tuple(Bad, HTTPrequest());
        /// Finally, extract filename
        result.filename = headertext.substr(pos1, pos2 - pos1);

        unsigned int blank_line_counter = 0;
        for (std::size_t p = pos2; p < headertext.length() - 3; ++p) {
            if (blank_line_counter == 0 && p < headertext.length() - 16) {
                const std::string needle = headertext.substr(p, 16);
                if (boost::iequals(needle, "content-length: ")) {
                    result.content_length = strtol(headertext.substr(p + 16, 6).c_str(), nullptr, 10);
                    if (errno != 0) result.content_length = 0;
                }
            }

            if (headertext[p] == 0x0d && headertext[p + 1] == 0x0a && headertext[p + 2] == 0x0d && headertext[p + 3] == 0x0a) {
                ++blank_line_counter;
                if (result.content_start == -1)
                    result.content_start = p + 4;
            }
        }

        if (result.content_length > 0) {
            /// There has been a content-length field

            if (result.content_length > (int)maxBufferSize - 1 && headertext.length() >= maxBufferSize - 3)
                /// Buffer is already full, no need to receive more
                return std::make_tuple(Good, result);

            /// Do we need to receive more data?
            const int delta = (int)(headertext.length()) - result.content_start - result.content_length;
            const HTTPstate state = delta < 0 ? NeedMoreData : (delta > 0 ? Bad : Good);
            return std::make_tuple(state, result);
        } else if (result.method > HTTPrequest::MethodUnknown) {
            /// Numeric value of Method coincides with number of expected blank lines
            const HTTPstate state = blank_line_counter < (int)result.method ? NeedMoreData : Good;
            return std::make_tuple(state, result);
        } else
            return std::make_tuple(Bad, result); ///< Unknown Method, request always ends
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

    void print_socket_status(const int socket) {
        int error = 0;
        socklen_t len = sizeof(error);
        const int retval = getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &len);
        if (retval > 0)
            Error::warn("Error getting error code for socket %d: %s", socket, strerror(retval));
        if (error != 0)
            Error::warn("Socket %d: error: %s", socket, strerror(error));
    }

    std::string extractTextToLocalize(const std::string &input) const {
        const std::size_t newline_text = input.find("\ntext=");
        if (newline_text != std::string::npos)
            return input.substr(newline_text + 6);
        else
            Error::warn("Could not find 'text=' in: %s", input.c_str());

        return std::string();
    }

    void writeFinancialSupportStatement(std::ostream &html_stream) const {
        html_stream << "<hr/>" << std::endl;
        html_stream << "<h2>Supported By</h2>" << std::endl;
        html_stream << "<p>This service is financially supported by:<br/>" << std::endl;
        html_stream << "<a style=\"margin-right:1em;\" href=\"https://www.his.se/\" target=\"_top\"><img src=\"his.png\" width=\"67\" height=\"64\" alt=\"H&ouml;gskolan i Sk&ouml;vde\" /></a>" << std::endl;
        html_stream << "<a style=\"margin-right:1em;\" href=\"https://www.iis.se/\" target=\"_top\"><img src=\"iis.png\" width=\"64\" height=\"64\" alt=\"Internetstiftelsen i Sverige\" /></a>" << std::endl;
        html_stream << "</p>" << std::endl;
    }

    void writeLinkToReport(std::ostream &html_stream) const {
        static const std::string filename = "osmgeoref-final.pdf";
        static const std::string localfilename = http_public_files + "/" + filename;
        static const int access_result = access(localfilename.c_str(), R_OK);
        if (access_result == 0) {
            html_stream << "<h2>Project Report</h2>" << std::endl;
            html_stream << "<p>The project's report is available for download:<br/>" << std::endl;
            html_stream << "<a style=\"padding-left:1.5em; background-image: url('/application-pdf.png'); background-repeat: no-repeat; background-size: contain;\" href=\"" << filename << "\" target=\"_top\">" << filename << "</a> (3.2&thinsp;MB)" << std::endl;
            html_stream << "</p>" << std::endl;
        }
    }

    void writeLinkToGithub(std::ostream &html_stream) const {
        html_stream << "<h2>Source Code</h2>" << std::endl;
        html_stream << "<p>The project's source code is available at GitHub:<br/>" << std::endl;
        html_stream << "<a style=\"padding-left:1.5em; background-image: url('/git.png'); background-repeat: no-repeat; background-size: contain;\" href=\"https://github.com/thomasfischer-his/pbflookup\" target=\"_top\">https://github.com/thomasfischer-his/pbflookup</a>" << std::endl;
        html_stream << "</p>" << std::endl;
    }

    void writeContactDetails(std::ostream &html_stream) const {
        html_stream << "<hr/>" << std::endl << "<h2>Contact Details</h2>" << std::endl;
        html_stream << "<p><a href=\"https://www.his.se/fish\" target=\"_top\">Thomas Fischer</a> (<a href=\"https://www.his.se/\" target=\"_top\">H&ouml;gskolan i Sk&ouml;vde</a>)</p>" << std::endl;
    }

    void writeHTTPError(int fd, unsigned int error_code, const std::string &msg = std::string(), const std::string &filename = std::string()) {
        std::string error_code_message("Unknown Error");
        if (error_code == 100)
            error_code_message = "Continue";
        else if (error_code > 100 && error_code < 200)
            error_code_message = "Informational 1xx";
        else if (error_code == 403)
            error_code_message = "Forbidden";
        else if (error_code == 404)
            error_code_message = "Not Found";
        else if (error_code >= 400 && error_code < 500)
            error_code_message = "Bad Request";
        else if (error_code >= 500 && error_code < 600)
            error_code_message = "Internal Server Error";

        const std::string internal_msg = msg.empty() ? "Could not serve your request." : msg;
        Error::debug("Sending HTTP status %d: %s", error_code, error_code_message.c_str());

        std::ostringstream html_stream;
        html_stream << "<!DOCTYPE html>" << std::endl << "<html>" << std::endl << "<head>" << std::endl << "<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" />" << std::endl;
        html_stream << "<meta charset=\"UTF-8\">" << std::endl << "<title>PBFLookup: Error " << error_code << " &ndash; " << error_code_message << "</title>" << std::endl;
        html_stream << "<link rel=\"icon\" type=\"image/x-icon\" href=\"/favicon.ico\" />" << std::endl << "</head>" << std::endl << "<body>" << std::endl;
        html_stream << "<h1 style=\"padding-left:1.5em; background-image: url('/favicon.ico'); background-repeat: no-repeat; background-size: contain;\">Error " << error_code << " &ndash; " << error_code_message << "</h1>" << std::endl;
        html_stream << "<p>" << internal_msg << "</p>" << std::endl;
        if (!filename.empty())
            html_stream << "<pre>" << filename << "</pre>" << std::endl;
        html_stream << "<p>Server is running since: " << start_time << "</p>" << std::endl;
        writeFinancialSupportStatement(html_stream);
        writeLinkToReport(html_stream);
        writeLinkToGithub(html_stream);
        writeContactDetails(html_stream);
        html_stream << "</body>" << std::endl << "</html>" << std::endl;

        const auto html_code = html_stream.str();
        const auto html_code_size = html_code.length();

        dprintf(fd, "HTTP/1.1 %d %s\r\n", error_code, error_code_message.c_str());
        dprintf(fd, "Content-Type: text/html; charset=utf-8\r\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\r\n");
        dprintf(fd, "Content-Length: %ld\r\n", html_code_size);
        dprintf(fd, "\r\n%s\r\n\r\n", html_code.c_str());
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
                if (strstr(acceptable_chars, needle) == nullptr)
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
            static char buffer[maxBufferSize];
            localfile.seekg(0, std::ifstream::end);
            const size_t filesize = localfile.tellg();
            localfile.seekg(0, std::ifstream::beg);
            localfile.read(buffer, maxBufferSize - 2);
            const size_t data_count = localfile.gcount();
            buffer[data_count] = '\0';

            if (data_count < 4 /** less than 4 Bytes doesn't sound right ... */) {
                Error::warn("Cannot read from file: '%s'", localfilename.c_str());
                writeHTTPError(fd, 404, "Could not serve your request for this file:", filename);
            } else {
                dprintf(fd, "HTTP/1.1 200 OK\r\n");
                const size_t lflen = localfilename.length();
                if (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'c' && localfilename[lflen - 2] == 's' && localfilename[lflen - 1] == 's')
                    dprintf(fd, "Content-Type: text/css; charset=utf-8\r\n");
                else if ((lflen > 6 && localfilename[lflen - 5] == '.' && localfilename[lflen - 4] == 'h' && localfilename[lflen - 3] == 't' && localfilename[lflen - 2] == 'm' && localfilename[lflen - 1] == 'l')
                         || (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'h' && localfilename[lflen - 2] == 't' && localfilename[lflen - 1] == 'm'))
                    dprintf(fd, "Content-Type: text/html; charset=utf-8\r\n");
                else if (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 't' && localfilename[lflen - 2] == 'x' && localfilename[lflen - 1] == 't')
                    dprintf(fd, "Content-Type: text/plain; charset=utf-8\r\n");
                else if ((lflen > 6 && localfilename[lflen - 5] == '.' && localfilename[lflen - 4] == 'j' && localfilename[lflen - 3] == 'p' && localfilename[lflen - 2] == 'e' && localfilename[lflen - 1] == 'g')
                         || (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'j' && localfilename[lflen - 2] == 'p' && localfilename[lflen - 1] == 'g'))
                    dprintf(fd, "Content-Type: image/jpeg; charset=utf-8\r\n");
                else if (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'p' && localfilename[lflen - 2] == 'n' && localfilename[lflen - 1] == 'g')
                    dprintf(fd, "Content-Type: image/png; charset=utf-8\r\n");
                else if (lflen > 5 && localfilename[lflen - 4] == '.' && localfilename[lflen - 3] == 'i' && localfilename[lflen - 2] == 'c' && localfilename[lflen - 1] == 'o')
                    dprintf(fd, "Content-Type: image/x-icon; charset=utf-8\r\n");
                else
                    dprintf(fd, "Content-Type: application/octet-stream\r\n");
                dprintf(fd, "Cache-Control: public\r\n");
                dprintf(fd, "Content-Length: %ld\r\n", filesize);
                dprintf(fd, "Content-Transfer-Encoding: 8bit\r\n\r\n");
                write(fd, buffer, data_count);
                while (localfile.good()) {
                    localfile.read(buffer, maxBufferSize - 2);
                    const size_t data_count = localfile.gcount();
                    buffer[data_count] = '\0';
                    if (data_count > 0)
                        write(fd, buffer, data_count);
                    else
                        break;
                }
                dprintf(fd, "\r\n");
            }
        } else {
            Error::warn("Cannot open file for reading: '%s'", localfilename.c_str());
            writeHTTPError(fd, 404, "Could not serve your request for this file:", filename);
        }
    }

    void printTimer(std::ostringstream &html_stream, Timer *timerServer, Timer *timerSearch) {
        int64_t cputime, walltime;
        if (timerServer != nullptr) {
            html_stream << "<h2>Consumed Time</h2>" << std::endl;
            if (timerSearch != nullptr) {
                html_stream << "<h3>Search</h3>" << std::endl;
                timerSearch->elapsed(&cputime, &walltime);
                html_stream << "<p>CPU Time: " << (cputime / 1000.0) << "&thinsp;ms<br/>";
                html_stream << "Wall Time: " << (walltime / 1000.0) << "&thinsp;ms</p>" << std::endl;
            }
            html_stream << "<h3>HTTP Server</h3>" << std::endl;
            timerServer->elapsed(&cputime, &walltime);
            html_stream << "<p>Wall Time: " << (walltime / 1000.0) << "&thinsp;ms</p>" << std::endl;
        }
        html_stream << "<p>Server is running since: " << start_time << "</p>" << std::endl;
    }

    void writeFormHTML(int fd) {
        std::ostringstream html_stream;
        html_stream << "<!DOCTYPE html>" << std::endl << "<html>" << std::endl << "<head>" << std::endl << "<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" />" << std::endl;
        html_stream << "<meta charset=\"UTF-8\">" << std::endl << "<title>PBFLookup: Search for Locations described in Swedish Text</title>" << std::endl;
        html_stream << "<script type=\"text/javascript\">" << std::endl << "function testsetChanged(combo) {" << std::endl << "  document.getElementById('textarea').value=combo.value;" << std::endl << "}" << std::endl;
        html_stream << "function resultMimetypeChanged(combo) {" << std::endl << "  document.getElementById('queryForm').setAttribute(\"action\",\"/?accept=\"+combo.value);" << std::endl << "}" << std::endl << "</script>" << std::endl;
        html_stream << "<link rel=\"icon\" type=\"image/x-icon\" href=\"/favicon.ico\" />" << std::endl << "</head>" << std::endl;
        html_stream << "<body>" << std::endl;
        html_stream << "<h1 style=\"padding-left:1.5em; background-image: url('/favicon.ico'); background-repeat: no-repeat; background-size: contain;\">Search for Locations described in Swedish Text</h1>" << std::endl;
        html_stream << "<form enctype=\"text/plain\" accept-charset=\"utf-8\" action=\".\" method=\"post\" id=\"queryForm\">" << std::endl;
        if (!testsets.empty()) {
            html_stream << "<p>Either select a pre-configured text from this list of " << testsets.size() << " examples:" << std::endl << "<select onchange=\"testsetChanged(this)\" id=\"testsets\">" << std::endl;
            html_stream << "<option selected=\"selected\" disabled=\"disabled\" hidden=\"hidden\" value=\"\"></option>";
            for (const auto &t : testsets)
                html_stream << "<option value=\"" << t.text << "\">" << t.name << "</option>";
            html_stream << "</select> or &hellip;</p>" << std::endl;
        }
        html_stream << "<p>Enter a Swedish text to localize:<br/><textarea name=\"text\" id=\"textarea\" cols=\"60\" rows=\"8\" placeholder=\"Write your Swedish text here\"></textarea></p>" << std::endl;
        html_stream << "<p><input type=\"submit\" value=\"Find location for text\"> and return result as ";
        html_stream << "<select onchange=\"resultMimetypeChanged(this)\" id=\"resultMimetype\">";
        html_stream << "<option selected=\"selected\" value=\"text/html\">Website (HTML)</option>";
        html_stream << "<option value=\"text/xml\">XML</option>";
        html_stream << "<option value=\"application/json\">JSON</option>";
        html_stream << "</select></p></form>" << std::endl;
        printTimer(html_stream, &timerServer, nullptr);
        writeFinancialSupportStatement(html_stream);
        writeLinkToReport(html_stream);
        writeLinkToGithub(html_stream);
        writeContactDetails(html_stream);
        html_stream << "</body>" << std::endl << "</html>" << std::endl << std::endl;

        const auto html_code = html_stream.str();
        const auto html_code_size = html_code.length();

        dprintf(fd, "HTTP/1.1 200 OK\n");
        dprintf(fd, "Content-Type: text/html; charset=utf-8\r\n");
        dprintf(fd, "Cache-Control: publicv\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\r\n");
        dprintf(fd, "Content-Length: %ld\r\n", html_code_size);
        dprintf(fd, "\r\n%s\r\n\r\n", html_code.c_str());
    }

    void writeResultsHTML(int fd, const std::string &textToLocalize, const std::vector<Result> &results) {
        std::ostringstream html_stream;
        html_stream << "<!DOCTYPE html>" << std::endl << "<html>" << std::endl << "<head>" << std::endl << "<meta charset=\"UTF-8\">" << std::endl;
        html_stream << "<link rel=\"stylesheet\" type=\"text/css\" href=\"/default.css\" />" << std::endl << "<link rel=\"icon\" type=\"image/x-icon\" href=\"/favicon.ico\" />" << std::endl;

        if (!results.empty()) {
            html_stream << "<title>PBFLookup: " << results.size() << " Results</title>" << std::endl << "</head>" << std::endl << "<body>" << std::endl;
            html_stream << "<h1 style=\"padding-left:1.5em; background-image: url('/favicon.ico'); background-repeat: no-repeat; background-size: contain;\">Results</h1><p>For the following input of " << textToLocalize.length() << "&nbsp;Bytes, <strong>" << results.size() << " results</strong> were located:</p>" << std::endl;
            html_stream << "<p><tt>" << XMLize(textToLocalize) << "</tt></p>" << std::endl;
            html_stream << "<p><a href=\".\">New search</a></p>" << std::endl;

            html_stream << "<h2>Found Locations</h2>" << std::endl;
            static const size_t maxCountResults = 20;
            html_stream << "<p>Number of results: " << results.size();
            if (results.size() > maxCountResults) html_stream << " (not all shown)";
            html_stream <<     "</p>" << std::endl;
            html_stream << "<table id=\"results\">" << std::endl << "<thead><tr><th>Coordinates</th><th>Link to OpenStreetMap</th><th>Hint on Result</th></thead>" << std::endl << "<tbody>" << std::endl;
            size_t resultCounter = maxCountResults;
            for (const Result &result : results) {
                if (--resultCounter <= 0) break; ///< Limit number of results
                Error::debug("Printing HTML code for result: %s", result.origin.c_str());

                const double lon = Coord::toLongitude(result.coord.x);
                const double lat = Coord::toLatitude(result.coord.y);
                const int scbarea = sweden->insideSCBarea(result.coord, Sweden::LevelMunicipality);
                static const int zoom = 15;
                html_stream << "<tr><td><a href=\"https://www.openstreetmap.org/?mlat=" << lat << "&amp;mlon=" << lon << "#map=" << zoom << "/" << lat << "/" << lon << "\" target=\"_blank\">lat= " << lat << "<br/>lon= " << lon << "</a><br/>near " << Sweden::nameOfSCBarea(scbarea) << ", " << Sweden::nameOfSCBarea(scbarea / 100) << "</td>";
                html_stream << "<td><a href=\"https://www.openstreetmap.org/?mlat=" << lat << "&amp;mlon=" << lon << "#map=" << zoom << "/" << lat << "/" << lon << "\" target=\"_blank\">";
                const int tileX = long2tilex(lon, zoom), tileY = lat2tiley(lat, zoom);
                unsigned char load_balancer = 'a' + (resultCounter % 3);
                html_stream << "<img class=\"extratile\" src=\"https://a.tile.openstreetmap.org/" << zoom << "/" << (tileX - 1) << "/" << (tileY - 1) << ".png\" width=\"256\" height=\"256\" /><img class=\"extratile\" src=\"https://a.tile.openstreetmap.org/" << zoom << "/" << tileX << "/" << (tileY - 1) << ".png\" width=\"256\" height=\"256\" /><img class=\"extratile\" src=\"https://a.tile.openstreetmap.org/" << zoom << "/" << (tileX + 1) << "/" << (tileY - 1) << ".png\" width=\"256\" height=\"256\" /><br/>";
                html_stream << "<img class=\"extratile\" src=\"https://b.tile.openstreetmap.org/" << zoom << "/" << (tileX - 1) << "/" << tileY << ".png\" width=\"256\" height=\"256\" /><img src=\"https://" << load_balancer << ".tile.openstreetmap.org/" << zoom << "/" << tileX << "/" << tileY << ".png\" width=\"256\" height=\"256\" /><img class=\"extratile\" src=\"https://b.tile.openstreetmap.org/" << zoom << "/" << (tileX + 1) << "/" << tileY << ".png\" width=\"256\" height=\"256\" /><br/>";
                html_stream << "<img class=\"extratile\" src=\"https://c.tile.openstreetmap.org/" << zoom << "/" << (tileX - 1) << "/" << (tileY + 1) << ".png\" width=\"256\" height=\"256\" /><img class=\"extratile\" src=\"https://c.tile.openstreetmap.org/" << zoom << "/" << tileX << "/" << (tileY + 1) << ".png\" width=\"256\" height=\"256\" /><img class=\"extratile\" src=\"https://c.tile.openstreetmap.org/" << zoom << "/" << (tileX + 1) << "/" << (tileY + 1) << ".png\" width=\"256\" height=\"256\" />";
                html_stream << "</a></td><td>" << XMLize(result.origin);
                if (!result.elements.empty()) {
                    html_stream << std::endl << "<small><ul>" << std::endl;
                    for (const OSMElement &e : result.elements) {
                        html_stream << "<li><a target=\"_top\" href=\"";
                        const std::string eid = std::to_string(e.id);
                        switch (e.type) {
                        case OSMElement::Node: html_stream << "https://www.openstreetmap.org/node/" + eid + "\">" << e.operator std::string(); break;
                        case OSMElement::Way: html_stream << "https://www.openstreetmap.org/way/" + eid + "\">" << e.operator std::string(); break;
                        case OSMElement::Relation: html_stream << "https://www.openstreetmap.org/relation/" + eid + "\">" << e.operator std::string(); break;
                        case OSMElement::UnknownElementType: html_stream << "https://www.openstreetmap.org/\">Unknown element type with id " << eid; break;
                        }
                        const std::string name = e.name();
                        if (!name.empty())
                            html_stream << " (" + e.name() + ")";
                        html_stream << "</a></li>" << std::endl;
                    }
                    html_stream << "</ul></small>";
                }
            }
            html_stream << "</tbody></table>" << std::endl;
            html_stream << "<h2>License</h2>" << std::endl;
            html_stream << "<p>Map data license: &copy; OpenStreetMap contributors, licensed under the <a href=\"http://opendatacommons.org/licenses/odbl/\" target=\"_top\">Open Data Commons Open Database License</a> (OBdL)<br/>Map tiles: OpenStreetMap, licensed under the <a href=\"http://creativecommons.org/licenses/by-sa/2.0/\" target=\"_top\">Creative Commons Attribution-ShareAlike&nbsp;2.0 License</a> (CC BY-SA 2.0)<br/>See <a target=\"_top\" href=\"www.openstreetmap.org/copyright\">www.openstreetmap.org/copyright</a> and <a target=\"_top\" href=\"http://wiki.openstreetmap.org/wiki/Legal_FAQ\">http://wiki.openstreetmap.org/wiki/Legal_FAQ</a> for details.</p>" << std::endl;
        } else {
            html_stream << "<title>PBFLookup: No Results</title>" << std::endl << "</head>" << std::endl << "<body>" << std::endl;
            html_stream << "<h1 style=\"padding-left:1.5em; background-image: url('/favicon.ico'); background-repeat: no-repeat; background-size: contain;\">Results</h1><p>Sorry, <strong>no results</strong> could be found for the following input:</p>" << std::endl;
            html_stream << "<p><tt>" << XMLize(textToLocalize) << "</tt></p>" << std::endl;
            html_stream << "<p><a href=\".\">New search</a></p>" << std::endl;
        }
        printTimer(html_stream, &timerServer, &timerSearch);
        writeFinancialSupportStatement(html_stream);
        writeLinkToReport(html_stream);
        writeLinkToGithub(html_stream);
        writeContactDetails(html_stream);
        html_stream << "</body>" << std::endl << "</html>";

        const auto html_code = html_stream.str();
        const auto html_code_size = html_code.length();

        dprintf(fd, "HTTP/1.1 200 OK\n");
        dprintf(fd, "Content-Type: text/html; charset=utf-8\r\n");
        dprintf(fd, "Cache-Control: private, max-age=0, no-cache, no-store\r\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\r\n");
        dprintf(fd, "Content-Length: %ld\r\n", html_code_size);
        dprintf(fd, "\r\n%s\r\n\r\n", html_code.c_str());
    }

    void writeResultsJSON(int fd, const std::vector<Result> &results) {
        int64_t cputime, walltime;

        std::ostringstream html_stream;
        html_stream << "{" << std::endl;
        timerSearch.elapsed(&cputime, &walltime);
        html_stream << "  \"cputime[ms]\": " << (cputime / 1000.0) << "," << std::endl;
        html_stream << "  \"license\": {\n    \"map\": \"OpenStreetMap contributors, licensed under the Open Data Commons Open Database License (ODbL)\",\n    \"tiles\": \"OpenStreetMap, licensed under the Creative Commons Attribution-ShareAlike 2.0 License (CC BY-SA 2.0)\"\n  }," << std::endl;

        static const size_t maxCountResults = results.size() > 20 ? 20 : results.size();
        size_t resultCounter = maxCountResults;
        html_stream << "  \"results\": [" << std::endl;
        for (const Result &result : results) {
            if (--resultCounter <= 0) break; ///< Limit number of results
            html_stream << "    {" << std::endl;

            const double lon = Coord::toLongitude(result.coord.x);
            const double lat = Coord::toLatitude(result.coord.y);
            const int scbarea = sweden->insideSCBarea(result.coord, Sweden::LevelMunicipality);

            html_stream << "      \"latitude\": " << lat << "," << std::endl;
            html_stream << "      \"longitude\": " << lon << "," << std::endl;
            html_stream << "      \"quality\": " << result.quality << "," << std::endl;
            html_stream << "      \"scbareacode\": " << scbarea << "," << std::endl;
            html_stream << "      \"municipality\": \"" << Sweden::nameOfSCBarea(scbarea) << "\"," << std::endl;
            html_stream << "      \"county\": \"" << Sweden::nameOfSCBarea(scbarea / 100) << "\"," << std::endl;
            static const int zoom = 13;
            html_stream << "      \"url\": \"https://www.openstreetmap.org/?mlat=" << lat << "&mlon=" << lon << "#map=" << zoom << "/" << lat << "/" << lon << "\"," << std::endl;
            const int tileX = long2tilex(lon, zoom), tileY = lat2tiley(lat, zoom);
            html_stream << "      \"image\": \"https://" << (unsigned char)('a' + resultCounter % 3) << ".tile.openstreetmap.org/" << zoom << "/" << tileX << "/" << tileY << ".png\"," << std::endl;
            html_stream << "      \"origin\": {" << std::endl;
            std::string jsonified = result.origin;
            boost::replace_all(jsonified, "\"", "'"); ///< replace all double quotation marks with single ones
            html_stream << "        \"description\": \"" << jsonified << "\"," << std::endl;
            html_stream << "        \"elements\": [";
            bool first = true;
            for (const OSMElement &e : result.elements) {
                if (!first)
                    html_stream << ",";
                switch (e.type) {
                case OSMElement::Node: html_stream << std::endl << "          \"node/" << e.id << "\""; break;
                case OSMElement::Way: html_stream << std::endl << "          \"way/" << e.id << "\""; break;
                case OSMElement::Relation: html_stream << std::endl << "          \"relation/" << e.id << "\""; break;
                default: {
                    /// ignore everything else
                }
                }
                first = false;
            }
            html_stream << std::endl << "        ]" << std::endl;
            html_stream << "      }" << std::endl;

            if (resultCounter <= 1)
                html_stream << "    }" << std::endl;
            else
                html_stream << "    }," << std::endl;
        }
        html_stream << "  ]" << std::endl;
        html_stream << "}";

        const auto html_code = html_stream.str();
        const auto html_code_size = html_code.length();

        dprintf(fd, "HTTP/1.1 200 OK\r\n");
        dprintf(fd, "Content-Type: application/json; charset=utf-8\n");
        dprintf(fd, "Cache-Control: private, max-age=0, no-cache, no-store\r\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\r\n");
        dprintf(fd, "Content-Length: %ld\r\n", html_code_size);
        dprintf(fd, "\r\n%s\r\n\r\n", html_code.c_str());
    }

    void writeResultsXML(int fd, const std::vector<Result> &results) {
        int64_t cputime, walltime;

        std::ostringstream html_stream;
        html_stream << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>" << std::endl;

        html_stream << "<pbflookup>" << std::endl;
        timerSearch.elapsed(&cputime, &walltime);
        html_stream << "  <cputime unit=\"ms\">" << (cputime / 1000.0) << "</cputime>" << std::endl;
        html_stream << "  <licenses>" << std::endl << "    <license for=\"map\">OpenStreetMap contributors, licensed under the Open Data Commons Open Database License (ODbL)</license>" << std::endl << "    <license for=\"tiles\">OpenStreetMap, licensed under the Creative Commons Attribution-ShareAlike 2.0 License (CC BY-SA 2.0)</license>" << std::endl << "  </licenses>" << std::endl;

        static const size_t maxCountResults = results.size() > 20 ? 20 : results.size();
        size_t resultCounter = maxCountResults;
        html_stream << "  <results>" << std::endl;
        for (const Result &result : results) {
            if (--resultCounter <= 0) break; ///< Limit number of results
            html_stream << "    <result>" << std::endl;

            const double lon = Coord::toLongitude(result.coord.x);
            const double lat = Coord::toLatitude(result.coord.y);
            const int scbarea = sweden->insideSCBarea(result.coord, Sweden::LevelMunicipality);

            html_stream << "      <latitude format=\"decimal\">" << lat << "</latitude>" << std::endl;
            html_stream << "      <longitude format=\"decimal\">" << lon << "</longitude>" << std::endl;
            html_stream << "      <quality>" << result.quality << "</quality>" << std::endl;
            html_stream << "      <scbareacode>" << scbarea << "</scbareacode>" << std::endl;
            html_stream << "      <municipality>" << Sweden::nameOfSCBarea(scbarea) << "</municipality>" << std::endl;
            html_stream << "      <county>" << Sweden::nameOfSCBarea(scbarea / 100) << "</county>" << std::endl;
            static const int zoom = 13;
            html_stream << "      <url rel=\"openstreetmap\">https://www.openstreetmap.org/?mlat=" << lat << "&amp;mlon=" << lon << "#map=" << zoom << "/" << lat << "/" << lon << "</url>" << std::endl;
            const int tileX = long2tilex(lon, zoom), tileY = lat2tiley(lat, zoom);
            html_stream << "      <image rel=\"tile\">https://" << (unsigned char)('a' + resultCounter % 3) << ".tile.openstreetmap.org/" << zoom << "/" << tileX << "/" << tileY << ".png</image>" << std::endl;
            html_stream << "      <origin>" << std::endl;
            html_stream << "        <description>" << XMLize(result.origin) << "</description>" << std::endl;
            html_stream << "        <elements>";
            for (const OSMElement &e : result.elements) {
                switch (e.type) {
                case OSMElement::Node: html_stream << std::endl << "          <node>" << e.id << "</node>"; break;
                case OSMElement::Way: html_stream << std::endl << "          <way>" << e.id << "</way>"; break;
                case OSMElement::Relation: html_stream << std::endl << "          <relation>" << e.id << "</relation>"; break;
                default: {
                    /// ignore everything else
                }
                }
            }
            html_stream << std::endl << "        </elements>" << std::endl;
            html_stream << "      </origin>" << std::endl;
            html_stream << "    </result>" << std::endl;
        }
        html_stream << "  </results>" << std::endl;

        html_stream << "</pbflookup>";

        const auto html_code = html_stream.str();
        const auto html_code_size = html_code.length();

        dprintf(fd, "HTTP/1.1 200 OK\r\n");
        dprintf(fd, "Content-Type: text/xml; charset=utf-8\r\n");
        dprintf(fd, "Cache-Control: private, max-age=0, no-cache, no-store\r\n");
        dprintf(fd, "Content-Transfer-Encoding: 8bit\r\n");
        dprintf(fd, "Content-Length: %ld\r\n", html_code_size);
        dprintf(fd, "\r\n%s\r\n\r\n", html_code.c_str());
    }
};

HTTPServer::HTTPServer()
    : d(new Private(this))
{
    /// Sort testsets by their names
    std::sort(testsets.begin(), testsets.end(), [](struct testset & a, struct testset & b) {
        return a.name < b.name;
    });
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
    static const size_t maxNumberSlaveSockets = 16;
    size_t numberOfUsedSlaveSockets = 0;
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
        /// Wait up to 1800 seconds (30 minutes)
        static const time_t timeout_sec = 1800;
        timeout.tv_sec = timeout_sec;
        timeout.tv_nsec = 0;

        const int pselect_result = pselect(maxSocket + 1, &readfds, NULL /** writefds */, NULL /** errorfds */, &timeout, &oldsigset);
        if (pselect_result < 0) {
            if (errno == EINTR)
                Error::debug("pselect(..) received signal: doexitserver=%s", doexitserver ? "true" : "false");
            else
                Error::err("pselect(...)  errno=%d  select_result=%d", errno, pselect_result);
        } else if (pselect_result == 0) {
            /// Timeout in pselect(..), nothing happened
            const time_t curtime = time(NULL);
            struct tm *brokentime = localtime(&curtime);
            static const size_t buffer_len = 256;
            static char buffer[buffer_len];
            strncpy(buffer, asctime(brokentime), buffer_len - 1);
            const size_t buffer_str_len = strlen(buffer);
            if (buffer[buffer_str_len - 1] < 0x20) buffer[buffer_str_len - 1] = '\0'; ///< remove line break if there is any
            Error::debug("Timeout in pselect after %d seconds at time/date: %s", timeout_sec, buffer);
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

            size_t idx = INT_MAX;
            for (idx = 0; idx < maxNumberSlaveSockets; ++idx)
                if (slaveConnections[idx].socket < 0) break;
            if (idx >= maxNumberSlaveSockets) {
                Error::warn("Too many slave connections (max=%d)", maxNumberSlaveSockets);
                d->writeHTTPError(slaveSocket, 500);
                close(slaveSocket);
            } else {
                slaveConnections[idx].socket = slaveSocket;
                slaveConnections[idx].data[0] = '\0';
                slaveConnections[idx].pos = 0;
            }
        } else {
            size_t localNumberOfUsedSlaveSockets = 0;
            for (size_t i = 0; i < maxNumberSlaveSockets; ++i) {
                if (slaveConnections[i].socket >= 0) {
                    d->print_socket_status(slaveConnections[i].socket);
                    ++localNumberOfUsedSlaveSockets;
                }
                if (slaveConnections[i].socket >= 0 && FD_ISSET(slaveConnections[i].socket, &readfds)) {
                    d->print_socket_status(slaveConnections[i].socket);

                    const ssize_t max_data_size = maxBufferSize - slaveConnections[i].pos - 1;
                    if (max_data_size == 0) {
                        Error::warn("Buffer is full, cannot store data, just discarding it...");
                        d->writeHTTPError(slaveConnections[i].socket, 500);
                        close(slaveConnections[i].socket);
                        slaveConnections[i].socket = -1;
                        continue;
                    }
                    ssize_t data_size = recv(slaveConnections[i].socket, slaveConnections[i].data + slaveConnections[i].pos, max_data_size, MSG_DONTWAIT);
                    if (data_size > 0 && data_size == max_data_size) {
                        Error::warn("Just received enough data to completely fill buffer for slave socket: %lu Bytes", data_size);
                    }

                    d->print_socket_status(slaveConnections[i].socket);

                    if (data_size < 0) {
                        /// Some error to handle ...
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            /// Try again later
                            Error::info("Got EAGAIN or EWOULDBLOCK");
                            continue;
                        } else {
                            /// Other unspecific error
                            Error::warn("Got errno=%d when receiving data", errno);
                            d->writeHTTPError(slaveConnections[i].socket, 500);
                            close(slaveConnections[i].socket);
                            slaveConnections[i].socket = -1;
                            continue;
                        }
                    } else if (data_size == 0) {
                        /// Remote peer closed connection, so do we, too
                        Error::info("Remote peer closed connection on socket %d, doing the same on this end (errno=%d)", slaveConnections[i].socket, errno);
                        close(slaveConnections[i].socket);
                        slaveConnections[i].socket = -1;
                        continue;
                    }
                    /// else: data_size > 0

                    /// Some data has been received, more may come
                    slaveConnections[i].pos += data_size;
                    Error::info("Just received %lu bytes of data on socket %u", data_size, slaveConnections[i].socket);
                    /// Remember number of bytes received
                    data_size = slaveConnections[i].pos;
                    slaveConnections[i].data[data_size] = '\0'; ///< ensure string termination

                    if (data_size == maxBufferSize - 1) {
                        /// Buffer is full
                        Error::debug("Slave socket's buffer is full");
                        /// Try to erase partial words at the end if there are any
                        for (size_t p = data_size; p > maxBufferSize - 64; --p) {
                            if (slaveConnections[i].data[p] <= 0x20 || slaveConnections[i].data[p] == '.' || slaveConnections[i].data[p] == ',' || slaveConnections[i].data[p] == ':' || slaveConnections[i].data[p] == '!' || slaveConnections[i].data[p] == '?' || slaveConnections[i].data[p] == ';' || slaveConnections[i].data[p] == '-') {
                                slaveConnections[i].data[p] = '\0';
                                data_size = p;
                                break;
                            } else
                                slaveConnections[i].data[p] = '\0';
                        }
                    }

                    const std::string readtext(slaveConnections[i].data);
                    const auto request = d->extractHTTPrequest(readtext);
                    if (std::get<0>(request) == Private::Bad) {
                        Error::warn("Failed to extract HTTP request from text '%s'", readtext.c_str());
                        d->writeHTTPError(slaveConnections[i].socket, 400);
                        close(slaveConnections[i].socket);
                        slaveConnections[i].socket = -1;
                        continue;
                    } else if (std::get<0>(request) == Private::NeedMoreData) {
                        /// More data expected
                        continue;
                    }

                    /// A valid request should have some minimum number of bytes
                    if (data_size < 4 /** less than 4 Bytes doesn't sound right ... */) {
                        Error::warn("Too few bytes read from slave socket: %d bytes only", data_size);
                        d->writeHTTPError(slaveConnections[i].socket, 400);
                        close(slaveConnections[i].socket);
                        slaveConnections[i].socket = -1;
                        continue;
                    } else
                        Error::info("Processing %lu Bytes", data_size);

                    if (std::get<1>(request).method == Private::HTTPrequest::MethodGet) {
                        const std::string &getfilename = std::get<1>(request).filename;
                        if (getfilename == "/")
                            /// Serve default search form
                            d->writeFormHTML(slaveConnections[i].socket);
                        else if (!http_public_files.empty())
                            d->deliverFile(slaveConnections[i].socket, getfilename.c_str());
                        else {
                            Error::warn("Don't know how to serve request for file '%s'", getfilename.c_str());
                            d->writeHTTPError(slaveConnections[i].socket, 404, "Could not serve your request for this file:", getfilename);
                            close(slaveConnections[i].socket);
                            slaveConnections[i].socket = -1;
                            continue;
                        }

                        close(slaveConnections[i].socket);
                        slaveConnections[i].socket = -1;
                    } else if (std::get<1>(request).method == Private::HTTPrequest::MethodPost) {
                        std::string lowercasetext = readtext;
                        utf8tolower(lowercasetext);
                        const Private::RequestedMimeType requestedMime = d->extractRequestedMimeType(lowercasetext);
                        const std::string text = d->extractTextToLocalize(lowercasetext);

                        d->print_socket_status(slaveConnections[i].socket);

                        d->timerSearch.start();
                        const std::vector<Result> results = text.length() > 3 ? resultGenerator.findResults(text, 1000, ResultGenerator::VerbositySilent) : std::vector<Result>();
                        d->timerSearch.stop();

                        d->print_socket_status(slaveConnections[i].socket);

                        switch (requestedMime) {
                        case Private::HTML: d->writeResultsHTML(slaveConnections[i].socket, text, results); break;
                        case Private::JSON: d->writeResultsJSON(slaveConnections[i].socket, results); break;
                        case Private::XML: d->writeResultsXML(slaveConnections[i].socket, results); break;
                        }
                        Error::debug("Sent data for mime type %d", requestedMime);

                        close(slaveConnections[i].socket);
                        slaveConnections[i].socket = -1;
                    } else {
                        Error::warn("Unknown/unsupported HTTP method");
                        d->writeHTTPError(slaveConnections[i].socket, 400);
                        close(slaveConnections[i].socket);
                        slaveConnections[i].socket = -1;
                        continue;
                    }
                }
            }
            if (localNumberOfUsedSlaveSockets > numberOfUsedSlaveSockets)
                numberOfUsedSlaveSockets = localNumberOfUsedSlaveSockets;
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

    Error::info("Maxmimum number of used slave sockets: %d", numberOfUsedSlaveSockets);

    return;
}
