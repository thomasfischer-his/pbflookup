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

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>

struct Coord;

extern std::string tempdir;
extern std::string mapname;
extern std::string pidfilename;
extern std::string osmpbffilename;
extern std::string stopwordfilename;
extern unsigned int http_port;
extern std::string http_interface;
extern std::string http_public_files;

extern std::ofstream logfile; ///< defined in 'error.cpp'

enum LoggingLevel {LevelDebug = 0, LevelInfo = 1, LevelWarn = 2, LevelError = 3};
extern LoggingLevel minimumLoggingLevel; ///< defined in 'error.cpp'

struct testset {
    std::string name;
    std::vector<Coord> coord;
    std::string text;
    std::string svgoutputfilename;
};
extern std::vector<struct testset> testsets;

bool init_configuration(const char *configfilename);

/**
 * Check if the software running in 'server mode', i.e.
 * running a HTTP server.
 * This function has undefined behavior before
 * init_configuration(const char *configfilename) has
 * been called.
 * @return True if HTTP is (to be) started, otherwise false
 */
bool server_mode();

#endif // CONFIG_H
