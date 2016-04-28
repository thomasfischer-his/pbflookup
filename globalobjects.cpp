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

#include "globalobjects.h"

#include <iostream>
#include <fstream>
#include <istream>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/thread.hpp>

#include "error.h"
#include "config.h"
#include "osmpbfreader.h"


IdTree<WayNodes> *wayNodes = NULL; ///< declared in 'globalobjects.h'
IdTree<Coord> *node2Coord = NULL; ///< declared in 'globalobjects.h'
IdTree<RelationMem> *relMembers = NULL; ///< declared in 'globalobjects.h'
IdTree<WriteableString> *nodeNames = NULL; ///< declared in 'globalobjects.h'
IdTree<WriteableString> *wayNames = NULL; ///< declared in 'globalobjects.h'
IdTree<WriteableString> *relationNames = NULL; ///< declared in 'globalobjects.h'
SwedishTextTree *swedishTextTree = NULL; ///< declared in 'globalobjects.h'
Sweden *sweden = NULL; ///< declared in 'globalobjects.h'


void loadSwedishTextTree() {
    char filenamebuffer[1024];
    snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
    Error::debug("Reading from '%s'", filenamebuffer);
    std::ifstream swedishtexttreefile(filenamebuffer);
    swedishTextTree = new SwedishTextTree(swedishtexttreefile);
    swedishtexttreefile.close();
}

void saveSwedishTextTree() {
    char filenamebuffer[1024];
    if (swedishTextTree != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream swedishtexttreefile(filenamebuffer);
        swedishTextTree->write(swedishtexttreefile);
        swedishtexttreefile.close();
    } else
        Error::err("Cannot save swedishTextTree, variable is NULL");
}

void loadNode2Coord() {
    char filenamebuffer[1024];
    snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, mapname);
    Error::debug("Reading from '%s'", filenamebuffer);
    std::ifstream node2CoordFile(filenamebuffer);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(node2CoordFile);
    node2Coord = new IdTree<Coord>(in);
}

void saveNode2Coord() {
    char filenamebuffer[1024];
    if (node2Coord != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.n2c", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream node2CoordFile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(node2CoordFile);
        node2Coord->write(out);
    } else
        Error::err("Cannot save node2Coord, variable is NULL");
}

void loadNodeNames() {
    char filenamebuffer[1024];
    snprintf(filenamebuffer, 1024, "%s/%s.nn", tempdir, mapname);
    Error::debug("Reading from '%s'", filenamebuffer);
    std::ifstream nnfile(filenamebuffer);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(nnfile);
    nodeNames = new IdTree<WriteableString>(in);
}

void saveNodeNames() {
    char filenamebuffer[1024];
    if (nodeNames != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.nn", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream nnfile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(nnfile);
        nodeNames->write(out);
    } else
        Error::err("Cannot save nodeNames, variable is NULL");
}

void loadWayNames() {
    char filenamebuffer[1024];
    snprintf(filenamebuffer, 1024, "%s/%s.wn", tempdir, mapname);
    Error::debug("Reading from '%s'", filenamebuffer);
    std::ifstream wnfile(filenamebuffer);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(wnfile);
    wayNames = new IdTree<WriteableString>(in);
}

void saveWayNames() {
    char filenamebuffer[1024];
    if (wayNames != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.wn", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream wnfile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(wnfile);
        wayNames->write(out);
    } else
        Error::err("Cannot save wayNames, variable is NULL");
}

void loadRelationNames() {
    char filenamebuffer[1024];
    snprintf(filenamebuffer, 1024, "%s/%s.rn", tempdir, mapname);
    Error::debug("Reading from '%s'", filenamebuffer);
    std::ifstream rnfile(filenamebuffer);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(rnfile);
    relationNames = new IdTree<WriteableString>(in);
}

void saveRelationNames() {
    char filenamebuffer[1024];
    if (wayNames != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.rn", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream rnfile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(rnfile);
        relationNames->write(out);
    } else
        Error::err("Cannot save relationNames, variable is NULL");
}

