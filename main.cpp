#include <iostream>
#include <fstream>

#include "swedishtexttree.h"
#include "osmpbfreader.h"
#include "tokenizer.h"

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
        IdTree<RelationMem> *relmem;
        const bool result = osmPbfReader.parse(fp, &swedishTextTree, &n2c, &w2n, &relmem);

        /// Clean up the protobuf lib
        google::protobuf::ShutdownProtobufLibrary();

        fp.close();

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

        std::ifstream textfile("input.txt");
        if (textfile.is_open()) {
            Tokenizer tokenizer;
            std::vector<std::string> words;
            tokenizer.read_words(textfile, words);
            textfile.close();

            static const size_t combined_len = 8188;
            char combined[combined_len + 4];
            for (int s = 3; s >= 1; --s) {
                for (int i = 0; i <= words.size() - s; ++i) {
                    char *p = combined;
                    for (int k = 0; k < s; ++k) {
                        if (k > 0)
                            p += snprintf(p, combined_len - (p - combined), " ");
                        p += snprintf(p, combined_len - (p - combined), "%s", words[i + k].c_str());
                    }

                    std::vector<uint64_t> id_list = swedishTextTree->retrieve_ids(combined);
                    if (!id_list.empty()) {
                        Error::debug("Got %i hits for word '%s' (s=%i)", id_list.size(), combined, s);
                        for (int l = 0; l < id_list.size(); ++l) {
                            const uint64_t id = id_list[l] >> 2;
                            Error::debug("  id=%llu", id);
                            const int lowerBits = id_list[l] & 3;
                            if (lowerBits == NODE_NIBBLE) {
                                Coord c;
                                const bool found = n2c->retrieve(id, c);
                                if (found)
                                    Error::debug("       lat=%lf lon=%lf (found=%i)", c.lat, c.lon);
                                else
                                    Error::debug("    is unknown Node");
                            } else if (lowerBits == WAY_NIBBLE)
                                Error::debug("    is Way");
                            else if (lowerBits == RELATION_NIBBLE)
                                Error::debug("    is Relation");
                            else
                                Error::info("  Neither node, way, nor relation");
                        }
                    }
                }
            }
        }


        if (swedishTextTree != NULL)
            delete swedishTextTree;
        if (n2c != NULL)
            delete n2c;
        if (w2n != NULL)
            delete w2n;
        if (relmem != NULL)
            delete relmem;
    } else
        return 1;


    return 0;
}

