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

#include "osmpbfreader.h"

/// Zlib compression is used inside the pbf blobs
#include <zlib.h>

/// netinet or winsock2 provides the network-byte-order conversion function
#ifdef D_HAVE_WINSOCK
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#include <stack>

#include "swedishtexttree.h"
#include "error.h"

const uint64_t OsmPbfReader::exclaveInclaveWays[] = {
    320878424,///< Mullsjo/Falkoping
    132851007,///< Kristianstad/Tomelilla
    132851008,///< Kristianstad/Tomelilla
    132865858,///< Simrishamn/Tomelilla
    185009590,///< Simrishamn/Kristianstad
    321283489,///<  Lilla Edet/Ale
    321283490,///<  Lilla Edet/Ale
    317893976,///<  Lilla Edet/Ale
    321283488,///<  Lilla Edet/Ale
    317893975,///<  Lilla Edet/Ale
    317893972,///<  Lilla Edet/Ale
    321283487,///<  Lilla Edet/Ale
    293357457,///<  Skovde/Skara
    293357462,///<  Skovde/Skara
    293357458,///<  Skovde/Skara/Falkoping
    322957346,///<  Skovde/Skara/Falkoping
    293357456,///<  Skovde/Skara/Falkoping
    0
};

OsmPbfReader::OsmPbfReader()
{
    buffer = new char[OSMPBF::max_uncompressed_blob_size];
    if (buffer == NULL)
        Error::err("Could not allocate memory for OsmPbfReader::buffer");
    unpack_buffer = new char[OSMPBF::max_uncompressed_blob_size];
    if (unpack_buffer == NULL)
        Error::err("Could not allocate memory for OsmPbfReader::unpack_buffer");

    minlat = 1000.0;
    minlon = 1000.0;
    maxlat = -1000.0;
    maxlon = -1000.0;
}

OsmPbfReader::~OsmPbfReader()
{
    delete[] buffer;
    delete[] unpack_buffer;
}