void loadWayNodes() {
    char filenamebuffer[1024];
    snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, mapname);
    Error::debug("Reading from '%s'", filenamebuffer);
    std::ifstream wayNodeFile(filenamebuffer);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(wayNodeFile);
    wayNodes = new IdTree<WayNodes>(in);
}

void saveWayNodes() {
    char filenamebuffer[1024];
    if (wayNodes != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.w2n", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream wayNodeFile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(wayNodeFile);
        wayNodes->write(out);
    } else
        Error::err("Cannot save wayNodes, variable is NULL");
}

void loadRelMem() {
    char filenamebuffer[1024];
    snprintf(filenamebuffer, 1024, "%s/%s.relmem", tempdir, mapname);
    Error::debug("Reading from '%s'", filenamebuffer);
    std::ifstream relmemfile(filenamebuffer);
    relMembers = new IdTree<RelationMem>(relmemfile);
    relmemfile.close();
}

void saveRelMem() {
    char filenamebuffer[1024];
    if (relMembers != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.relmem", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream relmemfile(filenamebuffer);
        relMembers->write(relmemfile);
        relmemfile.close();
    } else
        Error::err("Cannot save relMembers, variable is NULL");
}

/**
 * Important: 'loadSweden()' cannot be run in parallel to the other
 * load functions, as it requires those data structures to be already
 * initialized.
 */
void loadSweden() {
    char filenamebuffer[1024];
    snprintf(filenamebuffer, 1024, "%s/%s.sweden", tempdir, mapname);
    Error::debug("Reading from '%s'", filenamebuffer);
    std::ifstream swedenfile(filenamebuffer);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(swedenfile);
    sweden = new Sweden(in);
}

void saveSweden() {
    char filenamebuffer[1024];
    if (sweden != NULL) {
        snprintf(filenamebuffer, 1024, "%s/%s.sweden", tempdir, mapname);
        Error::debug("Writing to '%s'", filenamebuffer);
        std::ofstream swedenfile(filenamebuffer);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(swedenfile);
        sweden->write(out);
    } else
        Error::err("Cannot save sweden, variable is NULL");
}

