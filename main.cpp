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

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <algorithm>

#include "swedishtexttree.h"
#include "osmpbfreader.h"
#include "tokenizer.h"
#include "timer.h"
#include "weightednodeset.h"

using namespace std;

inline bool ends_with(std::string const &value, std::string const &ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

int main(int argc, char *argv[])
{
#ifdef DEBUG
    Error::debug("DEBUG flag enabled");
#endif // DEBUG

    const char *tempdir = getenv("TEMPDIR") == NULL || getenv("TEMPDIR")[0] == '\0' ? "/tmp" : getenv("TEMPDIR");

    char filenamebuffer[1024];
    const char *mapname = (argc < 2) ? "sweden" : argv[argc - 1];
    snprintf(filenamebuffer, 1024, "%s/git/pbflookup/%s.osm.pbf", getenv("HOME"), mapname);
    ifstream fp(filenamebuffer, ifstream::in | ifstream::binary);
    if (fp) {
        SwedishText::Tree *swedishTextTree = NULL;
        IdTree<Coord> *n2c = NULL;
        IdTree<WayNodes> *w2n = NULL;
        IdTree<RelationMem> *relmem = NULL;
        double minlat = 1000.0;
        double maxlat = -1000.0;
        double minlon = 1000.0;
        double maxlon = -1000.0;


        snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
        ifstream fileteststream(filenamebuffer);
        if (fileteststream && fileteststream.good()) {
            fileteststream.seekg(0, fileteststream.end);
            const int length = fileteststream.tellg();
            fileteststream.seekg(0, fileteststream.beg);

            if (length < 10) {
                Timer timer;
                OsmPbfReader osmPbfReader;
                osmPbfReader.parse(fp, &swedishTextTree, &n2c, &w2n, &relmem);
                const int64_t elapsed = timer.elapsed();
                Error::info("Spent %li us (CPU) to parse .osm.pbf file", elapsed);

                minlat = osmPbfReader.min_lat();
                maxlat = osmPbfReader.max_lat();
                minlon = osmPbfReader.min_lon();
                maxlon = osmPbfReader.max_lon();
                Error::info("maxlat=%.7f  minlat=%.7f  maxlon=%.7f  minlon=%.7f", maxlat, minlat, maxlon, minlon);
            }
        } else {
            Timer timer;
            OsmPbfReader osmPbfReader;
            osmPbfReader.parse(fp, &swedishTextTree, &n2c, &w2n, &relmem);
            const int64_t elapsed = timer.elapsed();
            Error::info("Spent %li us (CPU) to parse .osm.pbf file", elapsed);

            minlat = osmPbfReader.min_lat();
            maxlat = osmPbfReader.max_lat();
            minlon = osmPbfReader.min_lon();
            maxlon = osmPbfReader.max_lon();
            Error::info("maxlat=%.7f  minlat=%.7f  maxlon=%.7f  minlon=%.7f", maxlat, minlat, maxlon, minlon);
        }
        fileteststream.close();
        /// Clean up the protobuf lib
        google::protobuf::ShutdownProtobufLibrary();
        fp.close();

        Timer timer;
        if (swedishTextTree != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
            Error::debug("Writing to '%s'", filenamebuffer);
            ofstream swedishtexttreefile(filenamebuffer);
            swedishTextTree->write(swedishtexttreefile);
            swedishtexttreefile.close();
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream swedishtexttreefile(filenamebuffer);
            swedishTextTree = new SwedishText::Tree(swedishtexttreefile);
            swedishtexttreefile.close();
        }
        if (n2c != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, mapname);
            Error::debug("Writing to '%s'", filenamebuffer);
            ofstream n2cfile(filenamebuffer);
            n2c->write(n2cfile);
            n2cfile.close();
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, mapname);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream n2cfile(filenamebuffer);
            n2c = new IdTree<Coord>(n2cfile);
            n2cfile.close();
        }
        if (w2n != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, mapname);
            Error::debug("Writing to '%s'", filenamebuffer);
            ofstream w2nfile(filenamebuffer);
            w2n->write(w2nfile);
            w2nfile.close();
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, mapname);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream w2nfile(filenamebuffer);
            w2n = new IdTree<WayNodes>(w2nfile);
            w2nfile.close();
        }
        if (relmem != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.relmem", tempdir, mapname);
            Error::debug("Writing to '%s'", filenamebuffer);
            ofstream relmemfile(filenamebuffer);
            relmem->write(relmemfile);
            relmemfile.close();
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.relmem", tempdir, mapname);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream relmemfile(filenamebuffer);
            relmem = new IdTree<RelationMem>(relmemfile);
            relmemfile.close();
        }
        int64_t elapsed = timer.elapsed();
        Error::info("Spent %li us (CPU) to read/write own files", elapsed);

        if (strcmp(mapname, "isle-of-man") == 0) {
            /// Isle of man
            minlat = 54.0;
            minlon = -5.0;
            maxlat = 54.5;
            maxlon = -4.0;
        } else if (strcmp(mapname, "sweden") == 0) {
            /// Sweden
            minlat = 53.8;
            minlon = 4.4;
            maxlat = 71.2;
            maxlon = 31.2;
        }
        if (minlat < -500 || minlat > 500)
            Error::warn("Unknown min/max for latitudes and longitudes");
        else
            Error::info("maxlat=%.7f  minlat=%.7f  maxlon=%.7f  minlon=%.7f", maxlat, minlat, maxlon, minlon);

        if (relmem != NULL && w2n != NULL && n2c != NULL && swedishTextTree != NULL) {
            snprintf(filenamebuffer, 1024, "input-%s.txt", country);
            std::ifstream textfile(filenamebuffer);
            if (textfile.is_open()) {
                Error::debug("Reading token from '%s'", filenamebuffer);
                Tokenizer tokenizer;
                std::vector<std::string> words;
                timer.start();
                tokenizer.read_words(textfile, words);
                textfile.close();

                static const std::string ends_with_colon_s = ":s";
                static const std::string ends_with_s = "s";
                for (std::vector<std::string>::iterator it = words.begin(); it != words.end(); ++it) {
                    if (ends_with(*it, ends_with_colon_s))
                        it = words.insert(it, (*it).substr(0, (*it).length() - ends_with_colon_s.length()));
                    else if (ends_with(*it, ends_with_s))
                        it = words.insert(it, (*it).substr(0, (*it).length() - ends_with_s.length()));
                }

                WeightedNodeSet wns(n2c, w2n, relmem);

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
                            if (id_list.size() > 100)
                                Error::debug("Got too many hits (%i) for word '%s' (s=%i), skipping", id_list.size(), combined, s);
                            else {
                                Error::debug("Got %i hits for word '%s' (s=%i)", id_list.size(), combined, s);
                                for (int l = 0; l < id_list.size(); ++l) {
                                    const uint64_t id = id_list[l] >> 2;
                                    Error::debug("  id=%llu", id);
                                    const int lowerBits = id_list[l] & 3;
                                    if (lowerBits == NODE_NIBBLE) {
                                        wns.appendNode(id, s);
                                        /*
                                                Coord c;
                                        const bool found = n2c->retrieve(id, c);
                                        if (found)
                                            Error::debug("       lat=%lf lon=%lf (found=%i)", c.lat, c.lon);
                                        else
                                            Error::debug("    is unknown Node");
                                            */
                                    } else if (lowerBits == WAY_NIBBLE) {
                                        wns.appendWay(id);
                                        //Error::debug("    is Way");
                                    } else if (lowerBits == RELATION_NIBBLE) {
                                        wns.appendRelation(id);
                                        //Error::debug("    is Relation");
                                    } else
                                        Error::info("  Neither node, way, nor relation");
                                }
                            }
                        }
                    }

                    std::sort(wns.begin(), wns.end(), greater<WeightedNode>());
                    wns.dump();
                }

                elapsed = timer.elapsed();
                Error::info("Spent %li us (CPU) to tokenize and to search in data", elapsed);
            }
        }

        timer.start();
        if (swedishTextTree != NULL)
            delete swedishTextTree;
        if (n2c != NULL)
            delete n2c;
        if (w2n != NULL)
            delete w2n;
        if (relmem != NULL)
            delete relmem;
        elapsed = timer.elapsed();
        Error::info("Spent %li us (CPU) to free memory", elapsed);
    } else
        return 1;


    return 0;
}

