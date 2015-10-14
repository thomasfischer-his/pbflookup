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

#include <libconfig.h++>

#include "error.h"

#define MAX_STRING_LEN 1024
#define max(a,b) ((a)>(b))?(a):(b)
#define min(a,b) ((a)<(b))?(a):(b)

char mapname[MAX_STRING_LEN];
char osmpbffilename[MAX_STRING_LEN];

bool init_configuration(const char *configfilename) {
#ifdef DEBUG
    Error::info("Loading configuration file '%s'", configfilename);
#endif // DEBUG

    libconfig::Config config;
    try
    {
        char temp[MAX_STRING_LEN];
        const char *buffer;
        config.readFile(configfilename);

        if (config.lookupValue("mapname", buffer))
            strncpy(mapname, buffer, MAX_STRING_LEN - 1);
        else
            snprintf(mapname, MAX_STRING_LEN - 1, "sweden");
#ifdef DEBUG
        Error::info("  mapname = '%s'", mapname);
#endif // DEBUG

        if (config.lookupValue("osmpbffilename", buffer))
            strncpy(osmpbffilename, buffer, MAX_STRING_LEN - 1);
        else
            snprintf(osmpbffilename, MAX_STRING_LEN - 1, "~/git/pbflookup/${mapname}.osm.pbf");
        if (osmpbffilename[0] == '~' && osmpbffilename[1] == '/') {
            strncpy(temp, osmpbffilename, MAX_STRING_LEN);
            const char *home = getenv("HOME");
            strncpy(osmpbffilename, home, MAX_STRING_LEN);
            strncpy(osmpbffilename + strlen(home), temp + 1, MAX_STRING_LEN - strlen(home));
        }
        char *needle = strstr(osmpbffilename, "${mapname}");
        if (needle != NULL) {
            strncpy(temp, osmpbffilename, MAX_STRING_LEN);
            const size_t prefixlen = needle - osmpbffilename;
            const size_t mapnamelen = strlen(mapname);
            static const size_t variablelen = strlen("${mapname}");
            strncpy(needle, mapname, MAX_STRING_LEN - prefixlen);
            strncpy(needle + mapnamelen, temp + prefixlen + variablelen, MAX_STRING_LEN - prefixlen - variablelen);
        }
#ifdef DEBUG
        Error::info("  osmpbffilename = '%s'", osmpbffilename);
#endif // DEBUG

    }
    catch (std::exception &e)
    {
        Error::err("Parsing configuration file failed: %s", e.what());
        return false;
    }

    return true;
}
