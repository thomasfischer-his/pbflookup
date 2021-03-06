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
#include "helper.h"

#define SHORT_STRING_BUFFER_SIZE     64

/// Data structure for producer-consumer threads
/// which will simplify ways before storing them
struct OSMWay {
    OSMWay(const ::OSMPBF::Way &way, uint64_t id_offset)
        : id(way.id() + id_offset), size(way.refs_size()) {
        nodes = (uint64_t *)malloc(size * sizeof(uint64_t));

        uint64_t node_id = 0;
        for (size_t i = 0; i < size; ++i) {
            node_id += way.refs(i);
            nodes[i] = node_id + id_offset;
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
static const size_t queueWaySimplification_recommendedSize = 1 << 12;
boost::lockfree::queue<const OSMWay *> queueWaySimplification(queueWaySimplification_recommendedSize);
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
            const int x = coordA.x + (int)(t * (coordB.x - coordA.x) + 0.5);
            const int y = coordA.y + (int)(t * (coordB.y - coordA.y) + 0.5);
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
    if (way.size < 2) {
        /// Rare but exists in map: a node with only one node
        /// -> ignore those artefacts
        Error::warn("Way %llu has only %d nodes", way.id, way.size);
        return;
    }

    static const size_t simplifiedWay_size = 8192; /// largest observed way length is 2304
    static uint64_t simplifiedWay[simplifiedWay_size];
    if (way.size > simplifiedWay_size)
        Error::err("Way %llu of size %d is longer than acceptable (%d)", way.id, way.size, simplifiedWay_size);

    const size_t simplifiedWaySize = applyRamerDouglasPeucker(way, simplifiedWay);
    if (simplifiedWaySize < 2) {
        Error::warn("Way %llu got simplified to only %d nodes", way.id, simplifiedWay);
        return;
    }

    WayNodes wn(simplifiedWaySize);
    memcpy(wn.nodes, simplifiedWay, sizeof(uint64_t) * wn.num_nodes);
    for (size_t k = 0; k < simplifiedWaySize; ++k)
        node2Coord->increaseCounter((simplifiedWay)[k]);
    wayNodes->insert(way.id, wn);
}

/// This method will be run by the consumer thread which is
/// processing (simplifying) ways
void consumerWaySimplification(void) {
#ifdef CPUTIMER
    Timer consumerThreadTimer;
#endif // CPUTIMER

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

#ifdef CPUTIMER
    int64_t cpuTime, wallTime;
    consumerThreadTimer.elapsed(&cpuTime, &wallTime);
    Error::debug("Time spent in consumer thread: cpu= %.3fms   wall= %.3fms", cpuTime / 1000.0, wallTime / 1000.0);
#endif // CPUTIMER
}

/**
 * Insert the name(s) of an element into its data structures
 * after performing some sanitation and validity checking.
 *
 * @param id node, way, or relation id (determined by element_type)
 * @param element_type element type to notify if name belongs to a node, way, or relation
 * @param realworld_type real-world type of element to process
 * @param name_set a collection of key-value pairs of names such as "name:de=Oskars Schleussen"
 */
void insertNames(uint64_t id, OSMElement::ElementType element_type, OSMElement::RealWorldType realworld_type, const std::map<std::string, std::string> &name_set) {
    bool first_name = true;
    std::string best_name; ///< if multiple names are available, record the 'best' name
    const OSMElement element(id, element_type, realworld_type);
    std::set<std::string> known_names; ///< track names to avoid duplicate insertions
    static const std::set<std::string> ignored_country_codes = {"ab", "ace", "af", "ak", "als", "am", "an", "ang", "ar", "arc", "arz", "ast", "ay", "az", "ba", "bar", "bat-smg", "bcl", "be", "be-tarask", "bg", "bi", "bm", "bn", "bo", "bpy", "br", "bs", "bxr", "ca", "cdo", "ce", "ceb", "chr", "chy", "ckb", "co", "crh", "cs", "csb", "cu", "cv", "cy", "da", "de", "diq", "dsb", "dv", "dz", "ee", "el", "en", "eo", "es", "et", "eu", "ext", "fa", "ff", "fi", "fiu-vro", "fo", "fr", "frp", "frr", "fur", "fy", "ga", "gag", "gan", "gd", "gl", "gn", "gu", "gv", "ha", "hak", "haw", "he", "hi", "hif", "hr", "hsb", "ht", "hu", "hy", "ia", "id", "ie", "ig", "ilo", "io", "is", "it", "iu", "ja", "jbo", "jv", "ka", "kaa", "kab", "kbd", "kg", "ki", "kk", "kl", "km", "kn", "ko", "koi", "krc", "ks", "ksh", "ku", "kv", "kw", "ky", "la", "lad", "lb", "lez", "lg", "li", "lij", "lmo", "ln", "lo", "lt", "ltg", "lv", "mdf", "mg", "mhr", "mi", "mk", "ml", "mn", "mr", "mrj", "ms", "mt", "my", "myv", "mzn", "na", "nah", "nan", "nap", "nb", "nds", "nds-nl", "ne", "new", "nl", "nn", "no", "nov", "nrm", "nv", "oc", "om", "or", "os", "pa", "pag", "pam", "pap", "pcd", "pdc", "pih", "pl", "pms", "pnb", "pnt", "ps", "pt", "qu", "rm", "rmy", "rn", "ro", "roa-rup", "roa-tara", "ru", "rue", "rw", "sa", "sah", "sc", "scn", "sco", "se", "sg", "sh", "si", "simple", "sk", "sl", "sm", "sme", "sn", "so", "sq", "sr", "sr-Latn", "srn", "ss", "st", "stq", "su", "sw", "szl", "ta", "te", "tet", "tg", "th", "ti", "tk", "tl", "to", "tpi", "tr", "ts", "tt", "tw", "tzl", "udm", "ug", "uk", "ur", "uz", "vec", "vep", "vi", "vls", "vo", "wa", "war", "wo", "wuu", "xal", "xmf", "yi", "yo", "yue", "za", "zea", "zh", "zh-classical", "zh-min-nan", "zh_pinyin", "zh_py", "zh_pyt", "zh-simplified", "zh-yue", "zu"};

    for (auto name_pair : name_set) {
        auto const &name_key = name_pair.first;
        auto const &name_value = name_pair.second;
        /// Consider only names of length 2 or longer
        if (name_value.length() < 2) continue;

        if (first_name && element_type == OSMElement::Node) {
            node2Coord->increaseCounter(id);
            first_name = false;
        }

        if (name_key.find(":") != std::string::npos) {
            /// Name has components, split and check
            std::vector<std::string> name_key_components;
            split(name_key, ':', name_key_components);
            if (name_key_components.size() >= 2) {
                const auto components_back(name_key_components.back());
                if (ignored_country_codes.find(components_back) != ignored_country_codes.cend()) {
                    /// This is a language-specific name, such as 'name:en', but not 'name:sv' (Swedish), so skip it
                    continue;
                }
            }
        }

        /// Only unique names
        if (known_names.find(name_value) != known_names.cend())
            continue;
        else
            known_names.insert(name_value);

        if (best_name.empty())
            best_name = name_value;
        else if (name_key == "name")
            best_name = name_value;

        const bool result = swedishTextTree->insert(name_value, element);
        if (!result)
            Error::warn("Cannot insert %s=%s for id=%llu", name_key.c_str(), name_value.c_str(), id);
    }

    if (!best_name.empty()) {
        bool result = false;
        switch (element_type) {
        case OSMElement::Node: result = nodeNames->insert(id, WriteableString(best_name)); break;
        case OSMElement::Way: result = wayNames->insert(id, WriteableString(best_name)); break;
        case OSMElement::Relation: result = relationNames->insert(id, WriteableString(best_name)); break;
        case OSMElement::UnknownElementType: break;
        }
        if (!result)
            Error::warn("Cannot insert name %s for %s", best_name.c_str(), element.operator std::string().c_str());
    }
}

inline uint64_t record_max_id(const uint64_t current_id, uint64_t &variable_for_max) {
    if (current_id > variable_for_max)
        variable_for_max = current_id;
    return current_id;
}

OsmPbfReader::OsmPbfReader()
    : id_offset(0)
{
    swedishTextTree = nullptr;
    node2Coord = nullptr;
    nodeNames = nullptr;
    wayNames = nullptr;
    relationNames = nullptr;
    wayNodes = nullptr;
    relMembers = nullptr;
    sweden = nullptr;

    buffer = new char[OSMPBF::max_uncompressed_blob_size];
    if (buffer == nullptr)
        Error::err("Could not allocate memory for OsmPbfReader::buffer");
    unpack_buffer = new char[OSMPBF::max_uncompressed_blob_size];
    if (unpack_buffer == nullptr)
        Error::err("Could not allocate memory for OsmPbfReader::unpack_buffer");
}

OsmPbfReader::~OsmPbfReader()
{
    delete[] buffer;
    delete[] unpack_buffer;
}

bool OsmPbfReader::parse(std::istream &input, bool allow_overlapping_ids) {
    std::vector<std::pair<uint64_t, std::string> > roadsWithoutRef;
    size_t count_named_nodes = 0, count_named_ways = 0, count_named_relations = 0;
    uint64_t largest_observed_id = 0;

    if (!input || !input.good())
        return false;

    if (swedishTextTree == nullptr)
        swedishTextTree = new SwedishTextTree();
    if (swedishTextTree == nullptr)
        Error::err("Could not allocate memory for swedishTextTree");
    if (node2Coord == nullptr)
        node2Coord = new IdTree<Coord>();
    if (node2Coord == nullptr)
        Error::err("Could not allocate memory for node2Coord");
    if (nodeNames == nullptr)
        nodeNames = new IdTree<WriteableString>();
    if (nodeNames == nullptr)
        Error::err("Could not allocate memory for nodeNames");
    if (wayNames == nullptr)
        wayNames = new IdTree<WriteableString>();
    if (wayNames == nullptr)
        Error::err("Could not allocate memory for wayNames");
    if (relationNames == nullptr)
        relationNames = new IdTree<WriteableString>();
    if (relationNames == nullptr)
        Error::err("Could not allocate memory for relationNames");
    if (wayNodes == nullptr)
        wayNodes = new IdTree<WayNodes>();
    if (wayNodes == nullptr)
        Error::err("Could not allocate memory for wayNodes");
    if (relMembers == nullptr)
        relMembers = new IdTree<RelationMem>();
    if (relMembers == nullptr)
        Error::err("Could not allocate memory for relmem");
    if (sweden == nullptr)
        sweden = new Sweden();
    if (sweden == nullptr)
        Error::err("Could not allocate memory for Sweden");

    doneWaySimplification = false;
    boost::thread waySimplificationThread(consumerWaySimplification);
    size_t max_queue_size = 0;

#ifdef CPUTIMER
    Timer primitiveGroupTimer;
    int64_t accumulatedPrimitiveGroupTime = 0;
#endif // CPUTIMER

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

#ifdef CPUTIMER
                primitiveGroupTimer.start();
#endif // CPUTIMER
                if (pg.nodes_size() > 0) {
                    found_items = true;

                    const int maxnodes = pg.nodes_size();
                    for (int j = 0; j < maxnodes; ++j) {
                        const uint64_t id = record_max_id(pg.nodes(j).id(), largest_observed_id);
                        OSMElement::RealWorldType realworld_type = OSMElement::UnknownRealWorldType;
                        /// Track various names like 'name', 'name:en', or 'name:bridge:dk'
                        std::map<std::string, std::string> name_set;

                        const double lat = coord_scale * (primblock.lat_offset() + (primblock.granularity() * pg.nodes(j).lat()));
                        const double lon = coord_scale * (primblock.lon_offset() + (primblock.granularity() * pg.nodes(j).lon()));
                        node2Coord->insert(id + (allow_overlapping_ids ? 0 : id_offset), Coord::fromLonLat(lon, lat));

                        bool node_is_county = false, node_is_municipality = false, node_is_traffic_sign = false;
                        for (int k = 0; k < pg.nodes(j).keys_size(); ++k) {
                            const char *ckey = primblock.stringtable().s(pg.nodes(j).keys(k)).c_str();
                            if (strcmp("name", ckey) == 0) {
                                /// Store 'name' string for later use
                                name_set.insert(make_pair(primblock.stringtable().s(pg.nodes(j).keys(k)), primblock.stringtable().s(pg.nodes(j).vals(k))));
                                ++count_named_nodes;
                            } else if (strncmp("name:", ckey, 5) == 0 || strcmp("alt_name", ckey) == 0 || strncmp("alt_name:", ckey, 9) == 0 || strcmp("old_name", ckey) == 0 || strncmp("old_name:", ckey, 9) == 0 || strcmp("loc_name", ckey) == 0 || strncmp("loc_name:", ckey, 9) == 0 || strcmp("short_name", ckey) == 0 || strncmp("short_name:", ckey, 11) == 0 || strcmp("official_name", ckey) == 0 || strncmp("official_name:", ckey, 14) == 0) {
                                /// Store name string for later use
                                name_set.insert(make_pair(primblock.stringtable().s(pg.nodes(j).keys(k)), primblock.stringtable().s(pg.nodes(j).vals(k))));
                            } else if (strcmp("place", ckey) == 0) {
                                const char *cvalue = primblock.stringtable().s(pg.nodes(j).vals(k)).c_str();

                                if (strcmp("county", cvalue) == 0) {
                                    /// FIX OSM DATA
                                    /// Counties should not be represented by nodes, but by relations
                                    /// representing an area
                                    // realworld_type = OSMElement::PlaceLargeArea;
                                    node_is_county = true;
                                } else if (strcmp("municipality", cvalue) == 0) {
                                    /// FIX OSM DATA
                                    /// Counties should not be represented by nodes, but by relations
                                    /// representing an area
                                    // realworld_type = OSMElement::PlaceLargeArea;
                                    node_is_municipality = true;
                                } else if (strcmp("traffic_sign", cvalue) == 0) {
                                    /// Traffic signs may simply point to a location, but not be *at* this location.
                                    /// Thus, their names (if set) may be misleading and so they should be ignored.
                                    node_is_traffic_sign = true;
                                }

                                if (strcmp("city", cvalue) == 0 || strcmp("municipality", cvalue) == 0)
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

                        if (node_is_municipality)
                            Error::info("Municipality '%s' is represented by node %llu, not recoding node's name", name_set["name"].c_str(), id + (allow_overlapping_ids ? 0 : id_offset));
                        else if (node_is_county)
                            Error::info("County '%s' is represented by node %llu, not recoding node's name", name_set["name"].c_str(), id + (allow_overlapping_ids ? 0 : id_offset));
                        else if (node_is_traffic_sign)
                            Error::info("Node %llu with name '%s' is a traffic sign, not recoding node's name", id + (allow_overlapping_ids ? 0 : id_offset), name_set["name"].c_str());
                        else if (!name_set.empty() /** implicitly: not node_is_municipality and not node_is_county and not node_is_traffic_sign */)
                            insertNames(id + (allow_overlapping_ids ? 0 : id_offset), OSMElement::Node, realworld_type, name_set);
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
                        /// Track various names like 'name', 'name:en', or 'name:bridge:dk'
                        std::map<std::string, std::string> name_set;

                        last_id += pg.dense().id(j);
                        record_max_id(last_id, largest_observed_id);
                        last_lat += coord_scale * (primblock.lat_offset() + (primblock.granularity() * pg.dense().lat(j)));
                        last_lon += coord_scale * (primblock.lon_offset() + (primblock.granularity() * pg.dense().lon(j)));
                        node2Coord->insert(last_id + (allow_overlapping_ids ? 0 : id_offset), Coord::fromLonLat(last_lon, last_lat));

                        bool isKey = true;
                        int key = 0, value = 0;
                        bool node_is_county = false, node_is_municipality = false, node_is_traffic_sign = false;
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
                                    name_set.insert(make_pair(primblock.stringtable().s(key), primblock.stringtable().s(value)));
                                    ++count_named_nodes;
                                } else if (strncmp("name:", ckey, 5) == 0 || strcmp("alt_name", ckey) == 0 || strncmp("alt_name:", ckey, 9) == 0 || strcmp("old_name", ckey) == 0 || strncmp("old_name:", ckey, 9) == 0 || strcmp("loc_name", ckey) == 0 || strncmp("loc_name:", ckey, 9) == 0 || strcmp("short_name", ckey) == 0 || strncmp("short_name:", ckey, 11) == 0 || strcmp("official_name", ckey) == 0 || strncmp("official_name:", ckey, 14) == 0) {
                                    /// Store name string for later use
                                    name_set.insert(make_pair(primblock.stringtable().s(key), primblock.stringtable().s(value)));
                                } else if (strcmp("place", ckey) == 0) {
                                    const char *cvalue = primblock.stringtable().s(value).c_str();

                                    if (strcmp("county", cvalue) == 0) {
                                        /// FIX OSM DATA
                                        /// Counties should not be represented by nodes, but by relations
                                        /// representing an area
                                        // realworld_type = OSMElement::PlaceLargeArea;
                                        node_is_county = true;
                                    } else if (strcmp("municipality", cvalue) == 0) {
                                        /// FIX OSM DATA
                                        /// Counties should not be represented by nodes, but by relations
                                        /// representing an area
                                        // realworld_type = OSMElement::PlaceLargeArea;
                                        node_is_municipality = true;
                                    } else if (strcmp("traffic_sign", cvalue) == 0) {
                                        /// Traffic signs may simply point to a location, but not be *at* this location.
                                        /// Thus, their names (if set) may be misleading and so they should be ignored.
                                        node_is_traffic_sign = true;
                                    }

                                    if (strcmp("city", cvalue) == 0 || strcmp("municipality", cvalue) == 0)
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

                        if (node_is_municipality)
                            Error::info("Municipality '%s' is represented by node %llu, not recoding node's name", name_set["name"].c_str(), last_id + (allow_overlapping_ids ? 0 : id_offset));
                        else if (node_is_county)
                            Error::info("County '%s' is represented by node %llu, not recoding node's name", name_set["name"].c_str(), last_id + (allow_overlapping_ids ? 0 : id_offset));
                        else if (node_is_traffic_sign)
                            Error::info("Node %llu with name '%s' is a traffic sign, not recoding node's name", last_id + (allow_overlapping_ids ? 0 : id_offset), name_set["name"].c_str());
                        else if (!name_set.empty() /** implicitly: not node_is_municipality and not node_is_county and not node_is_traffic_sign */)
                            insertNames(last_id + (allow_overlapping_ids ? 0 : id_offset), OSMElement::Node, realworld_type, name_set);
                    }
                }

                if (pg.ways_size() > 0) {
                    found_items = true;

                    char buffer_ref[SHORT_STRING_BUFFER_SIZE], buffer_highway[SHORT_STRING_BUFFER_SIZE];
                    const int maxways = pg.ways_size();
                    for (int w = 0; w < maxways; ++w) {
                        const uint64_t wayId = record_max_id(pg.ways(w).id(), largest_observed_id);
                        const int way_size = pg.ways(w).refs_size();

                        if (way_size < 2) {
                            /// Rare but exists in map: a node with only one node (or no node?)
                            /// -> ignore those artefacts
                            Error::warn("Way %llu has only %d node(s)", wayId + (allow_overlapping_ids ? 0 : id_offset), way_size);
                            continue;
                        }

                        OSMElement::RealWorldType realworld_type = OSMElement::UnknownRealWorldType;
                        /// Track various names like 'name', 'name:en', or 'name:bridge:dk'
                        std::map<std::string, std::string> name_set;

                        buffer_ref[0] = buffer_highway[0] = '\0'; /// clear buffers
                        for (int k = 0; k < pg.ways(w).keys_size(); ++k) {
                            const char *ckey = primblock.stringtable().s(pg.ways(w).keys(k)).c_str();
                            if (strcmp("name", ckey) == 0) {
                                /// Store 'name' string for later use
                                name_set.insert(make_pair(primblock.stringtable().s(pg.ways(w).keys(k)), primblock.stringtable().s(pg.ways(w).vals(k))));
                                ++count_named_nodes;
                            } else if (strncmp("name:", ckey, 5) == 0 || strcmp("alt_name", ckey) == 0 || strncmp("alt_name:", ckey, 9) == 0 || strcmp("old_name", ckey) == 0 || strncmp("old_name:", ckey, 9) == 0 || strcmp("loc_name", ckey) == 0 || strncmp("loc_name:", ckey, 9) == 0 || strcmp("short_name", ckey) == 0 || strncmp("short_name:", ckey, 11) == 0 || strcmp("official_name", ckey) == 0 || strncmp("official_name:", ckey, 14) == 0) {
                                /// Store name string for later use
                                name_set.insert(make_pair(primblock.stringtable().s(pg.ways(w).keys(k)), primblock.stringtable().s(pg.ways(w).vals(k))));
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
                            sweden->insertWayAsRoad(wayId + (allow_overlapping_ids ? 0 : id_offset), buffer_ref);

                        /// This main thread is the 'producer' of ways,
                        /// pushing ways into a queue. Another thread,
                        /// the consumer, will pop ways, simplify them
                        /// (removing superfluous nodes), and store them
                        /// for searches later.
                        queueWaySimplification.push(new OSMWay(pg.ways(w), allow_overlapping_ids ? 0 : id_offset));
                        /// Keep track of queue size for statistical purposes
                        ++queueWaySimplificationSize;
                        if (queueWaySimplificationSize > max_queue_size) max_queue_size = queueWaySimplificationSize;
                        if (queueWaySimplificationSize > queueWaySimplification_recommendedSize - 16) {
                            /// Give consumer thread time to catch up
                            boost::this_thread::sleep(boost::posix_time::milliseconds(100));
                        }

                        if (way_size > 3 && buffer_ref[0] == '\0' && buffer_highway[0] != '\0' && (strcmp(buffer_highway, "primary") == 0 || strcmp(buffer_highway, "secondary") == 0 || strcmp(buffer_highway, "tertiary") == 0 || strcmp(buffer_highway, "trunk") == 0 || strcmp(buffer_highway, "motorway") == 0))
                            roadsWithoutRef.push_back(std::make_pair(wayId + (allow_overlapping_ids ? 0 : id_offset), std::string(buffer_highway)));

                        if (!name_set.empty())
                            insertNames(wayId + (allow_overlapping_ids ? 0 : id_offset), OSMElement::Way, realworld_type, name_set);
                    }
                }

                if (pg.relations_size() > 0) {
                    found_items = true;

                    const int maxrelations = pg.relations_size();
                    for (int i = 0; i < maxrelations; ++i) {
                        const uint64_t relId = record_max_id(pg.relations(i).id(), largest_observed_id);

                        /// Some relations should be ignored, e.g. for roads outside of Sweden
                        /// which just happend to be included in the map data
                        /// To sort:  echo '3, 1, 2' | sed -e 's/ //g' | tr ',' '\n' | sort -u -n | tr '\n' ',' | sed -e 's/,/, /g'
                        static const uint64_t blacklistedRelIds[] = {2545969, 3189514, 5518156, 5756777, 5794315, 5794316, 0};
                        /// To count: echo '3, 1, 2' | sed -e 's/ //g' | tr ',' '\n' | wc -l
                        static const size_t blacklistedRelIds_count = 6;
                        if (inSortedArray(blacklistedRelIds, blacklistedRelIds_count, relId + (allow_overlapping_ids ? 0 : id_offset))) continue;

                        OSMElement::RealWorldType realworld_type = OSMElement::UnknownRealWorldType;
                        /// Track various names like 'name', 'name:en', or 'name:bridge:dk'
                        std::map<std::string, std::string> name_set;
                        std::string type, route, boundary;
                        int admin_level = 0;
                        const int maxkv = pg.relations(i).keys_size();
                        for (int k = 0; k < maxkv; ++k) {
                            const char *ckey = primblock.stringtable().s(pg.relations(i).keys(k)).c_str();
                            if (strcmp("name", ckey) == 0) {
                                /// Store 'name' string for later use
                                name_set.insert(make_pair(primblock.stringtable().s(pg.relations(i).keys(k)), primblock.stringtable().s(pg.relations(i).vals(k))));
                                ++count_named_nodes;
                            } else if (strncmp("name:", ckey, 5) == 0 || strcmp("alt_name", ckey) == 0 || strncmp("alt_name:", ckey, 9) == 0 || strcmp("old_name", ckey) == 0 || strncmp("old_name:", ckey, 9) == 0 || strcmp("loc_name", ckey) == 0 || strncmp("loc_name:", ckey, 9) == 0 || strcmp("short_name", ckey) == 0 || strncmp("short_name:", ckey, 11) == 0 || strcmp("official_name", ckey) == 0 || strncmp("official_name:", ckey, 14) == 0) {
                                /// Store name string for later use
                                name_set.insert(make_pair(primblock.stringtable().s(pg.relations(i).keys(k)), primblock.stringtable().s(pg.relations(i).vals(k))));
                            } else if (strcmp("type", ckey) == 0) {
                                /// Store 'type' string for later use
                                type = primblock.stringtable().s(pg.relations(i).vals(k));
                            } else if (strcmp("route", ckey) == 0) {
                                /// Store 'route' string for later use
                                route = primblock.stringtable().s(pg.relations(i).vals(k));
                            } else if (strcmp("ref:scb", ckey) == 0 || strcmp("ref:se:scb", ckey) == 0) {
                                /// Found SCB reference (two digits for lands, four digits for municipalities
                                const char *s = primblock.stringtable().s(pg.relations(i).vals(k)).c_str();
                                errno = 0;
                                const long int v = strtol(s, NULL, 10);
                                if (errno == 0)
                                    sweden->insertSCBarea(v, relId + (allow_overlapping_ids ? 0 : id_offset));
                                else
                                    Error::warn("Cannot convert '%s' to a number", s);
                            } else if (strcmp("ref:nuts:3", ckey) == 0) {
                                /// Found three-digit NUTS reference (SEnnn)
                                const char *s = primblock.stringtable().s(pg.relations(i).vals(k)).c_str();
                                if (s[0] == 'S' && s[1] == 'E' && s[2] >= '0' && s[2] <= '9') {
                                    errno = 0;
                                    const long int v = strtol(s + 2 /** adding 2 to skip 'SE' prefix */, NULL, 10);
                                    if (errno == 0 && v > 0)
                                        sweden->insertNUTS3area(v, relId + (allow_overlapping_ids ? 0 : id_offset));
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

                        if (realworld_type == OSMElement::UnknownRealWorldType && type.compare("route") == 0 && route.compare("road") == 0)
                            realworld_type = OSMElement::RoadMajor;
                        else if (realworld_type == OSMElement::UnknownRealWorldType && boundary.compare("administrative") == 0)
                            realworld_type = OSMElement::PlaceLargeArea;

                        const auto name(name_set["name"]);
                        if (admin_level > 0 && name.length() > 1 && (boundary.compare("administrative") == 0 || boundary.compare("historic") == 0))
                            sweden->insertAdministrativeRegion(name, admin_level, relId + (allow_overlapping_ids ? 0 : id_offset));

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
                                Error::warn("Unknown relation type for member %llu in relation %llu : type=%d", memId, relId + (allow_overlapping_ids ? 0 : id_offset), pg.relations(i).types(k));
                            rm.members[k] = OSMElement(memId, type, OSMElement::UnknownRealWorldType);
                            rm.member_flags[k] = flags;
                        }
                        relMembers->insert(relId + (allow_overlapping_ids ? 0 : id_offset), rm);

                        if (!name_set.empty())
                            insertNames(relId + (allow_overlapping_ids ? 0 : id_offset), OSMElement::Relation, realworld_type, name_set);
                    }
                }

                if (!found_items) {
                    Error::warn("      contains no items");
                }
            }

#ifdef CPUTIMER
            int64_t cpuTime;
            primitiveGroupTimer.elapsed(&cpuTime);
            accumulatedPrimitiveGroupTime += cpuTime;
#endif // CPUTIMER
        }

        else {
            // unknown blob type
            Error::warn("  unknown blob type: %s", blobheader.type().c_str());
        }
    }

    /// Line break after series of dots
    if (isatty(1))
        std::cout << std::endl;

#ifdef CPUTIMER
    Error::debug("Time to process primitive groups: cpu= %.3fms", accumulatedPrimitiveGroupTime / 1000.0);
#endif // CPUTIMER

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

    if (!allow_overlapping_ids) {
        /// Value of 'largest_observed_id' is without any previous id_offset applied
        id_offset += largest_observed_id + 1;
        Error::debug("Setting id_offset = %llu", id_offset);
    }
    return true;
}
