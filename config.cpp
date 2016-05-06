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

#include "config.h"

#include <cstring>
#include <ctime>
#include <unistd.h>

/// see http://www.hyperrealm.com/libconfig/
#include <libconfig.h++>

#include "error.h"
#include "idtree.h"
#include "helper.h"

#define MAX_STRING_LEN 1024

char tempdir[MAX_STRING_LEN];
char mapname[MAX_STRING_LEN];
char pidfilename[MAX_STRING_LEN];
char osmpbffilename[MAX_STRING_LEN];
char inputextfilename[MAX_STRING_LEN];
char stopwordfilename[MAX_STRING_LEN];
unsigned int http_port;
char http_interface[MAX_STRING_LEN];
char http_public_files[MAX_STRING_LEN];

std::vector<struct testset> testsets;

static time_t current_time;

void replacetildehome(char *text) {
    if (text[0] == '~' && text[1] == '/') {
        static const char *home = getenv("HOME");
        static char temp[MAX_STRING_LEN];
        strncpy(temp, text, MAX_STRING_LEN - 1);
        strncpy(text, home, MAX_STRING_LEN - 1);
        strncpy(text + strlen(home), temp + 1, MAX_STRING_LEN - strlen(home) - 1);
    }
}

void replacevariablenames(char *text) {
    static const char needle_mapname[] = "${mapname}";
    char *needlepos = strstr(text, needle_mapname);
    if (needlepos != NULL) {
        static char temp[MAX_STRING_LEN];
        strncpy(temp, text, MAX_STRING_LEN);
        const size_t prefixlen = needlepos - text;
        const size_t mapnamelen = strlen(mapname);
        static const size_t variablelen = strlen(needle_mapname);
        strncpy(needlepos, mapname, MAX_STRING_LEN - prefixlen);
        strncpy(needlepos + mapnamelen, temp + prefixlen + variablelen, MAX_STRING_LEN - prefixlen - variablelen);
    }

    static const char needle_tempdir[] = "${tempdir}";
    needlepos = strstr(text, needle_tempdir);
    if (needlepos != NULL) {
        static char temp[MAX_STRING_LEN];
        strncpy(temp, text, MAX_STRING_LEN);
        const size_t prefixlen = needlepos - text;
        const size_t tempdirlen = strlen(tempdir);
        static const size_t variablelen = strlen(needle_tempdir);
        strncpy(needlepos, tempdir, MAX_STRING_LEN - prefixlen);
        strncpy(needlepos + tempdirlen, temp + prefixlen + variablelen, MAX_STRING_LEN - prefixlen - variablelen);
    }

    if (current_time != (time_t)(-1)) {
        static const char needle_timestamp[] = "${timestamp}";
        needlepos = strstr(text, needle_timestamp);
        if (needlepos != NULL) {
            static char temp[MAX_STRING_LEN], timestamp[MAX_STRING_LEN];
            strftime(timestamp, MAX_STRING_LEN, "%Y%m%d-%H%M%S", localtime(&current_time));
            snprintf(timestamp + 15, MAX_STRING_LEN - 15, "-%d", getpid());
            strncpy(temp, text, MAX_STRING_LEN);
            const size_t prefixlen = needlepos - text;
            const size_t timestamplen = strlen(timestamp);
            static const size_t variablelen = strlen(needle_timestamp);
            strncpy(needlepos, timestamp, MAX_STRING_LEN - prefixlen);
            strncpy(needlepos + timestamplen, temp + prefixlen + variablelen, MAX_STRING_LEN - prefixlen - variablelen);
        }
    }

    /// Find an replace environment variables
    static const char *needle_varstart = "${";
    static const char *needle_varend = "}";
    needlepos = strstr(text, needle_varstart);
    while (needlepos != NULL) {
        static char temp[MAX_STRING_LEN];
        const char *needleend = strstr(needlepos, needle_varend);
        const size_t needle_len = needleend - needlepos - 2;
        snprintf(temp, needle_len + 1, needlepos + 2);
        const char *envvar = getenv(temp);
        const size_t envvarlen = strlen(envvar);
        if (envvarlen == 0)
            Error::warn("Environment variable '%s' is empty or not set", temp);
        const size_t prefixlen = needlepos - text;
        strncpy(temp, text, MAX_STRING_LEN - 1);
        strncpy(needlepos, envvar, MAX_STRING_LEN - prefixlen - 1);
        strncpy(needlepos + envvarlen, temp + prefixlen + needle_len + 3, MAX_STRING_LEN - prefixlen - needle_len - 4);

        /// Continue searching after current replacement
        needlepos = strstr(needlepos + envvarlen, needle_varstart);
    }
}

