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
#include "htmloutput.h"
#include "global.h"
#include "globalobjects.h"

IdTree<WayNodes> *wayNodes = NULL; ///< declared in 'globalobjects.h'
IdTree<Coord> *node2Coord = NULL; ///< declared in 'globalobjects.h'
IdTree<RelationMem> *relMembers = NULL; ///< declared in 'globalobjects.h'
IdTree<WriteableString> *nodeNames = NULL; ///< declared in 'globalobjects.h'
SwedishTextTree *swedishTextTree = NULL; ///< declared in 'globalobjects.h'
Sweden *sweden = NULL; ///< declared in 'globalobjects.h'

inline bool ends_with(std::string const &value, std::string const &ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void loadOrSaveSwedishTextTree(const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (swedishTextTree != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream swedishtexttreefile(filenamebuffer);
        swedishTextTree->write(swedishtexttreefile);
        swedishtexttreefile.close();
    } else {
        snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
        Error::debug("Reading from '%s'", filenamebuffer);
        std::ifstream swedishtexttreefile(filenamebuffer);
        swedishTextTree = new SwedishTextTree(swedishtexttreefile);
        swedishtexttreefile.close();
    }
}

void loadOrSaveNode2Coord(const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (node2Coord != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream node2CoordFile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(node2CoordFile);
        node2Coord->write(out);
    } else {
        snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, mapname);
        Error::debug("Reading from '%s'", filenamebuffer);
        std::ifstream node2CoordFile(filenamebuffer);
        boost::iostreams::filtering_istream in;
        in.push(boost::iostreams::gzip_decompressor());
        in.push(node2CoordFile);
        node2Coord = new IdTree<Coord>(in);
    }
}

void loadOrSaveNodeNames(const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (nodeNames != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.nn", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream nnfile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(nnfile);
        nodeNames->write(out);
    } else {
        snprintf(filenamebuffer, 1024, "%s/%s.nn", tempdir, mapname);
        Error::debug("Reading from '%s'", filenamebuffer);
        std::ifstream nnfile(filenamebuffer);
        boost::iostreams::filtering_istream in;
        in.push(boost::iostreams::gzip_decompressor());
        in.push(nnfile);
        nodeNames = new IdTree<WriteableString>(in);
    }
}

void loadOrSaveWayNodes(const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (wayNodes != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream wayNodeFile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(wayNodeFile);
        wayNodes->write(out);
    } else {
        snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, mapname);
        Error::debug("Reading from '%s'", filenamebuffer);
        std::ifstream wayNodeFile(filenamebuffer);
        boost::iostreams::filtering_istream in;
        in.push(boost::iostreams::gzip_decompressor());
        in.push(wayNodeFile);
        wayNodes = new IdTree<WayNodes>(in);
    }
}

void loadOrSaveRelMem(const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (relMembers != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.relmem", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream relmemfile(filenamebuffer);
        relMembers->write(relmemfile);
        relmemfile.close();
    } else {
        snprintf(filenamebuffer, 1024, "%s/%s.relmem", tempdir, mapname);
        Error::debug("Reading from '%s'", filenamebuffer);
        std::ifstream relmemfile(filenamebuffer);
        relMembers = new IdTree<RelationMem>(relmemfile);
        relmemfile.close();
    }
}

