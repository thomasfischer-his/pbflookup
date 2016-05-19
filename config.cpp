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

#include <fstream>

#include <ctime>
#include <unistd.h>

/// see http://www.hyperrealm.com/libconfig/
#include <libconfig.h++>

#include "error.h"
#include "idtree.h"
#include "helper.h"

#define MAX_STRING_LEN 1024

std::string tempdir;
std::string mapname;
std::string pidfilename;
std::string osmpbffilename;
std::string inputextfilename;
std::string stopwordfilename;
unsigned int http_port;
std::string http_interface;
std::string http_public_files;

std::vector<struct testset> testsets;

static time_t current_time;

void replacetildehome(std::string &text) {
    if (text.length() > 2 && text[0] == '~' && text[1] == '/') {
        static const char *home = getenv("HOME");
        text.replace(0, 1, home);
    }
}

void replacevariablenames(std::string &text) {
    static  char timestamp[64];
    strftime(timestamp, 64 - 1, "%Y%m%d-%H%M%S-", localtime(&current_time));
    const std::vector<std::pair<std::string, std::string>> replace_map = {std::make_pair("${mapname}", mapname), std::make_pair("${tempdir}", tempdir), std::make_pair("${timestamp}", timestamp + std::to_string(getpid()))};

    for (const auto &mapping : replace_map) {
        size_t needle_pos = std::string::npos;
        while ((needle_pos = text.find(mapping.first)) != std::string::npos) {
            text = text.substr(0, needle_pos) + mapping.second + text.substr(needle_pos + mapping.first.length());
        }
    }

    /// Find an replace environment variables
    static const char *needle_varstart = "${";
    static const char *needle_varend = "}";
    size_t needle_varstart_pos = std::string::npos;
    while ((needle_varstart_pos = text.find(needle_varstart)) != std::string::npos) {
        size_t needle_varend_pos = text.find(needle_varend, needle_varstart_pos + 1);
        if (needle_varend_pos == std::string::npos)
            Error::err("Cannot replace environment variable, invalid synatx in '%s'", text.c_str());
        const std::string envname = text.substr(needle_varstart_pos + 2, needle_varend_pos - needle_varstart_pos - 2);
        const char *envvar = getenv(envname.c_str());
        if (envvar == NULL)
            Error::err("Environment variable '%s' is not set", envname.c_str());
        else if (envvar[0] == '\0') {
            Error::warn("Environment variable '%s' is empty", envname.c_str());
            text = text.substr(0, needle_varstart_pos) + text.substr(needle_varend_pos + 1);
        } else
            text = text.substr(0, needle_varstart_pos) + envvar + text.substr(needle_varend_pos + 1);
    }
}

void makeabsolutepath(std::string &text, const std::string &relative_to_file = std::string()) {
    if (!text.empty() && text[0] != '/') {
        if (relative_to_file.empty()) {
            /// no relative-to specified, use current working directory
            char cwd[MAX_STRING_LEN];
            if (getcwd(cwd, MAX_STRING_LEN - 1) != NULL) {
                /// Insert current working directory in front of relative path
                /// Requires some copying of strings ...
                text.insert(0, "/").insert(0, cwd);
            }
        } else {
            const size_t last_slash_pos = relative_to_file.rfind("/");
            if (last_slash_pos != std::string::npos)
                text.insert(0, relative_to_file.substr(0, last_slash_pos + 1));
        }
    }
}