void makeabsolutepath(char *text) {
    if (text != NULL && text[0] != '\0' && text[0] != '/') {
        char cwd[MAX_STRING_LEN];
        if (getcwd(cwd, MAX_STRING_LEN) != NULL) {
            /// Insert current working directory in front of relative path
            /// Requires some copying of strings ...
            char *buffer = strndup(text, MAX_STRING_LEN);
            const size_t len = snprintf(text, MAX_STRING_LEN - 1, "%s/%s", cwd, buffer);
            text[len] = '\0';
            free(buffer);
        }
    }
}

bool init_configuration(const char *configfilename) {
    memset(tempdir, 0, MAX_STRING_LEN);
    memset(mapname, 0, MAX_STRING_LEN);
    memset(pidfilename, 0, MAX_STRING_LEN);
    memset(osmpbffilename, 0, MAX_STRING_LEN);
    memset(inputextfilename, 0, MAX_STRING_LEN);
    memset(stopwordfilename, 0, MAX_STRING_LEN);
    http_port = 0;
    memset(http_interface, 0, MAX_STRING_LEN);

    /**
     * Modify given configuration filename:
     * - Resolve '~/' into the user's home directory
     * - Resolve a relative path into an absolute one
     *   based on the current working directory
     */
    char internal_configfilename[MAX_STRING_LEN];
    strncpy(internal_configfilename, configfilename, MAX_STRING_LEN - 1);
    replacetildehome(internal_configfilename);
    makeabsolutepath(internal_configfilename);

#ifdef DEBUG
    Error::debug("%sttached to terminal", isatty(1) ? "A" : "NOT a");
    Error::info("Loading configuration file '%s'", internal_configfilename);
#endif // DEBUG

    time(&current_time);///< get and memorize current time

    libconfig::Config config;

    /// Tell libconfig that the main configuration file's directory
    /// should be used as base for '@include' statements with
    /// relative paths.
    const char *lastslash = rindex(internal_configfilename, '/');
    if (lastslash != NULL) {
        char temp[MAX_STRING_LEN];
        temp[0] = '\0';
        size_t len = lastslash - internal_configfilename;
        if (len > MAX_STRING_LEN - 2) len = MAX_STRING_LEN - 2; ///< Prevent exceeding 'temp's size
        strncpy(temp, internal_configfilename, len);
        temp[len] = '\0';
        if (temp[0] == '\0') {
            /// Configuration file's location is / (very unusual)
            temp[0] = '/'; temp[1] = '\0';
        }
        Error::debug("Including directory '%s' when searching for config files", temp);
        /// For details see
        /// http://www.hyperrealm.com/libconfig/libconfig_manual.html#index-setIncludeDir-on-Config
        config.setIncludeDir(temp);
    }

    try
    {
        /// libconfig::lookupValue should, according to its documentation, simply
        /// return 'false' if a key is not found, but it still throws an exception.
        /// To cover for this case, first check if a key exists before attempting
        /// to retrieve its value.
#define configIfExistsLookup(config, key, variable) (config.exists(key) && config.lookupValue(key, variable))

        const char *buffer;
        config.readFile(internal_configfilename);

        if (configIfExistsLookup(config, "tempdir", buffer))
            strncpy(tempdir, buffer, MAX_STRING_LEN - 1);
        else {
            const char *envtempdir = getenv("TEMPDIR");
            if (envtempdir != NULL && envtempdir[0] != '\0')
                strncpy(tempdir, envtempdir, MAX_STRING_LEN - 1);
            else
                snprintf(tempdir, MAX_STRING_LEN - 1, "/tmp");
        }
        replacetildehome(tempdir);
        makeabsolutepath(tempdir);
#ifdef DEBUG
        Error::debug("  tempdir = '%s'", tempdir);
#endif // DEBUG

        if (configIfExistsLookup(config, "mapname", buffer))
            strncpy(mapname, buffer, MAX_STRING_LEN - 1);
        else
            snprintf(mapname, MAX_STRING_LEN - 1, "sweden");
#ifdef DEBUG
        Error::debug("  mapname = '%s'", mapname);
#endif // DEBUG

        static char logfilename[MAX_STRING_LEN];
        if (configIfExistsLookup(config, "logfile", buffer)) {
            strncpy(logfilename, buffer, MAX_STRING_LEN - 1);
            replacetildehome(logfilename);
            replacevariablenames(logfilename);
        } else
            logfilename[0] = '\0';
        makeabsolutepath(logfilename);
#ifdef DEBUG
        Error::debug("  logfilename = '%s'", logfilename);
#endif // DEBUG
        if (logfilename[0] != '\0')
            logfile.open(logfilename);

        minimumLoggingLevel = LevelDebug; ///< default value if nothing else set
        if (configIfExistsLookup(config, "loglevel", buffer)) {
            if (buffer[0] == 'd' && buffer[1] == 'e' && buffer[2] == 'b')
                minimumLoggingLevel = LevelDebug;
            else if (buffer[0] == 'i' && buffer[1] == 'n' && buffer[2] == 'f')
                minimumLoggingLevel = LevelInfo;
            else if (buffer[0] == 'w' && buffer[1] == 'a' && buffer[2] == 'r')
                minimumLoggingLevel = LevelWarn;
            else if (buffer[0] == 'e' && buffer[1] == 'r' && buffer[2] == 'r')
                minimumLoggingLevel = LevelError;
        }

        if (configIfExistsLookup(config, "pidfile", buffer))
            strncpy(pidfilename, buffer, MAX_STRING_LEN - 1);
        else
            snprintf(pidfilename, MAX_STRING_LEN - 1, "${XDG_RUNTIME_DIR}/pbflookup.pid");
        replacetildehome(pidfilename);
        replacevariablenames(pidfilename);
        makeabsolutepath(pidfilename);
#ifdef DEBUG
        Error::debug("  pidfilename = '%s'", pidfilename);
#endif // DEBUG

        if (configIfExistsLookup(config, "osmpbffilename", buffer))
            strncpy(osmpbffilename, buffer, MAX_STRING_LEN - 1);
        else if (mapname[0] != '\0')
            snprintf(osmpbffilename, MAX_STRING_LEN - 1, "${mapname}-latest.osm.pbf");
        else
            Error::err("No filename for .osm.pbf file set and cannot determine automatically");
        replacetildehome(osmpbffilename);
        replacevariablenames(osmpbffilename);
        makeabsolutepath(osmpbffilename);
#ifdef DEBUG
        Error::debug("  osmpbffilename = '%s'", osmpbffilename);
#endif // DEBUG

        if (configIfExistsLookup(config, "inputextfilename", buffer))
            strncpy(inputextfilename, buffer, MAX_STRING_LEN - 1);
        else
            /// If no 'inputextfilename' was defined, do not provide any default
            inputextfilename[0] = '\0';
        replacetildehome(inputextfilename);
        replacevariablenames(inputextfilename);
        makeabsolutepath(inputextfilename);
#ifdef DEBUG
        Error::debug("  inputextfilename = '%s'", inputextfilename);
#endif // DEBUG

        if (configIfExistsLookup(config, "stopwordfilename", buffer))
            strncpy(stopwordfilename, buffer, MAX_STRING_LEN - 1);
        else
            snprintf(stopwordfilename, MAX_STRING_LEN - 1, "stopwords-${mapname}.txt");
        replacetildehome(stopwordfilename);
        replacevariablenames(stopwordfilename);
        makeabsolutepath(stopwordfilename);
#ifdef DEBUG
        Error::debug("  stopwordfilename = '%s'", stopwordfilename);
#endif // DEBUG

        testsets.clear();
        static const std::vector<std::string> testsetKeySuffixes = {"", "1", "2", "3", "4", "5", "6", "A", "B", "C", "D", "E", "F"};
        for (const std::string &testsetKeySuffix : testsetKeySuffixes)
            if (config.exists("testsets" + testsetKeySuffix)) {
                libconfig::Setting &setting = config.lookup("testsets" + testsetKeySuffix);
                if (setting.isList()) {
                    for (const libconfig::Setting &testsetSetting : setting) {
                        if (testsetSetting.isGroup()) {
                            struct testset ts;
                            ts.name = testsetSetting.lookup("name").c_str();
                            const libconfig::Setting &lat = testsetSetting.lookup("latitude");
                            const libconfig::Setting &lon = testsetSetting.lookup("longitude");
                            if (lat.isScalar() && lon.isScalar())
                                ts.coord.push_back(Coord((double)lon, (double)lat));
                            else if (lat.isArray() && lon.isArray())
                                for (auto itLat = lat.begin(), itLon = lon.begin(); itLat != lat.end() && itLon != lon.end(); ++itLat, ++itLon)
                                    ts.coord.push_back(Coord((double)*itLon, (double)*itLat));
                            else
                                throw libconfig::SettingTypeException(testsetSetting, "Latitude and/or longitude given in wrong format (need to be both scalar or both array)");
                            ts.text = testsetSetting.lookup("text").c_str();
                            /// "svgoutputfilename" is optional, so tolerate if it is not set
                            if (configIfExistsLookup(config, "svgoutputfilename", ts.svgoutputfilename)) {
                                // FIXME can we do better than converting std::string -> char* -> std::string ?
                                static char filename[MAX_STRING_LEN];
                                strncpy(filename, ts.svgoutputfilename.c_str(), MAX_STRING_LEN);
                                replacetildehome(filename);
                                replacevariablenames(filename);
                                ts.svgoutputfilename = std::string(filename);
                            }
                            Error::debug("  name=%s  at   http://www.openstreetmap.org/#map=17/%.4f/%.4f", ts.name.c_str(), ts.coord.front().latitude(), ts.coord.front().longitude());
                            testsets.push_back(ts);
                        }
                    }
                }
            }
        Error::info("Testsets: %i in total", testsets.size());

        serverSocket = -1;
        if (config.exists("http_port")) {
            http_port = config.lookup("http_port");

            if (configIfExistsLookup(config, "http_interface", buffer))
                strncpy(http_interface, buffer, MAX_STRING_LEN - 1);
            else
                snprintf(http_interface, MAX_STRING_LEN - 1, "ANY");

            if (configIfExistsLookup(config, "http_public_files", buffer))
                strncpy(http_public_files, buffer, MAX_STRING_LEN - 1);
            else
                /// If no 'http_public_files' was defined, do not provide any default
                http_public_files[0] = '\0';
            replacetildehome(http_public_files);
            replacevariablenames(http_public_files);
            makeabsolutepath(http_public_files);
            const size_t http_public_files_len = strlen(http_public_files);
            if (http_public_files_len > 1 && http_public_files[http_public_files_len - 1] == '/') http_public_files[http_public_files_len - 1] = '\0';

#ifdef DEBUG
            Error::debug("  http_port = %d", http_port);
            Error::debug("  http_interface = %s", http_interface);
            Error::debug("  http_public_files = %s/", http_public_files);
#endif // DEBUG
        } else {
            http_port = 0;
#ifdef DEBUG
            Error::debug("  http_port = DISABLED");
#endif // DEBUG
        }
    }
    catch (libconfig::ParseException &pe)
    {
        Error::err("ParseException: Parsing configuration file '%s' failed in line %i: %s (%s)", pe.getFile(), pe.getLine(), pe.getError(), pe.what());
        return false;
    }
    catch (libconfig::SettingTypeException &ste)
    {
        Error::err("SettingTypeException: Parsing configuration failed: %s", ste.what());
        return false;
    }
    catch (libconfig::ConfigException &ce)
    {
        Error::err("ConfigException: Parsing configuration failed: %s", ce.what());
        return false;
    }
    catch (std::exception &e)
    {
        Error::err("General exception: Parsing configuration file failed: %s", e.what());
        return false;
    }

    return true;
}

bool server_mode() {
    return http_port > 0;
}
