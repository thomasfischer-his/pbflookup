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

#include "osmpbfreader.h"

/// Zlib compression is used inside the pbf blobs
#include <zlib.h>

/// netinet or winsock2 provides the network-byte-order conversion function
#ifdef D_HAVE_WINSOCK
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif
#include <unistd.h>

/// For threading
#include <boost/thread/thread.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/atomic.hpp>

#include <stack>
#include <sstream>

#include "swedishtexttree.h"
#include "config.h"
#include "error.h"
#include "globalobjects.h"

#define SHORT_STRING_BUFFER_SIZE     64

/// Data structure for producer-consumer threads
/// which will simplify ways before storing them
struct OSMWay {
    OSMWay(const ::OSMPBF::Way &way)
        : id(way.id()), size(way.refs_size()) {
        nodes = (uint64_t *)malloc(size * sizeof(uint64_t));

        uint64_t node_id = 0;
        for (size_t i = 0; i < size; ++i) {
            node_id += way.refs(i);
            nodes[i] = node_id;
        }
    }

    ~OSMWay() {
        free(nodes);
    }

    const uint64_t id;
    const size_t size;
    uint64_t *nodes;
};
/// Queue of ways, used in producer-consumer threads
boost::lockfree::queue<const OSMWay *> queueWaySimplification(1 << 16);
/// Boolean used to notify consumer threat to finish once consumer will no longer push ways into queue
boost::atomic<bool> doneWaySimplification(false);
/// For statistical purposes, track size of queue (boost::lockfree::queue has no method on its own for that)
boost::atomic<size_t> queueWaySimplificationSize(0);

int shortestSquareDistanceToSegment(uint64_t nodeA, uint64_t nodeInBetween, uint64_t nodeB) {
    Coord coordA, coordInBetween, coordB;

    if (node2Coord->retrieve(nodeA, coordA) && node2Coord->retrieve(nodeInBetween, coordInBetween) && node2Coord->retrieve(nodeB, coordB)) {
        /// http://stackoverflow.com/questions/849211/shortest-distance-between-a-point-and-a-line-segment
        const int d1 = coordB.x - coordA.x;
        const int d2 = coordB.y - coordA.y;
        if (d1 == 0 && d2 == 0) { ///< nodes A and B are equal
            const int d1 = coordA.x - coordInBetween.x;
            const int d2 = coordA.y - coordInBetween.y;
            return d1 * d1 + d2 * d2;
        }
        /// Find a projection of nodeInBetween onto the line
        /// between nodeA and nodeB
        const int l2 = (d1 * d1 + d2 * d2);
        const double t = ((coordInBetween.x - coordA.x) * d1 + (coordInBetween.y - coordA.y) * d2) / (double)l2;
        if (t < 0.0) { ///< beyond node A's end of the segment
            const int d1 = coordA.x - coordInBetween.x;
            const int d2 = coordA.y - coordInBetween.y;
            return d1 * d1 + d2 * d2;
        } else if (t > 1.0) { ///< beyond node B's end of the segment
            const int d1 = coordB.x - coordInBetween.x;
            const int d2 = coordB.y - coordInBetween.y;
            return d1 * d1 + d2 * d2;
        } else {
            const int x = coordA.x + t * (coordB.x - coordA.x) + 0.5;
            const int d = coordB.y - coordA.y;
            const int y = coordA.y + (int)(t * d + 0.5);
            const int d1 = x - coordInBetween.x;
            const int d2 = y - coordInBetween.y;
            return d1 * d1 + d2 * d2;
        }
    } else
        return 0;
}

size_t applyRamerDouglasPeucker(const OSMWay &way, uint64_t *result) {
    std::stack<std::pair<int, int> > recursion;
    recursion.push(std::make_pair(0, way.size - 1));
    memcpy(result, way.nodes, way.size * sizeof(uint64_t));

    while (!recursion.empty()) {
        const std::pair<int, int> nextPair = recursion.top();
        recursion.pop();
        const int a = nextPair.first, b = nextPair.second;

        int dmax = -1;
        int dnode = -1;
        for (int i = a + 1; i < b; ++i) {
            if (result[i] == 0) continue;
            const int dsquare = shortestSquareDistanceToSegment(result[a], result[i], result[b]);
            if (dsquare > dmax) {
                dmax = dsquare;
                dnode = i;
            }
        }

        static const int epsilon = 400;///< 2m corridor (20dm * 20dm)
        if (dmax > epsilon) {
            recursion.push(std::make_pair(a, dnode));
            recursion.push(std::make_pair(dnode, b));
        }
        else
            for (int i = a + 1; i < b; ++i)
                if (node2Coord->counter(result[i]) == 0) { ///< remove only unused/irrelevant nodes
                    result[i] = 0;
                }
    }

    size_t p = 0;
    for (size_t i = 0; i < way.size; ++i)
        if (result[i] > 0) {
            if (i > p) result[p] = result[i];
            ++p;
        }
    return p;
}

