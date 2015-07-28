#include "osmpbfreader.h"

/// Zlib compression is used inside the pbf blobs
#include <zlib.h>

/// netinet or winsock2 provides the network-byte-order conversion function
#ifdef D_HAVE_WINSOCK
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#include "swedishtexttree.h"
#include "idtree.h"
#include "error.h"

OsmPbfReader::OsmPbfReader()
{
    buffer = new char[OSMPBF::max_uncompressed_blob_size];
    unpack_buffer = new char[OSMPBF::max_uncompressed_blob_size];
}

OsmPbfReader::~OsmPbfReader()
{
    delete[] buffer;
    delete[] unpack_buffer;
}

bool OsmPbfReader::parse(std::istream &input, SwedishText::Tree **swedishTextTree, IdTree<Coord> **n2c, IdTree<WayNodes> **w2n, IdTree<RelationMem> **relmem) {
    *swedishTextTree = NULL;
    *n2c = NULL;
    *w2n = NULL;
    *relmem = NULL;

    if (!input || !input.good())
        return false;

    *swedishTextTree = new SwedishText::Tree();
    *n2c = new IdTree<Coord>();
    *w2n = new IdTree<WayNodes>();
    *relmem = new IdTree<RelationMem>();

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

        // tell about the blob-header
        //info("\nBlobHeader (%d bytes)", sz);
        //debug("  type = %s", blobheader.type().c_str());

        // size of the following blob
        sz = blobheader.datasize();
        //debug("  datasize = %u", sz);

        // optional indexdata
        /*if (blobheader.has_indexdata()) {
            debug("  indexdata = %u bytes", blobheader.indexdata().size());
        }*/

        // ensure the blob is smaller then MAX_BLOB_SIZE
        if (sz > OSMPBF::max_uncompressed_blob_size) {
            Error::err("blob-size is bigger then allowed (%u > %u)", sz, OSMPBF::max_uncompressed_blob_size);
        }

        // read the blob from the file
        input.read(buffer, sz);
        if (input.gcount() != sz || input.fail()) {
            Error::err("unable to read blob from file");
        }

        // parse the blob from the read-buffer
        if (!blob.ParseFromArray(buffer, sz)) {
            Error::err("unable to parse blob");
        }

        // tell about the blob-header
        //info("Blob (%d bytes)", sz);

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

            // tell about the compressed data
            //debug("  contains zlib-compressed data: %u bytes", sz);
            //debug("  uncompressed size: %u bytes", blob.raw_size());

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

            // tell about the compressed data
            //debug("  contains lzma-compressed data: %u bytes", blob.lzma_data().size());
            //debug("  uncompressed size: %u bytes", blob.raw_size());

            // issue a warning, lzma compression is not yet supported
            Error::err("  lzma-decompression is not supported");
        }

        // check we have at least one data-stream
        if (!found_data) {
            Error::err("  does not contain any known data stream");
        }

        // switch between different blob-types
        if (blobheader.type() == "OSMHeader") {
            // tell about the OSMHeader blob
            //info("  OSMHeader");

            // parse the HeaderBlock from the blob
            if (!headerblock.ParseFromArray(unpack_buffer, sz)) {
                Error::err("unable to parse header block");
            }

            // tell about the bbox
            if (headerblock.has_bbox()) {
                OSMPBF::HeaderBBox bbox = headerblock.bbox();
                /*debug("    bbox: %.7f,%.7f,%.7f,%.7f",
                    (double)bbox.left() / OSMPBF::lonlat_resolution,
                    (double)bbox.bottom() / OSMPBF::lonlat_resolution,
                    (double)bbox.right() / OSMPBF::lonlat_resolution,
                    (double)bbox.top() / OSMPBF::lonlat_resolution);*/
            }

            // tell about the required features
            /*for (int i = 0, l = headerblock.required_features_size(); i < l; i++) {
                debug("    required_feature: %s", headerblock.required_features(i).c_str());
            }*/

            // tell about the optional features
            /*for (int i = 0, l = headerblock.optional_features_size(); i < l; i++) {
                debug("    optional_feature: %s", headerblock.optional_features(i).c_str());
            }*/

            // tell about the writing program
            /*if (headerblock.has_writingprogram()) {
                debug("    writingprogram: %s", headerblock.writingprogram().c_str());
            }*/

            // tell about the source
            /*if (headerblock.has_source()) {
                debug("    source: %s", headerblock.source().c_str());
            }*/
        } else if (blobheader.type() == "OSMData") {
            // tell about the OSMData blob
            //info("  OSMData");

            // parse the PrimitiveBlock from the blob
            if (!primblock.ParseFromArray(unpack_buffer, sz)) {
                Error::err("unable to parse primitive block");
            }

            // tell about the block's meta info
            /*debug("    granularity: %u", primblock.granularity());
            debug("    lat_offset: %u", primblock.lat_offset());
            debug("    lon_offset: %u", primblock.lon_offset());
            debug("    date_granularity: %u", primblock.date_granularity());*/

            // tell about the stringtable
            //debug("    stringtable: %u items", primblock.stringtable().s_size());
            /*const int maxstring = primblock.stringtable().s_size() > list_limit ? list_limit : primblock.stringtable().s_size();
            for (int i = 0; i < maxstring; ++i) {
                debug("      string %d = '%s'", i, primblock.stringtable().s(i).c_str());
            }*/

            // number of PrimitiveGroups
            //debug("    primitivegroups: %u groups", primblock.primitivegroup_size());

            // iterate over all PrimitiveGroups
            for (int i = 0, l = primblock.primitivegroup_size(); i < l; i++) {
                // one PrimitiveGroup from the the Block
                OSMPBF::PrimitiveGroup pg = primblock.primitivegroup(i);

                bool found_items = false;
                const double coord_scale = 0.000000001;

                // tell about nodes
                if (pg.nodes_size() > 0) {
                    found_items = true;

                    //debug("      nodes: %d", pg.nodes_size());
                    /*if (pg.nodes(0).has_info()) {
                        debug("        with meta-info");
                    }*/

                    const int maxnodes = pg.nodes_size();
                    for (int j = 0; j < maxnodes; ++j) {
                        const double lat = coord_scale * (primblock.lat_offset() + (primblock.granularity() * pg.nodes(j).lat()));
                        const double lon = coord_scale * (primblock.lon_offset() + (primblock.granularity() * pg.nodes(j).lon()));
                        (*n2c)->insert(pg.nodes(j).id(), Coord(lat, lon));

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

                // tell about dense nodes
                if (pg.has_dense()) {
                    found_items = true;

                    //debug("      dense nodes: %d", pg.dense().id_size());
                    /*if (pg.dense().has_denseinfo()) {
                        debug("        with meta-info");
                    }*/

                    uint64_t last_id = 0;
                    int last_keyvals_pos = 0;
                    double last_lat = 0.0, last_lon = 0.0;
                    const int idmax = pg.dense().id_size();
                    for (int j = 0; j < idmax; ++j) {
                        last_id += pg.dense().id(j);
                        last_lat += coord_scale * (primblock.lat_offset() + (primblock.granularity() * pg.dense().lat(j)));
                        last_lon += coord_scale * (primblock.lon_offset() + (primblock.granularity() * pg.dense().lon(j)));
                        (*n2c)->insert(last_id, Coord(last_lat, last_lon));

                        //debug("        dense node %u   at lat=%.6f lon=%.6f", last_id, last_lat, last_lon);

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

                // tell about ways
                if (pg.ways_size() > 0) {
                    found_items = true;

                    //debug("      ways: %d", pg.ways_size());
                    /*if (pg.ways(0).has_info()) {
                        debug("        with meta-info");
                    }*/

                    const int maxways = pg.ways_size();// > list_limit ? list_limit : pg.ways_size();
                    for (int i = 0; i < maxways; ++i) {
                        for (int k = 0; k < pg.ways(i).keys_size(); ++k) {
                            const char *ckey = primblock.stringtable().s(pg.ways(i).keys(k)).c_str();
                            if (strcmp("name", ckey) == 0) {
                                uint64_t id = pg.ways(i).id();
                                const bool result = (*swedishTextTree)->insert(primblock.stringtable().s(pg.ways(i).vals(k)), id << 2 | WAY_NIBBLE);
                                if (!result)
                                    Error::warn("Cannot insert %s", primblock.stringtable().s(pg.ways(i).vals(k)).c_str());
                            }
                        }

                        const uint64_t wayId = pg.ways(i).id();
                        WayNodes wn(pg.ways(i).refs_size());
                        //Error::debug("Adding way %llu, %i nodes", wayId, pg.ways(i).refs_size());
                        uint64_t nodeId = 0;
                        for (int k = 0; k < pg.ways(i).refs_size(); ++k) {
                            nodeId += pg.ways(i).refs(k);
                            //Error::debug("  Adding node %llu at pos %i", nodeId, k);
                            wn.nodes[k] = nodeId;
                        }
                        (*w2n)->insert(wayId, wn);
                    }

                    /*
                    const int wayIndex = 0;
                    const uint64_t wayId = pg.ways(wayIndex).id();
                    Error::info("Way id: %d", wayId);
                    uint64_t nodeId = 0;
                    for (int i = 0; i < pg.ways(wayIndex).refs_size(); ++i) {
                        nodeId += pg.ways(wayIndex).refs(i);
                        Error::info("  Node %i id: %ld", i, nodeId);
                    }
                    */
                }

                // tell about relations
                if (pg.relations_size() > 0) {
                    found_items = true;

                    //debug("      relations: %d", pg.relations_size());
                    /*if (pg.relations(0).has_info()) {
                        debug("        with meta-info");
                    }*/

                    const int maxrelations = pg.relations_size();
                    for (int i = 0; i < maxrelations; ++i) {
                        const int maxkv = pg.relations(i).keys_size();
                        for (int k = 0; k < maxkv; ++k) {
                            const char *ckey = primblock.stringtable().s(pg.relations(i).keys(k)).c_str();
                            if (strcmp("name", ckey) == 0) {
                                uint64_t id = pg.relations(i).id();
                                const bool result = (*swedishTextTree)->insert(primblock.stringtable().s(pg.relations(i).vals(k)), id << 2 | RELATION_NIBBLE);
                                if (!result)
                                    Error::warn("Cannot insert %s", primblock.stringtable().s(pg.relations(i).vals(k)).c_str());
                            }
                        }

                        const uint64_t relId = pg.relations(i).id();
                        RelationMem rm(pg.relations(i).memids_size());
                        uint64_t memId = 0;
                        for (int k = 0; k < pg.relations(i).memids_size(); ++k) {
                            memId += pg.relations(i).memids(k);
                            rm.members[k] = memId;
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

    /*
    uint64_t nodeId = 3539685440; // Sweden
    //uint64_t nodeId = 13802131; // Isle of Man
    //uint64_t nodeId = 283479923; // Isle of Man
    Coord c;
    bool found = (*n2c)->retrieve(nodeId, c);
    Error::info("Coord for %llu: %lf %lf (found=%i)", nodeId, c.lat, c.lon, (found & 0x000000ff));

    nodeId = 13802131; // Isle of Man
    //uint64_t nodeId = 283479923; // Isle of Man
    found = (*n2c)->retrieve(nodeId, c);
    Error::info("Coord for %llu: %lf %lf (found=%i)", nodeId, c.lat, c.lon, (found & 0x000000ff));

    nodeId = 283479923; // Isle of Man
    found = (*n2c)->retrieve(nodeId, c);
    Error::info("Coord for %llu: %lf %lf (found=%i)", nodeId, c.lat, c.lon, (found & 0x000000ff));

    uint64_t wayId = 349336142; // Isle of Man
    WayNodes wn;
    found = (*w2n)->retrieve(wayId, wn);
    if (found) {
        Error::info("Way for http://www.openstreetmap.org/way/%llu : %i nodes", wayId, wn.num_nodes);
        for (int i = 0; i < wn.num_nodes; ++i) {
            const uint64_t nodeId = wn.nodes[i];
            Error::debug("  Node %llu", nodeId);
            Coord c;
            (*n2c)->retrieve(nodeId, c);
            Error::debug("     at pos %lf %lf", c.lat, c.lon);
        }
    } else
        Error::info("Did not find a way with id %llu", wayId);

    uint64_t relId = 3341682;
    RelationMem rm;
    found = (*relmem)->retrieve(relId, rm);
    if (found) {
        Error::info("Relation for http://www.openstreetmap.org/relation/%llu : %i members", relId, rm.num_members);
        for (int i = 0; i < rm.num_members; ++i) {
            const uint64_t memId = rm.members[i];
            Error::debug("  Member Id %llu", memId);
            Coord c;
            const bool isKnownNode = (*n2c)->retrieve(memId, c);
            if (isKnownNode)
                Error::debug("     at pos %lf %lf", c.lat, c.lon);
        }
    } else
        Error::info("Did not find a relation with id %llu", relId);
    */
    return true;
}

