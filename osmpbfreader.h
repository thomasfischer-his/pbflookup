#ifndef OSMPBFREADER_H
#define OSMPBFREADER_H

#include <istream>

/// This is the header to pbf format
#include <osmpbf/osmpbf.h>

#include "idtree.h"

namespace SwedishText {
class Tree;
}

class OsmPbfReader
{
public:
    OsmPbfReader();
    ~OsmPbfReader();

    SwedishText::Tree *parse(std::istream &input);
    bool parse(std::istream &input, SwedishText::Tree **, IdTree<Coord> **, IdTree<WayNodes> **, IdTree<RelationMem> **);

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
};

#endif // OSMPBFREADER_H
