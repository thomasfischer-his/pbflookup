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

#ifndef HTMLOUTPUT_H
#define HTMLOUTPUT_H

#include <string>

#include "tokenizer.h"
#include "weightednodeset.h"
#include "idtree.h"

class HtmlOutput
{
public:
    explicit HtmlOutput(const Tokenizer &tokenizer, const IdTree<WriteableString> &nodeNames, const WeightedNodeSet &wns);
    ~HtmlOutput();

    bool write(const std::vector<std::string> &tokenizedWords, const std::string &outputDirectory) const;

private:
    class Private;
    Private *const d;
};

#endif // HTMLOUTPUT_H