void simplifyWay(const OSMWay &way) {
    static const size_t simplifiedWay_size = 8192; /// largest observed way length is 2304
    static uint64_t simplifiedWay[simplifiedWay_size];
    if (way.size > simplifiedWay_size)
        Error::err("Way %llu of size %d is longer than acceptable (%d)", way.id, way.size, simplifiedWay_size);

    const size_t simplifiedWaySize = applyRamerDouglasPeucker(way, simplifiedWay);

    WayNodes wn(simplifiedWaySize);
    memcpy(wn.nodes, simplifiedWay, sizeof(uint64_t) * wn.num_nodes);
    for (size_t k = 0; k < simplifiedWaySize; ++k)
        node2Coord->increaseCounter((simplifiedWay)[k]);
    wayNodes->insert(way.id, wn);
}

/// This method will be run by the consumer thread which is
/// processing (simplifying) ways
void consumerWaySimplification(void) {
    const OSMWay *way;
    while (!doneWaySimplification) {
        while (queueWaySimplification.pop(way)) {
            --queueWaySimplificationSize;
            simplifyWay(*way);
            delete way;
        }
        /// Sleep a little bit to avoid busy waiting
        // TODO use conditions or something like this
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    }
    while (queueWaySimplification.pop(way)) {
        --queueWaySimplificationSize;
        simplifyWay(*way);
        delete way;
    }
}

OsmPbfReader::OsmPbfReader()
{
    buffer = new char[OSMPBF::max_uncompressed_blob_size];
    if (buffer == NULL)
        Error::err("Could not allocate memory for OsmPbfReader::buffer");
    unpack_buffer = new char[OSMPBF::max_uncompressed_blob_size];
    if (unpack_buffer == NULL)
        Error::err("Could not allocate memory for OsmPbfReader::unpack_buffer");
}

OsmPbfReader::~OsmPbfReader()
{
    delete[] buffer;
    delete[] unpack_buffer;
}

bool OsmPbfReader::parse(std::istream &input) {
    std::vector<std::pair<uint64_t, std::string> > roadsWithoutRef;
    swedishTextTree = NULL;
    node2Coord = NULL;
    nodeNames = NULL;
    wayNames = NULL;
    relationNames = NULL;
    wayNodes = NULL;
    relMembers = NULL;
    sweden = NULL;
    size_t count_named_nodes = 0, count_named_ways = 0, count_named_relations = 0;

    if (!input || !input.good())
        return false;

    swedishTextTree = new SwedishTextTree();
    if (swedishTextTree == NULL)
        Error::err("Could not allocate memory for swedishTextTree");
    node2Coord = new IdTree<Coord>();
    if (node2Coord == NULL)
        Error::err("Could not allocate memory for node2Coord");
    nodeNames = new IdTree<WriteableString>();
    if (nodeNames == NULL)
        Error::err("Could not allocate memory for nodeNames");
    wayNames = new IdTree<WriteableString>();
    if (wayNames == NULL)
        Error::err("Could not allocate memory for wayNames");
    relationNames = new IdTree<WriteableString>();
    if (relationNames == NULL)
        Error::err("Could not allocate memory for relationNames");
    wayNodes = new IdTree<WayNodes>();
    if (wayNodes == NULL)
        Error::err("Could not allocate memory for wayNodes");
    relMembers = new IdTree<RelationMem>();
    if (relMembers == NULL)
        Error::err("Could not allocate memory for relmem");
    sweden = new Sweden();
    if (sweden == NULL)
        Error::err("Could not allocate memory for Sweden");

    doneWaySimplification = false;
    boost::thread waySimplificationThread(consumerWaySimplification);
    size_t max_queue_size = 0;

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
        } else if (isatty(1))
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
                        OSMElement::RealWorldType realworld_type = OSMElement::UnknownRealWorldType;
                        std::string name;

                        const double lat = coord_scale * (primblock.lat_offset() + (primblock.granularity() * pg.nodes(j).lat()));
                        const double lon = coord_scale * (primblock.lon_offset() + (primblock.granularity() * pg.nodes(j).lon()));
                        node2Coord->insert(pg.nodes(j).id(), Coord::fromLonLat(lon, lat));

