/***************************************************************************
 *   Copyright (C) 2015 by Thomas Fischer <thomas.fischer@his.se>          *
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

#define MAX_STRING_LEN 1024

char tempdir[MAX_STRING_LEN];
char mapname[MAX_STRING_LEN];
char osmpbffilename[MAX_STRING_LEN];
char inputextfilename[MAX_STRING_LEN];
char stopwordfilename[MAX_STRING_LEN];

std::vector<struct testset> testsets;

static time_t current_time;

void replacetildehome(char *text) {
    if (text[0] == '~' && text[1] == '/') {
        static const char *home = getenv("HOME");
        static char temp[MAX_STRING_LEN];
        strncpy(temp, text, MAX_STRING_LEN);
        strncpy(text, home, MAX_STRING_LEN);
        strncpy(text + strlen(home), temp + 1, MAX_STRING_LEN - strlen(home));
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
}

bool init_configuration(const char *configfilename) {
    memset(tempdir, 0, MAX_STRING_LEN);
    memset(mapname, 0, MAX_STRING_LEN);
    memset(osmpbffilename, 0, MAX_STRING_LEN);
    memset(inputextfilename, 0, MAX_STRING_LEN);
    memset(stopwordfilename, 0, MAX_STRING_LEN);

    /**
     * Modify given configuration filename:
     * - Resolve '~/' into the user's home directory
     * - Resolve a relative path into an absolute one
     *   based on the current working directory
     */
    char internal_configfilename[MAX_STRING_LEN];
    strncpy(internal_configfilename, configfilename, MAX_STRING_LEN - 1);
    replacetildehome(internal_configfilename);
    if (internal_configfilename[0] != '\0' && internal_configfilename[0] != '/') {
        char cwd[MAX_STRING_LEN];
        if (getcwd(cwd, MAX_STRING_LEN) != NULL) {
            /// Insert current working directory in front of relative path
            /// Requires some copying of strings ...
            char *buffer = strndup(internal_configfilename, MAX_STRING_LEN);
            strncpy(internal_configfilename, cwd, MAX_STRING_LEN);
            size_t p = strlen(internal_configfilename);
            p += snprintf(internal_configfilename + p, MAX_STRING_LEN - p, "/");
            strncpy(internal_configfilename + p, buffer, MAX_STRING_LEN - p);
            free(buffer);
        }
    }

#ifdef DEBUG
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
        strncpy(temp, internal_configfilename, lastslash - internal_configfilename);
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
        const char *buffer;
        config.readFile(internal_configfilename);

        if (config.lookupValue("tempdir", buffer))
            strncpy(tempdir, buffer, MAX_STRING_LEN - 1);
        else {
            const char *envtempdir = getenv("TEMPDIR");
            if (envtempdir != NULL && envtempdir[0] != '\0')
                strncpy(tempdir, envtempdir, MAX_STRING_LEN - 1);
            else
                snprintf(tempdir, MAX_STRING_LEN - 1, "/tmp");
        }
#ifdef DEBUG
        Error::debug("  tempdir = '%s'", tempdir);
#endif // DEBUG

        if (config.lookupValue("mapname", buffer))
            strncpy(mapname, buffer, MAX_STRING_LEN - 1);
        else
            snprintf(mapname, MAX_STRING_LEN - 1, "sweden");
#ifdef DEBUG
        Error::debug("  mapname = '%s'", mapname);
#endif // DEBUG

        static char logfilename[MAX_STRING_LEN];
        if (config.lookupValue("logfile", buffer)) {
            strncpy(logfilename, buffer, MAX_STRING_LEN - 1);
            replacetildehome(logfilename);
            replacevariablenames(logfilename);
        } else
            logfilename[0] = '\0';
#ifdef DEBUG
        Error::debug("  logfilename = '%s'", logfilename);
#endif // DEBUG
        if (logfilename[0] != '\0') {
            logfile = fopen(logfilename, "w");
            /// TODO figure out how to close file at program termination
        }

        if (config.lookupValue("osmpbffilename", buffer))
            strncpy(osmpbffilename, buffer, MAX_STRING_LEN - 1);
        else
            snprintf(osmpbffilename, MAX_STRING_LEN - 1, "${mapname}-latest.osm.pbf");
        replacetildehome(osmpbffilename);
        replacevariablenames(osmpbffilename);
#ifdef DEBUG
        Error::debug("  osmpbffilename = '%s'", osmpbffilename);
#endif // DEBUG

        if (config.lookupValue("inputextfilename", buffer))
            strncpy(inputextfilename, buffer, MAX_STRING_LEN - 1);
        else
            /// If no 'inputextfilename' was defined, do not provide any default
            inputextfilename[0] = '\0';
        replacetildehome(inputextfilename);
        replacevariablenames(inputextfilename);
#ifdef DEBUG
        Error::debug("  inputextfilename = '%s'", inputextfilename);
#endif // DEBUG

        if (config.lookupValue("stopwordfilename", buffer))
            strncpy(stopwordfilename, buffer, MAX_STRING_LEN - 1);
        else
            snprintf(stopwordfilename, MAX_STRING_LEN - 1, "stopwords-${mapname}.txt");
        replacetildehome(stopwordfilename);
        replacevariablenames(stopwordfilename);
#ifdef DEBUG
        Error::debug("  stopwordfilename = '%s'", stopwordfilename);
#endif // DEBUG

        testsets.clear();
        if (config.exists("testsets")) {
            libconfig::Setting &setting = config.lookup("testsets");
            if (setting.isList()) {
                Error::info("Testsets: %i in total", setting.getLength());
                for (auto it = setting.begin(); it != setting.end(); ++it) {
                    const libconfig::Setting &testsetSetting = *it;
                    if (testsetSetting.isGroup()) {
                        struct testset ts;
                        ts.name = testsetSetting.lookup("name").c_str();
                        ts.lat = testsetSetting.lookup("latitude");
                        ts.lon = testsetSetting.lookup("longitude");
                        ts.text = testsetSetting.lookup("text").c_str();
                        /// "svgoutputfilename" is optional, so tolerate if it is not set
                        if (testsetSetting.lookupValue("svgoutputfilename", ts.svgoutputfilename)) {
                            // FIXME can we do better than converting std::string -> char* -> std::string ?
                            static char filename[MAX_STRING_LEN];
                            strncpy(filename, ts.svgoutputfilename.c_str(), MAX_STRING_LEN);
                            replacetildehome(filename);
                            replacevariablenames(filename);
                            ts.svgoutputfilename = std::string(filename);
                        }
                        Error::debug("  name=%s  at   http://www.openstreetmap.org/#map=17/%.4f/%.4f", ts.name.c_str(), ts.lat, ts.lon);
                        testsets.push_back(ts);
                    }
                }
            }
        }
    }
    catch (libconfig::ParseException &pe)
    {
        Error::err("ParseException: Parsing configuration file '%s' failed in line %i: %s", pe.getFile(), pe.getLine(), pe.getError());
        return false;
    }
    catch (std::exception &e)
    {
        Error::err("Parsing configuration file failed: %s", e.what());
        return false;
    }

    return true;
}