bool OsmPbfReader::parse(std::istream &input, SwedishText::Tree **swedishTextTree, IdTree<Coord> **n2c, IdTree<WayNodes> **w2n, IdTree<RelationMem> **relmem, Sweden **sweden) {
    *swedishTextTree = NULL;
    *n2c = NULL;
    *w2n = NULL;
    *relmem = NULL;
    *sweden = NULL;
    minlat = 1000.0;
    minlon = 1000.0;
    maxlat = -1000.0;
    maxlon = -1000.0;

    if (!input || !input.good())
        return false;

    *swedishTextTree = new SwedishText::Tree();
    *n2c = new IdTree<Coord>();
    if (n2c == NULL)
        Error::err("Could not allocate memory for n2c");
    *w2n = new IdTree<WayNodes>();
    if (w2n == NULL)
        Error::err("Could not allocate memory for w2n");
    *relmem = new IdTree<RelationMem>();
    if (relmem == NULL)
        Error::err("Could not allocate memory for relmem");
    *sweden = new Sweden(*n2c, *w2n, *relmem);
    if (sweden == NULL)
        Error::err("Could not allocate memory for Sweden");

    int simplifiedWayAllocationSize = 1024;
    uint64_t *simplifiedWay = (uint64_t *)calloc(simplifiedWayAllocationSize, sizeof(uint64_t));

    /// Read while the file has not reached its end
    while (input.good()) {
        /// Storage of size, used multiple times
        int32_t sz;

        /// Read the first 4 bytes of the file, this is the size of the blob-header
        input.read((char *)(&sz), sizeof(sz));
        if (!input.good()) {
            break; /// End of file reached
        }

        /// Convert the size from network byte-order to host byte-order
        sz = ntohl(sz);

        /// Ensure the blob-header is smaller then MAX_BLOB_HEADER_SIZE
        if (sz > OSMPBF::max_blob_header_size) {
            Error::err("blob-header-size is bigger then allowed (%u > %u)", sz, OSMPBF::max_blob_header_size);
        }

        // read the blob-header from the file
        input.read(buffer, sz);
        if (input.gcount() != sz || input.fail()) {
            Error::err("unable to read blob-header from file");
        }

        // parse the blob-header from the read-buffer
        if (!blobheader.ParseFromArray(buffer, sz)) {
            Error::err("unable to parse blob header");
        }

        // size of the following blob
        sz = blobheader.datasize();
        //debug("  datasize = %u", sz);

        // ensure the blob is smaller then MAX_BLOB_SIZE
        if (sz > OSMPBF::max_uncompressed_blob_size) {
            Error::err("blob-size is bigger then allowed (%u > %u)", sz, OSMPBF::max_uncompressed_blob_size);
        }

        // read the blob from the file
        input.read(buffer, sz);
        if (input.gcount() != sz || input.fail()) {
            Error::err("unable to read blob from file");
        } else
            std::cout << (sz > (1 << 18) ? "*" : (sz > (1 << 16) ? ":" : ".")) << std::flush;

        // parse the blob from the read-buffer
        if (!blob.ParseFromArray(buffer, sz)) {
            Error::err("unable to parse blob");
        }

        // set when we find at least one data stream
        bool found_data = false;

        // if the blob has uncompressed data
        if (blob.has_raw()) {
            // we have at least one datastream
            found_data = true;

            // size of the blob-data
            sz = blob.raw().size();

            // check that raw_size is set correctly
            if (sz != blob.raw_size()) {
                Error::warn("  reports wrong raw_size: %u bytes", blob.raw_size());
            }

            // tell about the blob-data
            //debug("  contains uncompressed data: %u bytes", sz);

            // copy the uncompressed data over to the unpack_buffer
            memcpy(unpack_buffer, buffer, sz);
        }

        // if the blob has zlib-compressed data
        if (blob.has_zlib_data()) {
            // issue a warning if there is more than one data steam, a blob may only contain one data stream
            if (found_data) {
                Error::warn("  contains several data streams");
            }

            // we have at least one datastream
            found_data = true;

            // the size of the compressesd data
            sz = blob.zlib_data().size();

            // zlib information
            z_stream z;

            // next byte to decompress
            z.next_in   = (unsigned char *) blob.zlib_data().c_str();

            // number of bytes to decompress
            z.avail_in  = sz;

            // place of next decompressed byte
            z.next_out  = (unsigned char *) unpack_buffer;

            // space for decompressed data
            z.avail_out = blob.raw_size();

            // misc
            z.zalloc    = Z_NULL;
            z.zfree     = Z_NULL;
            z.opaque    = Z_NULL;

            if (inflateInit(&z) != Z_OK) {
                Error::err("  failed to init zlib stream");
            }
            if (inflate(&z, Z_FINISH) != Z_STREAM_END) {
                Error::err("  failed to inflate zlib stream");
            }
            if (inflateEnd(&z) != Z_OK) {
                Error::err("  failed to deinit zlib stream");
            }

            // unpacked size
            sz = z.total_out;
        }

        // if the blob has lzma-compressed data
        if (blob.has_lzma_data()) {
            // issue a warning if there is more than one data steam, a blob may only contain one data stream
            if (found_data) {
                Error::warn("  contains several data streams");
            }

            // we have at least one datastream
            found_data = true;

            // issue a warning, lzma compression is not yet supported
            Error::err("  lzma-decompression is not supported");
        }

        // check we have at least one data-stream
        if (!found_data) {
            Error::err("  does not contain any known data stream");
        }

        // switch between different blob-types
        if (blobheader.type() == "OSMHeader") {
            // parse the HeaderBlock from the blob
            if (!headerblock.ParseFromArray(unpack_buffer, sz)) {
                Error::err("unable to parse header block");
            }
        } else if (blobheader.type() == "OSMData") {
            // parse the PrimitiveBlock from the blob
            if (!primblock.ParseFromArray(unpack_buffer, sz)) {
                Error::err("unable to parse primitive block");
            }

            // iterate over all PrimitiveGroups
            for (int i = 0, l = primblock.primitivegroup_size(); i < l; i++) {
                // one PrimitiveGroup from the the Block
                OSMPBF::PrimitiveGroup pg = primblock.primitivegroup(i);

                bool found_items = false;
                const double coord_scale = 0.000000001;

                if (pg.nodes_size() > 0) {
                    found_items = true;

                    const int maxnodes = pg.nodes_size();
                    for (int j = 0; j < maxnodes; ++j) {
                        const double lat = coord_scale * (primblock.lat_offset() + (primblock.granularity() * pg.nodes(j).lat()));
                        const double lon = coord_scale * (primblock.lon_offset() + (primblock.granularity() * pg.nodes(j).lon()));
                        (*n2c)->insert(pg.nodes(j).id(), Coord(lon, lat));
                        if (lat > maxlat) maxlat = lat;
                        if (lat < minlat) minlat = lat;
                        if (lon > maxlon) maxlon = lon;
                        if (lon < minlon) minlon = lon;

                        for (int k = 0; k < pg.nodes(j).keys_size(); ++k) {
                            const char *ckey = primblock.stringtable().s(pg.nodes(j).keys(k)).c_str();
                            if (strcmp("name", ckey) == 0) {
                                const uint64_t id = pg.nodes(j).id();
                                const bool result = (*swedishTextTree)->insert(primblock.stringtable().s(pg.nodes(j).vals(k)), id << 2 | NODE_NIBBLE);
                                if (!result)
                                    Error::warn("Cannot insert %s", primblock.stringtable().s(pg.nodes(j).vals(k)).c_str());
                            }
                        }
                    }
                }

                if (pg.has_dense()) {
                    found_items = true;

                    uint64_t last_id = 0;
                    int last_keyvals_pos = 0;
                    double last_lat = 0.0, last_lon = 0.0;
                    const int idmax = pg.dense().id_size();
                    for (int j = 0; j < idmax; ++j) {
                        last_id += pg.dense().id(j);
                        last_lat += coord_scale * (primblock.lat_offset() + (primblock.granularity() * pg.dense().lat(j)));
                        last_lon += coord_scale * (primblock.lon_offset() + (primblock.granularity() * pg.dense().lon(j)));
                        (*n2c)->insert(last_id, Coord(last_lon, last_lat));
                        if (last_lat > maxlat) maxlat = last_lat;
                        if (last_lat < minlat) minlat = last_lat;
                        if (last_lon > maxlon) maxlon = last_lon;
                        if (last_lon < minlon) minlon = last_lon;

                        bool isKey = true;
                        int key = 0, value = 0;
                        while (last_keyvals_pos < pg.dense().keys_vals_size()) {
                            const int key_val = pg.dense().keys_vals(last_keyvals_pos);
                            ++last_keyvals_pos;
                            if (key_val == 0) break;
                            if (isKey) {
                                key = key_val;
                                isKey = false;
                            } else { /// must be value
                                value = key_val;
                                isKey = true;

                                const char *ckey = primblock.stringtable().s(key).c_str();
                                if (strcmp("name", ckey) == 0) {
                                    const bool result = (*swedishTextTree)->insert(primblock.stringtable().s(value), last_id << 2 | NODE_NIBBLE);
                                    if (!result)
                                        Error::warn("Cannot insert %s", primblock.stringtable().s(value).c_str());
                                }
                            }
                        }
                    }
                }

                if (pg.ways_size() > 0) {
                    found_items = true;


                    const int maxways = pg.ways_size();
                    for (int w = 0; w < maxways; ++w) {
                        const uint64_t wayId = pg.ways(w).id();
                        bool isBlacklisted = false;
                        for (int ei = 0; !isBlacklisted && exclaveInclaveWays[ei] > 0; ++ei)
                            isBlacklisted = exclaveInclaveWays[ei] == wayId;
                        if (isBlacklisted) continue; ///< skip black-listed ways

                        for (int k = 0; k < pg.ways(w).keys_size(); ++k) {
                            const char *ckey = primblock.stringtable().s(pg.ways(w).keys(k)).c_str();
                            if (strcmp("name", ckey) == 0) {
                                const bool result = (*swedishTextTree)->insert(primblock.stringtable().s(pg.ways(w).vals(k)), wayId << 2 | WAY_NIBBLE);
                                if (!result)
                                    Error::warn("Cannot insert %s", primblock.stringtable().s(pg.ways(w).vals(k)).c_str());
                            } else if (strcmp("ref", ckey) == 0) {
                                (*sweden)->insertWayAsRoad(wayId, primblock.stringtable().s(pg.ways(w).vals(k)).c_str());
                            }
                        }

                        if (pg.ways(w).refs_size() + 2 > simplifiedWayAllocationSize) {
                            simplifiedWayAllocationSize = ((pg.ways(w).refs_size() >> 8) + 1) << 8;
                            free(simplifiedWay);
                            simplifiedWay = (uint64_t *)calloc(simplifiedWayAllocationSize, sizeof(uint64_t));
                        }
                        uint64_t *simplifiedWay = (uint64_t *)calloc(pg.ways(w).refs_size() + 2, sizeof(uint64_t));
                        const int simplifiedWaySize = applyRamerDouglasPeucker(pg.ways(w), *n2c, simplifiedWay);

                        WayNodes wn(simplifiedWaySize);
                        memcpy(wn.nodes, simplifiedWay, sizeof(uint64_t)*simplifiedWaySize);
                        for (int k = 0; k < simplifiedWaySize; ++k)
                            (*n2c)->increaseUseCounter(simplifiedWay[k]);
                        (*w2n)->insert(wayId, wn);
                    }
                }

                if (pg.relations_size() > 0) {
                    found_items = true;

                    const int maxrelations = pg.relations_size();
                    for (int i = 0; i < maxrelations; ++i) {
                        const int maxkv = pg.relations(i).keys_size();
                        for (int k = 0; k < maxkv; ++k) {
                            const char *ckey = primblock.stringtable().s(pg.relations(i).keys(k)).c_str();
                            const uint64_t id = pg.relations(i).id();
                            if (strcmp("name", ckey) == 0) {
                                const bool result = (*swedishTextTree)->insert(primblock.stringtable().s(pg.relations(i).vals(k)), id << 2 | RELATION_NIBBLE);
                                if (!result)
                                    Error::warn("Cannot insert %s", primblock.stringtable().s(pg.relations(i).vals(k)).c_str());
                            } else if (strcmp("ref:scb", ckey) == 0) {
                                /// Found SCB reference (two digits for lands, four digits for municipalities
                                (*sweden)->insertSCBarea(std::stoi(primblock.stringtable().s(pg.relations(i).vals(k))), id);
                            } else if (strcmp("ref:nuts:3", ckey) == 0) {
                                /// Found three-digit NUTS reference (SEnnn)
                                (*sweden)->insertNUTS3area(std::stoi(primblock.stringtable().s(pg.relations(i).vals(k)).substr(2)), id);
                            }
                        }

                        const uint64_t relId = pg.relations(i).id();
                        RelationMem rm(pg.relations(i).memids_size());
                        uint64_t memId = 0;
                        for (int k = 0; k < pg.relations(i).memids_size(); ++k) {
                            uint16_t flags = 0;
                            memId += pg.relations(i).memids(k);
                            if (strcmp("outer", primblock.stringtable().s(pg.relations(i).roles_sid(k)).c_str()) == 0)
                                flags |= RelationFlags::RoleOuter;
                            rm.member_ids[k] = memId;
                            rm.member_flags[k] = flags;
                        }
                        (*relmem)->insert(relId, rm);
                    }
                }

                if (!found_items) {
                    Error::warn("      contains no items");
                }
            }
        }

        else {
            // unknown blob type
            Error::warn("  unknown blob type: %s", blobheader.type().c_str());
        }
    }

    free(simplifiedWay);

    /// Line break after series of dots
    std::cout << std::endl;

    return true;
}

