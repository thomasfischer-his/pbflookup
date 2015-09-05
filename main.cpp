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

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include "swedishtexttree.h"
#include "osmpbfreader.h"
#include "tokenizer.h"
#include "timer.h"
#include "weightednodeset.h"
#include "sweden.h"
#include "tokenprocessor.h"

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
                Error::info("Spent CPU time to parse .osm.pbf file: %lius == %.1fs", elapsed, elapsed / 1000000.0);

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
            Error::info("Spent CPU time to parse .osm.pbf file: %lius == %.1fs", elapsed, elapsed / 1000000.0);

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
            boost::iostreams::filtering_ostream out;
            out.push(boost::iostreams::gzip_compressor());
            out.push(n2cfile);
            n2c->write(out);
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, mapname);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream n2cfile(filenamebuffer);
            boost::iostreams::filtering_istream in;
            in.push(boost::iostreams::gzip_decompressor());
            in.push(n2cfile);
            n2c = new IdTree<Coord>(in);
        }
        if (w2n != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, mapname);
            Error::debug("Writing to '%s'", filenamebuffer);
            ofstream w2nfile(filenamebuffer);
            boost::iostreams::filtering_ostream out;
            out.push(boost::iostreams::gzip_compressor());
            out.push(w2nfile);
            w2n->write(out);
        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, mapname);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream w2nfile(filenamebuffer);
            boost::iostreams::filtering_istream in;
            in.push(boost::iostreams::gzip_decompressor());
            in.push(w2nfile);
            w2n = new IdTree<WayNodes>(in);
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
            boost::iostreams::filtering_ostream out;
            out.push(boost::iostreams::gzip_compressor());
            out.push(swedenfile);
            sweden->write(out);

        } else {
            snprintf(filenamebuffer, 1024, "%s/%s.sweden", tempdir, mapname);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream swedenfile(filenamebuffer);
            boost::iostreams::filtering_istream in;
            in.push(boost::iostreams::gzip_decompressor());
            in.push(swedenfile);
            sweden = new Sweden(in, n2c, w2n, relmem);
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
        } else if (strcmp(mapname, "westsweden") == 0) {
            /// West Sweden
            minlat = 56.3;
            minlon = 10.4;
            maxlat = 59.9;
            maxlon = 16.3;
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

                TokenProcessor tokenProcessor(swedishTextTree, n2c, w2n, relmem, sweden);
                tokenProcessor.evaluteWordCombinations(words, wns);
                tokenProcessor.evaluteRoads(words, wns);

                //wns.powerCluster(2.0, 2.0 / wns.size());
                wns.normalize();
                //wns.dump();
            }

            elapsed = timer.elapsed();
            Error::info("Spent CPU time to tokenize and to search in data: %lius == %.1fs", elapsed, elapsed / 1000000.0);

            if (sweden != NULL) {
                timer.start();
                uint64_t id = 322746501;
                Coord coord;
                if (n2c->retrieve(id, coord)) {
                    Error::info("node %llu is located at lat=%.5f, lon=%.5f", id, coord.lat, coord.lon);
                }
                int scbcode = sweden->insideSCBarea(id);
                if (scbcode <= 0)
                    Error::warn("No SCB code found for node %llu", id);
                else if (scbcode == 2361) {
                    Error::info("Found correct SCB code for node %llu is %i", id, scbcode);
                    Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcode);
                } else {
                    Error::warn("Found SCB code for node %llu is %i, should be 2361", id, scbcode);
                    Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcode);
                }
                int nuts3code = sweden->insideNUTS3area(id);
                if (nuts3code > 0) {
                    Error::info("NUTS3 code for node %llu is %i", id, nuts3code);
                    Error::debug("  http://nuts.geovocab.org/id/SE%i.html", nuts3code);
                } else
                    Error::warn("No NUTS3 code found for node %llu", id);

                id = 541187594;
                if (n2c->retrieve(id, coord)) {
                    Error::info("node %llu is located at lat=%.5f, lon=%.5f", id, coord.lat, coord.lon);
                }
                scbcode = sweden->insideSCBarea(id);
                if (scbcode <= 0)
                    Error::warn("No SCB code found for node %llu", id);
                else if (scbcode == 2034) {
                    Error::info("Found correct SCB code for node %llu is %i", id, scbcode);
                    Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcode);
                } else {
                    Error::warn("Found SCB code for node %llu is %i, should be 2034", id, scbcode);
                    Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcode);
                }
                nuts3code = sweden->insideNUTS3area(id);
                if (nuts3code > 0) {
                    Error::info("NUTS3 code for node %llu is %i", id, nuts3code);
                    Error::debug("  http://nuts.geovocab.org/id/SE%i.html", nuts3code);
                } else
                    Error::warn("No NUTS3 code found for node %llu", id);


                id = 3170517078;
                if (n2c->retrieve(id, coord)) {
                    Error::info("node %llu is located at lat=%.5f, lon=%.5f", id, coord.lat, coord.lon);
                }
                scbcode = sweden->insideSCBarea(id);
                if (scbcode <= 0)
                    Error::warn("No SCB code found for node %llu", id);
                else if (scbcode == 2161) {
                    Error::info("Found correct SCB code for node %llu is %i", id, scbcode);
                    Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcode);
                } else {
                    Error::warn("Found SCB code for node %llu is %i, should be 2161", id, scbcode);
                    Error::debug("  http://www.ekonomifakta.se/sv/Fakta/Regional-statistik/Din-kommun-i-siffror/Oversikt-for-region/?region=%i", scbcode);
                }
                nuts3code = sweden->insideNUTS3area(id);
                if (nuts3code > 0) {
                    Error::info("NUTS3 code for node %llu is %i", id, nuts3code);
                    Error::debug("  http://nuts.geovocab.org/id/SE%i.html", nuts3code);
                } else
                    Error::warn("No NUTS3 code found for node %llu", id);

                elapsed = timer.elapsed();
                Error::info("Spent CPU time to search SCB/NUTS3 in data: %lius == %.1fs", elapsed, elapsed / 1000000.0);
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