GlobalObjectManager::GlobalObjectManager() {
    swedishTextTree = NULL;
    node2Coord = NULL;
    wayNodes = NULL;
    relMembers = NULL;
    nodeNames = NULL;
    wayNames = NULL;
    relationNames = NULL;
    sweden = NULL;

    char filenamebuffer[1024];
    snprintf(filenamebuffer, 1024, "%s/%s.texttree", tempdir, mapname);
    if (testNonEmptyFile(filenamebuffer)) {
        load();
    } else if (testNonEmptyFile(osmpbffilename)) {
        std::ifstream fp(osmpbffilename, std::ifstream::in | std::ifstream::binary);
        if (fp) {
            Timer timer;
            OsmPbfReader osmPbfReader;
            osmPbfReader.parse(fp);
            /// Clean up the protobuf lib
            google::protobuf::ShutdownProtobufLibrary();
            fp.close();

            if (sweden != NULL)
                sweden->fixUnlabeledRegionalRoads();

            int64_t cputime, walltime;
            timer.elapsed(&cputime, &walltime);
            Error::info("Spent CPU time to parse .osm.pbf file: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);

            save();
        } else
            Error::err("Loading .osm.pbf file failed");
    } else
        Error::err("Can neither load internal files from /tmp, nor .osm.pbf file");

    /// Once loading from .osm.pbf file or own temporary files is done,
    /// perform some consistency checks
    sweden->test();
}

GlobalObjectManager::~GlobalObjectManager() {
    Error::debug("Going to shut down, free'ing memory");

    Timer timer;
    timer.start();
    if (swedishTextTree != NULL)
        delete swedishTextTree;
    if (node2Coord != NULL)
        delete node2Coord;
    if (nodeNames != NULL)
        delete nodeNames;
    if (wayNames != NULL)
        delete wayNames;
    if (relationNames != NULL)
        delete relationNames;
    if (wayNodes != NULL)
        delete wayNodes;
    if (relMembers != NULL)
        delete relMembers;
    if (sweden != NULL)
        delete sweden;
    int64_t cputime, walltime;
    timer.elapsed(&cputime, &walltime);
    Error::info("Spent CPU time to free memory: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
}

void GlobalObjectManager::load() {
    Timer timer;
    boost::thread threadLoadSwedishTextTree(loadSwedishTextTree);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadLoadNode2Cood(loadNode2Coord);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadLoadNodeNames(loadNodeNames);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadLoadWayNames(loadWayNames);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadLoadRelationNames(loadRelationNames);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadLoadWayNodes(loadWayNodes);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadLoadRelMem(loadRelMem);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    Error::debug("Waiting for load threads to join");
    threadLoadSwedishTextTree.join();
    threadLoadNode2Cood.join();
    threadLoadNodeNames.join();
    threadLoadWayNames.join();
    threadLoadRelationNames.join();
    threadLoadWayNodes.join();
    threadLoadRelMem.join();
    Error::debug("All load threads joined, now loading 'sweden'");
    loadSweden();
    int64_t cputime, walltime;
    timer.elapsed(&cputime, &walltime);
    Error::info("Spent CPU time to read files: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
}

void GlobalObjectManager::save() const {
    Timer timer;
    boost::thread threadSaveSwedishTextTree(saveSwedishTextTree);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadSaveNode2Cood(saveNode2Coord);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadSaveNodeNames(saveNodeNames);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadSaveWayNames(saveWayNames);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadSaveRelationNames(saveRelationNames);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadSaveWayNodes(saveWayNodes);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadSaveRelMem(saveRelMem);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    boost::thread threadSaveSweden(saveSweden);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    Error::debug("Waiting for save threads to join");
    threadSaveSwedishTextTree.join();
    threadSaveNode2Cood.join();
    threadSaveNodeNames.join();
    threadSaveWayNames.join();
    threadSaveRelationNames.join();
    threadSaveWayNodes.join();
    threadSaveRelMem.join();
    threadSaveSweden.join();
    Error::debug("All save threads joined");
    int64_t cputime, walltime;
    timer.elapsed(&cputime, &walltime);
    Error::info("Spent CPU time to write files: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
}

bool GlobalObjectManager::testNonEmptyFile(const std::string &filename, unsigned int minimumSize) {
    if (filename.empty()) return false;
    std::ifstream fileteststream(filename);
    if (fileteststream && fileteststream.good()) {
        fileteststream.seekg(0, fileteststream.end);
        const std::istream::pos_type length = fileteststream.tellg();
        fileteststream.seekg(0, fileteststream.beg);
        fileteststream.close();
        if (length >= minimumSize) return true; ///< file can be read and is at least 16 bytes large
    }

    return false; ///< fail by default
}

PidFile::PidFile(const char *pidfilename)
    : m_pidfilename(pidfilename) {
    if (m_pidfilename == NULL || m_pidfilename[0] == '\0') {
        /// Invalid pidfilename
        Error::err("Invalid pidfilename");
        return /*false*/;
    }
    FILE *pidfile = fopen(m_pidfilename, "w");
    if (pidfile == NULL) {
        /// Cannot open/write to pidfile
        Error::err("Cannot open/write to pidfile: %s", m_pidfilename);
        return /*false*/;
    }

    const int pid = getpid();
    if (fprintf(pidfile, "%d\n", pid) <= 0) {
        /// Could not write number to pidfile
        Error::err("Could not write number to pidfile: %s", m_pidfilename);
        fclose(pidfile);
        return /*false*/;
    }

    fclose(pidfile);

    Error::info("Created PID file in '%s', PID is %d", pidfilename, pid);
    return /*true*/;
}

PidFile::~PidFile() {
    unlink(m_pidfilename);
}
