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

/***************************************************************************
 *   This code is heavily based on file  tools/osmpbf-outline.cpp          *
 *   from OSM-binary:  https://github.com/scrosby/OSM-binary               *
 *   OSM-binary is also licensed under the GNU Public License v3.          *
 *                                                                         *
 *   Authors of   tools/osmpbf-outline.cpp   include (in alphabetic        *
 *   order):                                                               *
 *     alex85k <alexei_kasatkin@mail.ru>                                   *
 *     Bas Couwenberg <sebastic@xs4all.nl>                                 *
 *     Charlie Root <root@freebsd.vvtis>                                   *
 *     Dmitry Marakasov <amdmi3@amdmi3.ru>                                 *
 *     Frederik Ramm <frederik@remote.org>                                 *
 *     Hartmut Holzgraefe <hartmut@php.net>                                *
 *     Jochen Topf <jochen@topf.org>                                       *
 *     kayrus <kay.diam@gmail.com>                                         *
 *     keine-ahnung <keine.ahnung.ka@googlemail.com>                       *
 *     michael <michael@192.168.1.3>                                       *
 *     Peter <github@mazdermind.de>                                        *
 *     Scott Crosby <scrosby@cs.rice.edu>                                  *
 *     scrosby <scrosby@cs.rice.edu>                                       *
 *     Thomas Friebel <yaron@codefreax.com>                                *
 ***************************************************************************/

#ifndef OSMPBFREADER_H
#define OSMPBFREADER_H

#include <istream>

/// This is the header to pbf format
#include <osmpbf/osmpbf.h>

#include "idtree.h"
#include "sweden.h"
#include "swedishtexttree.h"

class OsmPbfReader
{
public:
    OsmPbfReader();
    ~OsmPbfReader();

    bool parse(std::istream &input);

private:
    /// Buffer for reading a compressed blob from file
    char *buffer;
    /// Buffer for decompressing the blob
    char *unpack_buffer;

    // pbf struct of a BlobHeader
    OSMPBF::BlobHeader blobheader;

    // pbf struct of a Blob
    OSMPBF::Blob blob;

    // pbf struct of an OSM HeaderBlock
    OSMPBF::HeaderBlock headerblock;

    // pbf struct of an OSM PrimitiveBlock
    OSMPBF::PrimitiveBlock primblock;

    static const uint64_t exclaveInclaveWays[];
};

#endif // OSMPBFREADER_H
