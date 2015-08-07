#include <cstdlib>
#include <iostream>
#include <fstream>

#include "swedishtexttree.h"
#include "osmpbfreader.h"
#include "tokenizer.h"

using namespace std;

int main(int argc, char *argv[])
{
#ifdef DEBUG
    Error::debug("DEBUG flag enabled");
#endif // DEBUG

    const char *tempdir = getenv("TEMPDIR") == NULL || getenv("TEMPDIR")[0] == '\0' ? "/tmp" : getenv("TEMPDIR");

    char filenamebuffer[1024];
    const char *country = (argc < 2) ? "sweden" : argv[argc - 1];
    snprintf(filenamebuffer, 1024, "%s/git/pbflookup/%s.osm.pbf", getenv("HOME"), country);
    ifstream fp(filenamebuffer, ifstream::in | ifstream::binary);
    if (fp) {
        SwedishText::Tree *swedishTextTree = NULL;
        IdTree<Coord> *n2c = NULL;
        IdTree<WayNodes> *w2n = NULL;
        IdTree<RelationMem> *relmem = NULL;

        snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, country);
        ifstream fileteststream(filenamebuffer);
        if (fileteststream && fileteststream.good()) {
            fileteststream.seekg(0, fileteststream.end);
            const int length = fileteststream.tellg();
            fileteststream.seekg(0, fileteststream.beg);

            if (length < 10) {
                OsmPbfReader osmPbfReader;
                osmPbfReader.parse(fp, &swedishTextTree, &n2c, &w2n, &relmem);
            }
        } else {
            OsmPbfReader osmPbfReader;
            osmPbfReader.parse(fp, &swedishTextTree, &n2c, &w2n, &relmem);
        }
        fileteststream.close();
        /// Clean up the protobuf lib
        google::protobuf::ShutdownProtobufLibrary();
        fp.close();

        if (swedishTextTree != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, country);
            Error::debug("Writing to '%s'", filenamebuffer);
            ofstream swedishtexttreefile(filenamebuffer);
            swedishTextTree->write(swedishtexttreefile);
            swedishtexttreefile.close();
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, country);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream swedishtexttreefile(filenamebuffer);
            swedishTextTree = new SwedishText::Tree(swedishtexttreefile);
            swedishtexttreefile.close();
        }
        if (n2c != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, country);
            Error::debug("Writing to '%s'", filenamebuffer);
            ofstream n2cfile(filenamebuffer);
            n2c->write(n2cfile);
            n2cfile.close();
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, country);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream n2cfile(filenamebuffer);
            n2c = new IdTree<Coord>(n2cfile);
            n2cfile.close();
        }
        if (w2n != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, country);
            Error::debug("Writing to '%s'", filenamebuffer);
            ofstream w2nfile(filenamebuffer);
            w2n->write(w2nfile);
            w2nfile.close();
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, country);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream w2nfile(filenamebuffer);
            w2n = new IdTree<WayNodes>(w2nfile);
            w2nfile.close();
        }
        if (relmem != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.relmem", tempdir, country);
            Error::debug("Writing to '%s'", filenamebuffer);
            ofstream relmemfile(filenamebuffer);
            relmem->write(relmemfile);
            relmemfile.close();
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.relmem", tempdir, country);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream relmemfile(filenamebuffer);
            relmem = new IdTree<RelationMem>(relmemfile);
            relmemfile.close();
        }

        std::ifstream textfile("input.txt");
        if (textfile.is_open()) {
            Tokenizer tokenizer;
            std::vector<std::string> words;
            tokenizer.read_words(textfile, words);
            textfile.close();

            if (relmem != NULL && w2n != NULL && n2c != NULL && swedishTextTree != NULL) {
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