double OsmPbfReader::min_lat() const {
    return minlat;
}
double OsmPbfReader::max_lat() const {
    return maxlat;
}
double OsmPbfReader::min_lon() const {
    return minlon;
}
double OsmPbfReader::max_lon() const {
    return maxlon;
}

int OsmPbfReader::applyRamerDouglasPeucker(const ::OSMPBF::Way &ways, IdTree<Coord> *n2c, uint64_t *result) {
    uint64_t nodeId = 0;
    for (int i = 0; i < ways.refs_size(); ++i) {
        nodeId += ways.refs(i);
        result[i] = nodeId;
    }

    std::stack<std::pair<int, int> > recursion;
    recursion.push(std::pair<int, int>(0, ways.refs_size() - 1));

    while (!recursion.empty()) {
        const std::pair<int, int> nextPair = recursion.top();
        recursion.pop();
        const int a = nextPair.first, b = nextPair.second;

        double dmax = -1.0;
        int dnode = -1;
        for (int i = a + 1; i < b; ++i) {
            if (result[i] == 0) continue;
            const double d = shortestDistanceToSegment(result[a], result[i], result[b], n2c);
            if (d > dmax) {
                dmax = d;
                dnode = i;
            }
        }

        static const double epsilon = 5.0;
        if (dmax > epsilon) {
            recursion.push(std::pair<int, int>(a, dnode));
            recursion.push(std::pair<int, int>(dnode, b));
        }
        else
            for (int i = a + 1; i < b; ++i)
                result[i] = 0;
    }

    int p = 0;
    for (int i = 0; i < ways.refs_size(); ++i)
        if (result[i] > 0) {
            if (i > p) result[p] = result[i];
            ++p;
        }
    return p;
}

double OsmPbfReader::shortestDistanceToSegment(uint64_t nodeA, uint64_t nodeInBetween, uint64_t nodeB, IdTree<Coord> *n2c) const {
    Coord coordA, coordInBetween, coordB;

    if (n2c->retrieve(nodeA, coordA) && n2c->retrieve(nodeInBetween, coordInBetween) && n2c->retrieve(nodeB, coordB)) {
        // TODO
        return 10000.0;
    } else
        return 0.0;
}
