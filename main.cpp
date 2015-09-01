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

#include "swedishtexttree.h"
#include "osmpbfreader.h"
#include "tokenizer.h"
#include "timer.h"
#include "weightednodeset.h"
#include "sweden.h"

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
        Sweden *sweden = NULL;
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
                osmPbfReader.parse(fp, &swedishTextTree, &n2c, &w2n, &relmem, &sweden);
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
            osmPbfReader.parse(fp, &swedishTextTree, &n2c, &w2n, &relmem, &sweden);
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
        if (sweden != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.sweden", tempdir, mapname);
            Error::debug("Writing to '%s'", filenamebuffer);
            ofstream swedenfile(filenamebuffer);
            sweden->write(swedenfile);
            swedenfile.close();
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.sweden", tempdir, mapname);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream swedenfile(filenamebuffer);
            sweden = new Sweden(swedenfile, n2c, w2n, relmem);
            swedenfile.close();
        }
        int64_t elapsed = timer.elapsed();
        Error::info("Spent CPU time to read/write own files: %lius == %.1fs", elapsed, elapsed / 1000000.0);

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
        } else if (strcmp(mapname, "goteborg") == 0) {
            /// Gothenburg
            minlat = 56.9;
            minlon = 11.4;
            maxlat = 59.4;
            maxlon = 14.5;
        }
        sweden->setMinMaxLatLon(minlat, minlon, maxlat, maxlon);

        if (minlat < -500 || minlat > 500)
            Error::warn("Unknown min/max for latitudes and longitudes");
        else
            Error::info("maxlat=%.7f  minlat=%.7f  maxlon=%.7f  minlon=%.7f", maxlat, minlat, maxlon, minlon);

        if (relmem != NULL && w2n != NULL && n2c != NULL && swedishTextTree != NULL) {
            snprintf(filenamebuffer, 1024, "%s/git/pbflookup/input-%s.txt", getenv("HOME"), mapname);
            std::ifstream textfile(filenamebuffer);
            if (textfile.is_open()) {
                Error::info("Reading token from '%s'", filenamebuffer);
                Tokenizer tokenizer(mapname);
                std::vector<std::string> words;
                timer.start();
                tokenizer.read_words(textfile, words, Tokenizer::Unique);
                textfile.close();

                WeightedNodeSet wns(n2c, w2n, relmem);
                wns.setMinMaxLatLon(minlat, maxlat, minlon, maxlon);

                static const size_t combined_len = 8188;
                char combined[combined_len + 4];
                for (int s = 3; s >= 1; --s) {
                    for (unsigned int i = 0; i <= words.size() - s; ++i) {
                        char *p = combined;
                        for (int k = 0; k < s; ++k) {
                            if (k > 0)
                                p += snprintf(p, combined_len - (p - combined), " ");
                            p += snprintf(p, combined_len - (p - combined), "%s", words[i + k].c_str());
                        }

                        const size_t wordlen = strlen(combined);
                        std::vector<uint64_t> id_list = swedishTextTree->retrieve_ids(combined);
                        if (!id_list.empty()) {
                            if (id_list.size() > 1000)
                                Error::debug("Got too many hits (%i) for word '%s' (s=%i), skipping", id_list.size(), combined, s);
                            else {
                                Error::debug("Got %i hits for word '%s' (s=%i)", id_list.size(), combined, s);
                                for (unsigned int l = 0; l < id_list.size(); ++l) {
                                    const uint64_t id = id_list[l] >> 2;
                                    const int lowerBits = id_list[l] & 3;
                                    if (lowerBits == NODE_NIBBLE) {
#ifdef DEBUG
                                        Error::debug("   https://www.openstreetmap.org/node/%llu", id);
#endif // DEBUG
                                        wns.appendNode(id, s, wordlen);
                                    } else if (lowerBits == WAY_NIBBLE) {
#ifdef DEBUG
                                        Error::debug("   https://www.openstreetmap.org/way/%llu", id);
#endif // DEBUG
                                        wns.appendWay(id, s, wordlen);
                                    } else if (lowerBits == RELATION_NIBBLE) {
#ifdef DEBUG
                                        Error::debug("   https://www.openstreetmap.org/relation/%llu", id);
#endif // DEBUG
                                        wns.appendRelation(id, s, wordlen);
                                    } else
                                        Error::warn("  Neither node, way, nor relation: %llu", id);
                                }
                            }
                        }
                    }
                }

                wns.powerCluster(2.0, 2.0 / wns.size());
                wns.normalize();
                wns.dump();

                elapsed = timer.elapsed();
                Error::info("Spent CPU time to tokenize and to search in data: %lius == %.1fs", elapsed, elapsed / 1000000.0);

                if (sweden != NULL) {
                    timer.start();
                    uint64_t id = 798695652;
                    const int scbcode = sweden->insideSCBcode(id);
                    if (scbcode > 0) {
                        Error::info("SCB code for node %llu is %i", id, scbcode);
                        Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcode);
                    } else
                        Error::warn("No SCB code found for node %llu", id);
                    const int nuts3code = sweden->insideNUTS3code(id);
                    if (nuts3code > 0) {
                        Error::info("NUTS3 code for node %llu is %i", id, nuts3code);
                        Error::debug("  http://nuts.geovocab.org/id/SE%i.html", nuts3code);
                    } else
                        Error::warn("No NUTS3 code found for node %llu", id);
                    elapsed = timer.elapsed();
                    Error::info("Spent CPU time to search SCB/NUTS3 in data: %lius == %.1fs", elapsed, elapsed / 1000000.0);
                }
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
        if (sweden != NULL)
            delete sweden;
        elapsed = timer.elapsed();
        Error::info("Spent CPU time to free memory: %lius == %.1fs", elapsed, elapsed / 1000000.0);
    } else
        return 1;


    return 0;
}