bool init_configuration(const char *configfilename) {
    http_port = 0;

    /**
     * Modify given configuration filename:
     * - Resolve '~/' into the user's home directory
     * - Resolve a relative path into an absolute one
     *   based on the current working directory
     */
    std::string internal_configfilename(configfilename);
    replacetildehome(internal_configfilename);
    makeabsolutepath(internal_configfilename);

#ifdef DEBUG
    Error::debug("%sttached to terminal", isatty(1) ? "A" : "NOT a");
    Error::info("Loading configuration file '%s'", internal_configfilename.c_str());
#endif // DEBUG

    time(&current_time);///< get and memorize current time

    libconfig::Config config;

    /// Tell libconfig that the main configuration file's directory
    /// should be used as base for '@include' statements with
    /// relative paths.
    const size_t lastslash_pos = internal_configfilename.rfind("/");
    if (lastslash_pos != std::string::npos && lastslash_pos > 1) {
        const std::string include_dir = internal_configfilename.substr(0, lastslash_pos);
        Error::debug("Including directory '%s' when searching for config files", include_dir.c_str());
        /// For details see
        /// http://www.hyperrealm.com/libconfig/libconfig_manual.html#index-setIncludeDir-on-Config
        config.setIncludeDir(include_dir.c_str());
    }

    try
    {
        std::string str_buffer;

        /// libconfig::lookupValue should, according to its documentation, simply
        /// return 'false' if a key is not found, but it still throws an exception.
        /// To cover for this case, first check if a key exists before attempting
        /// to retrieve its value.
#define configIfExistsLookup(config, key, variable) (config.exists(key) && config.lookupValue(key, variable))

        config.readFile(internal_configfilename.c_str());

        if (!configIfExistsLookup(config, "tempdir", tempdir)) {
            const char *envtempdir = getenv("TEMPDIR");
            if (envtempdir != NULL && envtempdir[0] != '\0')
                tempdir = envtempdir;
            else
                tempdir = "/tmp";
        }
        replacetildehome(tempdir);
        makeabsolutepath(tempdir, internal_configfilename);
#ifdef DEBUG
        Error::debug("  tempdir = '%s'", tempdir.c_str());
#endif // DEBUG

        if (!configIfExistsLookup(config, "mapname", mapname))
            mapname = "sweden";
#ifdef DEBUG
        Error::debug("  mapname = '%s'", mapname.c_str());
#endif // DEBUG

        std::string logfilename;
        if (!configIfExistsLookup(config, "logfile", logfilename))
            logfilename.clear();
        replacetildehome(logfilename);
        replacevariablenames(logfilename);
        makeabsolutepath(logfilename, internal_configfilename);
#ifdef DEBUG
        Error::debug("  logfilename = '%s'", logfilename.c_str());
#endif // DEBUG
        if (!logfilename.empty())
            logfile.open(logfilename);

        minimumLoggingLevel = LevelDebug; ///< default value if nothing else set
        if (configIfExistsLookup(config, "loglevel", str_buffer) && str_buffer.length() > 3) {
            if (str_buffer[0] == 'd' && str_buffer[1] == 'e' && str_buffer[2] == 'b')
                minimumLoggingLevel = LevelDebug;
            else if (str_buffer[0] == 'i' && str_buffer[1] == 'n' && str_buffer[2] == 'f')
                minimumLoggingLevel = LevelInfo;
            else if (str_buffer[0] == 'w' && str_buffer[1] == 'a' && str_buffer[2] == 'r')
                minimumLoggingLevel = LevelWarn;
            else if (str_buffer[0] == 'e' && str_buffer[1] == 'r' && str_buffer[2] == 'r')
                minimumLoggingLevel = LevelError;
        }

        if (!configIfExistsLookup(config, "pidfile", pidfilename)) {
            const char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
            if (xdg_runtime_dir != NULL)
                pidfilename = std::string(xdg_runtime_dir) + "/pbflookup.pid";
            else
                pidfilename = "${tempdir}/pbflookup.pid";
        }
        replacetildehome(pidfilename);
        replacevariablenames(pidfilename);
        makeabsolutepath(pidfilename, internal_configfilename);
#ifdef DEBUG
        Error::debug("  pidfilename = '%s'", pidfilename.c_str());
#endif // DEBUG

        if (!configIfExistsLookup(config, "osmpbffilename", osmpbffilename)) {
            if (!mapname.empty())
                osmpbffilename = mapname + "-latest.osm.pbf";
            else
                Error::err("No filename for .osm.pbf file set and cannot determine automatically");
        }
        replacetildehome(osmpbffilename);
        replacevariablenames(osmpbffilename);
        makeabsolutepath(osmpbffilename, internal_configfilename);
#ifdef DEBUG
        Error::debug("  osmpbffilename = '%s'", osmpbffilename.c_str());
#endif // DEBUG

        if (!configIfExistsLookup(config, "stopwordfilename", stopwordfilename)) {
            if (!mapname.empty())
                stopwordfilename = "stopwords-" + mapname + ".txt";
            else
                Error::err("No filename for stopword file set and cannot determine automatically");
        }
        replacetildehome(stopwordfilename);
        replacevariablenames(stopwordfilename);
        makeabsolutepath(stopwordfilename, internal_configfilename);
#ifdef DEBUG
        Error::debug("  stopwordfilename = '%s'", stopwordfilename.c_str());
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
                                replacetildehome(ts.svgoutputfilename);
                                replacevariablenames(ts.svgoutputfilename);
                                makeabsolutepath(ts.svgoutputfilename);
                            }
                            Error::debug("  name=%s  at   https://www.openstreetmap.org/#map=17/%.4f/%.4f", ts.name.c_str(), ts.coord.front().latitude(), ts.coord.front().longitude());
                            testsets.push_back(ts);
                        }
                    }
                }
            }
        Error::info("Testsets: %i in total", testsets.size());

        serverSocket = -1;
        if (config.exists("http_port")) {
            http_port = config.lookup("http_port");
            if (http_port < 1024 || http_port > 65535)
                Error::err("http_port is invalid or privileged port (<1024), both are not acceptable");

            if (!configIfExistsLookup(config, "http_interface", http_interface))
                http_interface = "ANY";

            if (!configIfExistsLookup(config, "http_public_files", http_public_files))
                /// If no 'http_public_files' was defined, do not provide any default
                http_public_files.clear();
            replacetildehome(http_public_files);
            replacevariablenames(http_public_files);
            makeabsolutepath(http_public_files, internal_configfilename);
            const size_t http_public_files_len = http_public_files.length();
            if (http_public_files_len > 1 && http_public_files[http_public_files_len - 1] == '/') http_public_files[http_public_files_len - 1] = '\0';

#ifdef DEBUG
            Error::debug("  http_port = %d", http_port);
            Error::debug("  http_interface = %s", http_interface.c_str());
            Error::debug("  http_public_files = %s/", http_public_files.c_str());
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
