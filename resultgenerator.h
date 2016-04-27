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

#ifndef RESULTGENERATOR_H
#define RESULTGENERATOR_H

#include "idtree.h"

struct Result {
    Result(const Coord &_coord, double _quality, const std::string &_origin)
        : coord(_coord), quality(_quality), origin(_origin) {
        /** nothing */
    }

    Coord coord;
    double quality;
    std::string origin;
    std::vector<OSMElement> elements;
};


class ResultGenerator {
public:
    enum Verbosity { VerbositySilent = 0, VerbosityTalking = 5};

    static std::vector<Result> findResults(const std::string &text, ResultGenerator::Verbosity verbosity);
};

#endif // RESULTGENERATOR_H
