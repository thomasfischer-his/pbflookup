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
#include <boost/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

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

void loadOrSaveSwedishTextTree(SwedishText::Tree **swedishTextTree, const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (*swedishTextTree != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        ofstream swedishtexttreefile(filenamebuffer);
        (*swedishTextTree)->write(swedishtexttreefile);
        swedishtexttreefile.close();
    } else {
        snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
        Error::debug("Reading from '%s'", filenamebuffer);
        ifstream swedishtexttreefile(filenamebuffer);
        *swedishTextTree = new SwedishText::Tree(swedishtexttreefile);
        swedishtexttreefile.close();
    }
}

void loadOrSaveN2c(IdTree<Coord> **n2c, const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (*n2c != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        ofstream n2cfile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(n2cfile);
        (*n2c)->write(out);
    } else {
        snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, mapname);
        Error::debug("Reading from '%s'", filenamebuffer);
        ifstream n2cfile(filenamebuffer);
        boost::iostreams::filtering_istream in;
        in.push(boost::iostreams::gzip_decompressor());
        in.push(n2cfile);
        *n2c = new IdTree<Coord>(in);
    }
}

void loadOrSaveW2n(IdTree<WayNodes> **w2n, const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (*w2n != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        ofstream w2nfile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(w2nfile);
        (*w2n)->write(out);
    } else {
        snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, mapname);
        Error::debug("Reading from '%s'", filenamebuffer);
        ifstream w2nfile(filenamebuffer);
        boost::iostreams::filtering_istream in;
        in.push(boost::iostreams::gzip_decompressor());
        in.push(w2nfile);
        *w2n = new IdTree<WayNodes>(in);
    }
}

void loadOrSaveRelMem(IdTree<RelationMem> **relmem, const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (*relmem != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.relmem", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        ofstream relmemfile(filenamebuffer);
        (*relmem)->write(relmemfile);
        relmemfile.close();
    } else {
        snprintf(filenamebuffer, 1024, "%s/%s.relmem", tempdir, mapname);
        Error::debug("Reading from '%s'", filenamebuffer);
        ifstream relmemfile(filenamebuffer);
        *relmem = new IdTree<RelationMem>(relmemfile);
        relmemfile.close();
    }
}

void saveSweden(Sweden **sweden, const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (*sweden != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.sweden", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        ofstream swedenfile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(swedenfile);
        (*sweden)->write(out);
    }
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
                int64_t cputime, walltime;
                timer.elapsed(&cputime, &walltime);
                Error::info("Spent CPU time to parse .osm.pbf file: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);
            }
        } else {
            Timer timer;
            OsmPbfReader osmPbfReader;
            osmPbfReader.parse(fp, &swedishTextTree, &n2c, &w2n, &relmem, &sweden);
            int64_t cputime, walltime;
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to parse .osm.pbf file: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);
        }
        fileteststream.close();
        /// Clean up the protobuf lib
        google::protobuf::ShutdownProtobufLibrary();
        fp.close();

        Timer timer;
        boost::thread threadLoadOrSaveSwedishTextTree(loadOrSaveSwedishTextTree, &swedishTextTree, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        boost::thread threadLoadOrSaveN2c(loadOrSaveN2c, &n2c, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        boost::thread threadLoadOrSaveW2n(loadOrSaveW2n, &w2n, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        boost::thread threadLoadOrRelMem(loadOrSaveRelMem, &relmem, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        boost::thread threadSaveSweden(saveSweden, &sweden, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        Error::debug("Waiting for threads to join");
        threadLoadOrSaveSwedishTextTree.join();
        threadLoadOrSaveN2c.join();
        threadLoadOrSaveW2n.join();
        threadLoadOrRelMem.join();
        threadSaveSweden.join();
        Error::debug("All threads joined");

        if (sweden == NULL && n2c != NULL && w2n != NULL && relmem != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.sweden", tempdir, mapname);
            Error::debug("Reading from '%s'", filenamebuffer);
            ifstream swedenfile(filenamebuffer);
            boost::iostreams::filtering_istream in;
            in.push(boost::iostreams::gzip_decompressor());
            in.push(swedenfile);
            sweden = new Sweden(in, n2c, w2n, relmem);
        }

        int64_t cputime, walltime;
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to read/write own files: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);

        if (relmem != NULL && w2n != NULL && n2c != NULL && swedishTextTree != NULL && sweden != NULL) {
            snprintf(filenamebuffer, 1024, "%s/git/pbflookup/input-%s.txt", getenv("HOME"), mapname);
            std::ifstream textfile(filenamebuffer);
            if (textfile.is_open()) {
                Error::info("Reading token from '%s'", filenamebuffer);
                Tokenizer tokenizer(mapname);
                std::vector<std::string> words;
                timer.start();
                tokenizer.read_words(textfile, words, Tokenizer::Unique);
                textfile.close();

                WeightedNodeSet wns(n2c, w2n, relmem, sweden);

                TokenProcessor tokenProcessor(swedishTextTree, n2c, w2n, relmem, sweden);
                tokenProcessor.evaluteWordCombinations(words, wns);
                tokenProcessor.evaluteRoads(words, wns);

                //wns.powerCluster(2.0, 2.0 / wns.size());
                wns.normalize();
                //wns.dump();
            }

            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to tokenize and to search in data: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);

            timer.start();
            sweden->test();
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to search SCB/NUTS3 in data: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);
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
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to free memory: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);
    } else
        return 1;


    return 0;
}

