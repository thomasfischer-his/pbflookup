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
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef GLOBAL_H
#define GLOBAL_H

static const size_t maxStringLen = 1024;

/// Minimum latitude and longitude for Sweden
extern const double minlon, minlat; ///< defined in 'sweden.cpp'
extern const double maxlon, maxlat; ///< defined in 'sweden.cpp'

/// Decimeter per degree longitude and latitude at N 60 (north of Uppsala)
extern const double decimeterDegreeLongitude, decimeterDegreeLatitude; ///< defined in 'sweden.cpp'

extern int serverSocket; ///< defined in http_server.cpp

#endif // GLOBAL_H
