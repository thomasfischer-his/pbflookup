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

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>

struct Coord;

extern char tempdir[];
extern char mapname[];
extern char osmpbffilename[];
extern char inputextfilename[];
extern char stopwordfilename[];
extern unsigned int http_port;
extern char http_interface[];
extern char cssfilename[];

extern FILE *logfile; ///< defined in 'error.cpp'

struct testset {
    std::string name;
    std::vector<Coord> coord;
    std::string text;
    std::string svgoutputfilename;
};
extern std::vector<struct testset> testsets;

bool init_configuration(const char *configfilename);

#endif // CONFIG_H