#ifdef DEBUG
                        bool node_is_county = false;
#endif // DEBUG
                        for (int k = 0; k < pg.nodes(j).keys_size(); ++k) {
                            const char *ckey = primblock.stringtable().s(pg.nodes(j).keys(k)).c_str();
                            if (strcmp("name", ckey) == 0) {
                                /// Store 'name' string for later use
                                name = primblock.stringtable().s(pg.nodes(j).vals(k));
                                ++count_named_nodes;
                            } else if (strcmp("place", ckey) == 0) {
                                const char *cvalue = primblock.stringtable().s(pg.nodes(j).vals(k)).c_str();

#ifdef DEBUG
                                if (strcmp("county", cvalue) == 0) {
                                    /// FIX OSM DATA
                                    /// Counties should not be represented by nodes, but by relations
                                    /// representing an area
                                    // realworld_type = OSMElement::PlaceLargeArea;
                                    node_is_county = true;
                                }
#endif // DEBUG

                                if (strcmp("city", cvalue) == 0)
                                    realworld_type = OSMElement::PlaceLarge;
                                else if (strcmp("borough", cvalue) == 0 || strcmp("suburb", cvalue) == 0 || strcmp("town", cvalue) == 0 || strcmp("village", cvalue) == 0)
                                    realworld_type = OSMElement::PlaceMedium;
                                else if (strcmp("quarter", cvalue) == 0 || strcmp("neighbourhood", cvalue) == 0 || strcmp("hamlet", cvalue) == 0 || strcmp("isolated_dwelling", cvalue) == 0)
                                    /// Disabling 'city_block' as those may have misleading names like node 3188612201 ('Skaraborg') in Södermalm, Stockholm
                                    realworld_type = OSMElement::PlaceSmall;
                                else if (strcmp("island", cvalue) == 0)
                                    realworld_type = OSMElement::Island;
                                else {
                                    /// Skipping other types of places:
                                    /// * Administrative boundaries should be checked elsewhere like SCBareas or NUTS3areas
                                    /// * Very small places like farms or plots neither
                                }
                            } else if (strcmp("natural", ckey) == 0) {
                                const char *cvalue = primblock.stringtable().s(pg.nodes(j).vals(k)).c_str();
                                if (strcmp("water", cvalue) == 0)
                                    realworld_type = OSMElement::Water;
                            }
                        }

                        /// Consider only names of length 2 or longer
                        if (name.length() > 1) {
                            const uint64_t id = pg.nodes(j).id();
                            node2Coord->increaseCounter(id);
                            const OSMElement element(id, OSMElement::Node, realworld_type);
                            const bool result = swedishTextTree->insert(name, element);
                            if (!result)
                                Error::warn("Cannot insert %s", name.c_str());
                            nodeNames->insert(id, WriteableString(name));
#ifdef DEBUG
                            if (node_is_county)
                                Error::info("County '%s' is represented by node %llu", name.c_str(), id);
#endif // DEBUG
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
                        OSMElement::RealWorldType realworld_type = OSMElement::UnknownRealWorldType;
                        std::string name;

                        last_id += pg.dense().id(j);
                        last_lat += coord_scale * (primblock.lat_offset() + (primblock.granularity() * pg.dense().lat(j)));
                        last_lon += coord_scale * (primblock.lon_offset() + (primblock.granularity() * pg.dense().lon(j)));
                        node2Coord->insert(last_id, Coord::fromLonLat(last_lon, last_lat));

                        bool isKey = true;
                        int key = 0, value = 0;
#ifdef DEBUG
                        bool node_is_county = false;
#endif // DEBUG
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
                                    /// Store 'name' string for later use
                                    name = primblock.stringtable().s(value);
                                    ++count_named_nodes;
                                } else if (strcmp("place", ckey) == 0) {
                                    const char *cvalue = primblock.stringtable().s(value).c_str();

#ifdef DEBUG
                                    if (strcmp("county", cvalue) == 0) {
                                        /// FIX OSM DATA
                                        /// Counties should not be represented by nodes, but by relations
                                        /// representing an area
                                        // realworld_type = OSMElement::PlaceLargeArea;
                                        node_is_county = true;
                                    }
#endif // DEBUG

                                    if (strcmp("city", cvalue) == 0)
                                        realworld_type = OSMElement::PlaceLarge;
                                    else if (strcmp("borough", cvalue) == 0 || strcmp("suburb", cvalue) == 0 || strcmp("town", cvalue) == 0 || strcmp("village", cvalue) == 0)
                                        realworld_type = OSMElement::PlaceMedium;
                                    else if (strcmp("quarter", cvalue) == 0 || strcmp("neighbourhood", cvalue) == 0 || strcmp("hamlet", cvalue) == 0 || strcmp("isolated_dwelling", cvalue) == 0)
                                        /// Disabling 'city_block' as those may have misleading names like node 3188612201 ('Skaraborg') in Södermalm, Stockholm
                                        realworld_type = OSMElement::PlaceSmall;
                                    else if (strcmp("island", cvalue) == 0)
                                        realworld_type = OSMElement::Island;
                                    else {
                                        /// Skipping other types of places:
                                        /// * Administrative boundaries should be checked elsewhere like SCBareas or NUTS3areas
                                        /// * Very small places like farms or plots neither
                                    }
                                } else if (strcmp("natural", ckey) == 0) {
                                    const char *cvalue = primblock.stringtable().s(value).c_str();
                                    if (strcmp("water", cvalue) == 0)
                                        realworld_type = OSMElement::Water;
                                }
                            }
                        }

                        /// Consider only names of length 2 or longer
                        if (name.length() > 1) {
                            node2Coord->increaseCounter(last_id);
                            const OSMElement element(last_id, OSMElement::Node, realworld_type);
                            const bool result = swedishTextTree->insert(name, element);
                            if (!result)
                                Error::warn("Cannot insert %s", name.c_str());
                            nodeNames->insert(last_id, WriteableString(name));
#ifdef DEBUG
                            if (node_is_county)
                                Error::debug("County '%s' is represented by node %llu", name.c_str(), last_id);
#endif // DEBUG
                        }
                    }
                }

                if (pg.ways_size() > 0) {
                    found_items = true;

                    char buffer_ref[SHORT_STRING_BUFFER_SIZE], buffer_highway[SHORT_STRING_BUFFER_SIZE];
                    const int maxways = pg.ways_size();
                    for (int w = 0; w < maxways; ++w) {
                        const uint64_t wayId = pg.ways(w).id();
                        OSMElement::RealWorldType realworld_type = OSMElement::UnknownRealWorldType;
                        std::string name;
                        buffer_ref[0] = buffer_highway[0] = '\0'; /// clear buffers
                        for (int k = 0; k < pg.ways(w).keys_size(); ++k) {
                            const char *ckey = primblock.stringtable().s(pg.ways(w).keys(k)).c_str();
                            if (strcmp("name", ckey) == 0) {
                                /// Store 'name' string for later use
                                name = primblock.stringtable().s(pg.ways(w).vals(k));
                                ++count_named_ways;
                            } else if (strcmp("highway", ckey) == 0) {
                                const char *cvalue = primblock.stringtable().s(pg.ways(w).vals(k)).c_str();

                                /// Store 'highway' string for later use
                                strncpy(buffer_highway, cvalue, SHORT_STRING_BUFFER_SIZE);

                                if (strcmp("motorway", cvalue) == 0 || strcmp("trunk", cvalue) == 0 || strcmp("primary", cvalue) == 0)
                                    realworld_type = OSMElement::RoadMajor;
                                else if (strcmp("secondary", cvalue) == 0 || strcmp("tertiary", cvalue) == 0)
                                    realworld_type = OSMElement::RoadMedium;
                                else if (strcmp("unclassified", cvalue) == 0 || strcmp("residential", cvalue) == 0 || strcmp("service", cvalue) == 0)
                                    realworld_type = OSMElement::RoadMinor;
                                else {
                                    /// Skipping other types of roads:
                                    /// * Cycle or pedestrian ways
                                    /// * Hiking and 'offroad'
                                    /// * Special cases like turning circles
                                }
                            } else if (strcmp("ref", ckey) == 0)
                                /// Store 'ref' string for later use
                                strncpy(buffer_ref, primblock.stringtable().s(pg.ways(w).vals(k)).c_str(), SHORT_STRING_BUFFER_SIZE);
                            else if (strcmp("building", ckey) == 0)
                                /// Remember if way is a building
                                realworld_type = OSMElement::Building;
                            else if (strcmp("place", ckey) == 0) {
                                const char *cvalue = primblock.stringtable().s(pg.ways(w).vals(k)).c_str();
                                if (strcmp("island", cvalue) == 0)
                                    realworld_type = OSMElement::Island;
                            } else if (strcmp("natural", ckey) == 0) {
                                const char *cvalue = primblock.stringtable().s(pg.ways(w).vals(k)).c_str();
                                if (strcmp("water", cvalue) == 0)
                                    realworld_type = OSMElement::Water;
                            }
                        }

                        /// If 'ref' string is not empty and 'highway' string is 'primary', 'secondary', or 'tertiary' ...
                        if (buffer_ref[0] != '\0' && buffer_highway[0] != '\0' && (strcmp(buffer_highway, "primary") == 0 || strcmp(buffer_highway, "secondary") == 0 || strcmp(buffer_highway, "tertiary") == 0 || strcmp(buffer_highway, "trunk") == 0 || strcmp(buffer_highway, "motorway") == 0))
                            /// ... assume that this way is part of a national or primary regional road
                            sweden->insertWayAsRoad(wayId, buffer_ref);

                        /// This main thread is the 'producer' of ways,
                        /// pushing ways into a queue. Another thread,
                        /// the consumer, will pop ways, simplify them
                        /// (removing superfluous nodes), and store them
                        /// for searches later.
                        queueWaySimplification.push(new OSMWay(pg.ways(w)));
                        /// Keep track of queue size for statistical purposes
                        ++queueWaySimplificationSize;
                        if (queueWaySimplificationSize > max_queue_size) max_queue_size = queueWaySimplificationSize;

                        if (pg.ways(w).refs_size() > 3 && buffer_ref[0] == '\0' && buffer_highway[0] != '\0' && (strcmp(buffer_highway, "primary") == 0 || strcmp(buffer_highway, "secondary") == 0 || strcmp(buffer_highway, "tertiary") == 0 || strcmp(buffer_highway, "trunk") == 0 || strcmp(buffer_highway, "motorway") == 0))
                            roadsWithoutRef.push_back(std::make_pair(wayId, std::string(buffer_highway)));

                        /// Consider only names of length 2 or longer
                        if (name.length() > 1) {
                            const OSMElement element(wayId, OSMElement::Way, realworld_type);
                            const bool result = swedishTextTree->insert(name, element);
                            if (!result)
                                Error::warn("Cannot insert %s", name.c_str());
                            wayNames->insert(wayId, WriteableString(name));
                        }
                    }
                }

                if (pg.relations_size() > 0) {
                    found_items = true;

                    const int maxrelations = pg.relations_size();
                    for (int i = 0; i < maxrelations; ++i) {
                        const uint64_t relId = pg.relations(i).id();
                        OSMElement::RealWorldType realworld_type = OSMElement::UnknownRealWorldType;
                        std::string name, boundary;
                        int admin_level = 0;
                        const int maxkv = pg.relations(i).keys_size();
                        for (int k = 0; k < maxkv; ++k) {
                            const char *ckey = primblock.stringtable().s(pg.relations(i).keys(k)).c_str();
                            if (strcmp("name", ckey) == 0) {
                                /// Store 'name' string for later use
                                name = primblock.stringtable().s(pg.relations(i).vals(k));
                                ++count_named_relations;
                            } else if (strcmp("ref:scb", ckey) == 0 || strcmp("ref:se:scb", ckey) == 0) {
                                /// Found SCB reference (two digits for lands, four digits for municipalities
                                const char *s = primblock.stringtable().s(pg.relations(i).vals(k)).c_str();
                                errno = 0;
                                const long int v = strtol(s, NULL, 10);
                                if (errno == 0)
                                    sweden->insertSCBarea(v, relId);
                                else
                                    Error::warn("Cannot convert '%s' to a number", s);
                            } else if (strcmp("ref:nuts:3", ckey) == 0) {
                                /// Found three-digit NUTS reference (SEnnn)
                                const char *s = primblock.stringtable().s(pg.relations(i).vals(k)).c_str();
                                if (s[0] == 'S' && s[1] == 'E' && s[2] >= '0' && s[2] <= '9') {
                                    errno = 0;
                                    const long int v = strtol(s + 2 /** adding 2 to skip 'SE' prefix */, NULL, 10);
                                    if (errno == 0 && v > 0)
                                        sweden->insertNUTS3area(v, relId);
                                    else
                                        Error::warn("Cannot convert '%s' to a number", s + 2);
                                }
                            } else if (strcmp("boundary", ckey) == 0) {
                                /// Store 'boundary' string for later use
                                boundary = primblock.stringtable().s(pg.relations(i).vals(k));
                            } else if (strcmp("admin_level", ckey) == 0) {
                                /// Store 'admin_level' string for later use
                                std::stringstream ss(primblock.stringtable().s(pg.relations(i).vals(k)));
                                ss >> admin_level;
                            } else if (strcmp("building", ckey) == 0)
                                /// Remember if way is a building
                                realworld_type = OSMElement::Building;
                            else if (strcmp("place", ckey) == 0) {
                                const char *cvalue = primblock.stringtable().s(pg.relations(i).vals(k)).c_str();
                                if (strcmp("island", cvalue) == 0)
                                    realworld_type = OSMElement::Island;
                            } else if (strcmp("natural", ckey) == 0) {
                                const char *cvalue = primblock.stringtable().s(pg.relations(i).vals(k)).c_str();
                                if (strcmp("water", cvalue) == 0)
                                    realworld_type = OSMElement::Water;
                            }
                            // TODO cover different types of relations to set 'realworld_type' properly
                        }

                        if (admin_level > 0 && name.length() > 1 && (boundary.compare("administrative") == 0 || boundary.compare("historic") == 0))
                            sweden->insertAdministrativeRegion(name, admin_level, relId);

                        RelationMem rm(pg.relations(i).memids_size());
                        uint64_t memId = 0;
                        for (int k = 0; k < pg.relations(i).memids_size(); ++k) {
                            memId += pg.relations(i).memids(k);
                            uint16_t flags = 0;
                            if (strcmp("outer", primblock.stringtable().s(pg.relations(i).roles_sid(k)).c_str()) == 0)
                                flags |= RelationFlags::RoleOuter;
                            else if (strcmp("inner", primblock.stringtable().s(pg.relations(i).roles_sid(k)).c_str()) == 0)
                                flags |= RelationFlags::RoleInner;
                            OSMElement::ElementType type = OSMElement::UnknownElementType;
                            if (pg.relations(i).types(k) == 0)
                                type = OSMElement::Node;
                            else if (pg.relations(i).types(k) == 1)
                                type = OSMElement::Way;
                            else if (pg.relations(i).types(k) == 2)
                                type = OSMElement::Relation;
                            else
                                Error::warn("Unknown relation type for member %llu in relation %llu : type=%d", memId, relId, pg.relations(i).types(k));
                            rm.members[k] = OSMElement(memId, type, OSMElement::UnknownRealWorldType);
                            rm.member_flags[k] = flags;
                        }
                        relMembers->insert(relId, rm);

                        /// Consider only names of length 2 or longer
                        if (name.length() > 1) {
                            const OSMElement element(relId, OSMElement::Relation, realworld_type);
                            const bool result = swedishTextTree->insert(name, element);
                            if (!result)
                                Error::warn("Cannot insert %s", name.c_str());
                            relationNames->insert(relId, WriteableString(name));
                        }
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

    /// Line break after series of dots
    if (isatty(1))
        std::cout << std::endl;

    Timer joinTimer;
    int64_t wallTime, cpuTime;
    doneWaySimplification = true;
    Error::debug("Waiting for way simplification thread, max queue length was %d", max_queue_size);
    waySimplificationThread.join();
    joinTimer.elapsed(&cpuTime, &wallTime);
    Error::debug("Time to join: cpu= %.3fms   wall= %.3fms", cpuTime / 1000.0, wallTime / 1000.0);

    Error::info("Number of named nodes: %d", count_named_nodes);
    Error::info("Number of named nodes: %d", count_named_ways);
    Error::info("Number of named relations: %d", count_named_relations);
    Error::info("Number of named elements (sum): %d", count_named_nodes + count_named_ways + count_named_relations);

    return true;
}
