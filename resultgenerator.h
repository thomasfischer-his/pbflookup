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

class Tokenizer;
class TokenProcessor;
class MapAnalysis;

struct Result {
    Result(const Coord &_coord, double _quality, const std::string &_origin)
        : coord(_coord), quality(_quality), origin(_origin) {
        /** nothing */
    }

    /// Comparison operator, necessary e.g. for std::unordered_set.
    const bool operator==(const Result &r) const;

    /// Comparison operator, necessary e.g. for std::set.
    const bool operator<(const Result &r) const;

    Coord coord;
    double quality;
    std::string origin;
    std::vector<OSMElement> elements;
};


class ResultGenerator {
public:
    struct Statistics {
        size_t word_count = 0;
        size_t word_combinations_count = 0;
    };

    enum Verbosity { VerbositySilent = 0, VerbosityTalking = 5};

    ResultGenerator();
    ~ResultGenerator();

    std::vector<Result> findResults(const std::string &text, int duplicateProximity, ResultGenerator::Verbosity verbosity, ResultGenerator::Statistics *statistics = nullptr);

private:
    Tokenizer *tokenizer;
    TokenProcessor *tokenProcessor;
    MapAnalysis *mapAnalysis;
};

#endif // RESULTGENERATOR_H
