#include <iostream>
#include <fstream>

#include "swedishtexttree.h"
#include "osmpbfreader.h"

using namespace std;

int main(int argc, char *argv[])
{
    const char *inputfilename = (argc < 2) ? "sweden.osm.pbf" : argv[argc - 1];
    ifstream fp(inputfilename, ifstream::in | ifstream::binary);
    if (fp) {

        OsmPbfReader osmPbfReader;
        SwedishText::Tree *swedishTextTree;
        IdTree<Coord> *n2c;
        IdTree<WayNodes> *w2n;
        const bool result = osmPbfReader.parse(fp, &swedishTextTree, &n2c, &w2n);

        /// Clean up the protobuf lib
        google::protobuf::ShutdownProtobufLibrary();

        fp.close();

        std::cout << "Tree size=" << swedishTextTree->size() << std::endl;

        /*
        while (1) {
            if (!std::cin.good()) {
                std::cout << std::endl;
                break;
            }
            std::cout << std::endl << "New query: ";
            std::string keyboardinput;
            std::getline(std::cin, keyboardinput);
            const char *query = keyboardinput.c_str();
            if (query[0] == 0 || query[0] < 0x20) {
                std::cout << std::endl;
                break;
            }

            std::vector< uint64_t> ids = swedishTextTree->retrieve_ids(query);
            for (unsigned int i = 0; i < ids.size(); ++i) {
                std::cout << "  http://www.openstreetmap.org/";
                switch (ids[i] & 3) {
                case NODE_NIBBLE: std::cout << "node/"; break;
                case WAY_NIBBLE: std::cout << "way/"; break;
                case RELATION_NIBBLE: std::cout << "relation/"; break;
                default: break;
                }
                std::cout << (ids[i] >> 2) << std::endl;
            }
            std::cout << "Number of hits: " << ids.size() << std::endl;
        }
        */

        if (swedishTextTree != NULL)
            delete swedishTextTree;
        if (n2c != NULL)
            delete n2c;
        if (w2n != NULL)
            delete w2n;
    } else
        return 1;


    return 0;
}