void saveSweden(const char *tempdir, const char *mapname) {
    char filenamebuffer[1024];
    if (sweden != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.sweden", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream swedenfile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(swedenfile);
        sweden->write(out);
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
    std::ifstream fp(filenamebuffer, std::ifstream::in | std::ifstream::binary);
    if (fp) {
        swedishTextTree = NULL;
        node2Coord = NULL;
        wayNodes = NULL;
        relMembers = NULL;
        nodeNames = NULL;
        sweden = NULL;

        snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
        std::ifstream fileteststream(filenamebuffer);
        if (fileteststream && fileteststream.good()) {
            fileteststream.seekg(0, fileteststream.end);
            const int length = fileteststream.tellg();
            fileteststream.seekg(0, fileteststream.beg);

            if (length < 10) {
                Timer timer;
                OsmPbfReader osmPbfReader;
                osmPbfReader.parse(fp);
                int64_t cputime, walltime;
                timer.elapsed(&cputime, &walltime);
                Error::info("Spent CPU time to parse .osm.pbf file: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);
            }
        } else {
            Timer timer;
            OsmPbfReader osmPbfReader;
            osmPbfReader.parse(fp);
            int64_t cputime, walltime;
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to parse .osm.pbf file: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);
        }
        fileteststream.close();
        /// Clean up the protobuf lib
        google::protobuf::ShutdownProtobufLibrary();
        fp.close();

        Timer timer;
        boost::thread threadLoadOrSaveSwedishTextTree(loadOrSaveSwedishTextTree, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        boost::thread threadLoadOrSaveNode2Cood(loadOrSaveNode2Coord, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        boost::thread threadLoadOrSaveNodeNames(loadOrSaveNodeNames, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        boost::thread threadLoadOrSaveWayNodes(loadOrSaveWayNodes, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        boost::thread threadLoadOrRelMem(loadOrSaveRelMem, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        boost::thread threadSaveSweden(saveSweden, tempdir, mapname);
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        Error::debug("Waiting for threads to join");
        threadLoadOrSaveSwedishTextTree.join();
        threadLoadOrSaveNode2Cood.join();
        threadLoadOrSaveNodeNames.join();
        threadLoadOrSaveWayNodes.join();
        threadLoadOrRelMem.join();
        threadSaveSweden.join();
        Error::debug("All threads joined");

        if (sweden == NULL && node2Coord != NULL && nodeNames != NULL && wayNodes != NULL && relMembers != NULL) {
            snprintf(filenamebuffer, 1024, "%s/%s.sweden", tempdir, mapname);
            Error::debug("Reading from '%s'", filenamebuffer);
            std::ifstream swedenfile(filenamebuffer);
            boost::iostreams::filtering_istream in;
            in.push(boost::iostreams::gzip_decompressor());
            in.push(swedenfile);
            sweden = new Sweden(in);
        }

        int64_t cputime, walltime;
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to read/write own files: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);

        if (relMembers != NULL && wayNodes != NULL && node2Coord != NULL && nodeNames != NULL && swedishTextTree != NULL && sweden != NULL) {

            snprintf(filenamebuffer, 1024, "%s/git/pbflookup/input-%s.txt", getenv("HOME"), mapname);
            std::ifstream textfile(filenamebuffer);
            if (textfile.is_open()) {
                Error::info("Reading token from '%s'", filenamebuffer);
                timer.start();

                std::vector<std::string> words;
                Tokenizer tokenizer(mapname);
                tokenizer.read_words(textfile, words, Tokenizer::Unique);
                textfile.close();

                WeightedNodeSet wns;
                TokenProcessor tokenProcessor;
                tokenProcessor.evaluteWordCombinations(words, wns);
                tokenProcessor.evaluteRoads(words, wns);

                /*
                Error::debug("Running 'powerCluster'");
                wns.powerCluster(2.0, 2.0 / wns.size());
                Error::debug("Running 'powerMunicipalityCluster'");
                wns.powerMunicipalityCluster(5.0 / wns.size());
                Error::debug("Running 'normalize'");
                wns.normalize();
                wns.dumpGpx();
                */

                Error::debug("Running 'powerCluster'");
                wns.powerCluster(2.0, 2.0 / wns.size());
                Error::debug("Running 'buildRingCluster'");
                wns.buildRingCluster();
                wns.dumpRingCluster();

                if (!wns.ringClusters.empty()) {
                    if (tokenProcessor.knownRoads().empty())
                        Error::warn("No roads known");
                    else
                        for (auto it = tokenProcessor.knownRoads().cbegin(); it != tokenProcessor.knownRoads().cend(); ++it) {
                            const Sweden::Road &road = *it;
                            int64_t minSqDistance = INT64_MAX;
                            uint64_t bestNode = 0;
                            sweden->closestPointToRoad(wns.ringClusters.front().weightedCenterX, wns.ringClusters.front().weightedCenterY, road, bestNode, minSqDistance);
                        }
                } else
                    Error::warn("wns.ringClusters is empty");

                timer.elapsed(&cputime, &walltime);
                Error::info("Spent CPU time to tokenize and to search in data: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);

                HtmlOutput htmlOutput(tokenizer, wns);
                htmlOutput.write(words, std::string("/tmp/html"));
            }

            /*
            timer.start();
            sweden->test();
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to search SCB/NUTS3 in data: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);
            */
        }

        timer.start();
        if (swedishTextTree != NULL)
            delete swedishTextTree;
        if (node2Coord != NULL)
            delete node2Coord;
        if (nodeNames != NULL)
            delete nodeNames;
        if (wayNodes != NULL)
            delete wayNodes;
        if (relMembers != NULL)
            delete relMembers;
        if (sweden != NULL)
            delete sweden;
        timer.elapsed(&cputime, &walltime);
        Error::info("Spent CPU time to free memory: %lius == %.1fs  (wall time: %lius == %.1fs)", cputime, cputime / 1000000.0, walltime, walltime / 1000000.0);
    } else
        return 1;


    return 0;
}

