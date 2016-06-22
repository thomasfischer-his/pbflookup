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

#include <climits>
#include <string>
#include <vector>
#include <sstream>

#include "types.h"

OSMElement getNodeInOSMElement(const OSMElement &element);
bool getCenterOfOSMElement(const OSMElement &element, struct Coord &coord);

std::string &utf8tolower(std::string &text);

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems, bool skip_empty = true);

#ifdef LATEX_OUTPUT
std::string teXify(const std::string &input);
std::string rewrite_TeX_spaces(const std::string &input);
#endif // LATEX_OUTPUT

bool inSortedArray(const uint64_t *array, const size_t array_size, const uint64_t needle);
