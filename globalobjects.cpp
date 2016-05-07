/***************************************************************************
 *   Copyright (C) 2015-2016 by Thomas Fischer <thomas.fischer@his.se>     *
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
    const std::string filename = tempdir + "/" + mapname + ".texttree";
    Error::debug("Reading from '%s' (mapping text to element ids)", filename.c_str());
    std::ifstream swedishtexttreefile(filename);
    swedishTextTree = new SwedishTextTree(swedishtexttreefile);
    swedishtexttreefile.close();
}

void saveSwedishTextTree() {
    if (swedishTextTree != NULL) {
        const std::string filename = tempdir + "/" + mapname + ".texttree";
        Error::debug("Writing to '%s' (mapping text to element ids)", filename.c_str());
        std::ofstream swedishtexttreefile(filename);
        swedishTextTree->write(swedishtexttreefile);
        swedishtexttreefile.close();
    } else
        Error::err("Cannot save swedishTextTree, variable is NULL");
}

void loadNode2Coord() {
    const std::string filename = tempdir + "/" + mapname + ".n2c";
    Error::debug("Reading from '%s' (mapping nodes to coordinates)", filename.c_str());
    std::ifstream node2CoordFile(filename);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(node2CoordFile);
    node2Coord = new IdTree<Coord>(in);
}

void saveNode2Coord() {
    if (node2Coord != NULL) {
        const std::string filename = tempdir + "/" + mapname + ".n2c";
        Error::debug("Writing to '%s' (mapping nodes to coordinates)", filename.c_str());
        std::ofstream node2CoordFile(filename);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(node2CoordFile);
        node2Coord->write(out);
    } else
        Error::err("Cannot save node2Coord, variable is NULL");
}

void loadNodeNames() {
    const std::string filename = tempdir + "/" + mapname + ".nn";
    Error::debug("Reading from '%s' (mapping nodes to their names)", filename.c_str());
    std::ifstream nnfile(filename);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(nnfile);
    nodeNames = new IdTree<WriteableString>(in);
}

void saveNodeNames() {
    if (nodeNames != NULL) {
        const std::string filename = tempdir + "/" + mapname + ".nn";
        Error::debug("Writing to '%s' (mapping nodes to their names)", filename.c_str());
        std::ofstream nnfile(filename);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(nnfile);
        nodeNames->write(out);
    } else
        Error::err("Cannot save nodeNames, variable is NULL");
}

void loadWayNames() {
    const std::string filename = tempdir + "/" + mapname + ".wn";
    Error::debug("Reading from '%s' (mapping ways to their names)", filename.c_str());
    std::ifstream wnfile(filename);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(wnfile);
    wayNames = new IdTree<WriteableString>(in);
}

void saveWayNames() {
    if (wayNames != NULL) {
        const std::string filename = tempdir + "/" + mapname + ".wn";
        Error::debug("Writing to '%s' (mapping ways to their names)", filename.c_str());
        std::ofstream wnfile(filename);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(wnfile);
        wayNames->write(out);
    } else
        Error::err("Cannot save wayNames, variable is NULL");
}

void loadRelationNames() {
    const std::string filename = tempdir + "/" + mapname + ".rn";
    Error::debug("Reading from '%s' (mapping relations to their names)", filename.c_str());
    std::ifstream rnfile(filename);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(rnfile);
    relationNames = new IdTree<WriteableString>(in);
}

void saveRelationNames() {
    if (wayNames != NULL) {
        const std::string filename = tempdir + "/" + mapname + ".rn";
        Error::debug("Writing to '%s' (mapping relations to their names)", filename.c_str());
        std::ofstream rnfile(filename);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(rnfile);
        relationNames->write(out);
    } else
        Error::err("Cannot save relationNames, variable is NULL");
}

void loadWayNodes() {
    const std::string filename = tempdir + "/" + mapname + ".w2n";
    Error::debug("Reading from '%s' (mapping ways to nodes they span over)", filename.c_str());
    std::ifstream wayNodeFile(filename);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(wayNodeFile);
    wayNodes = new IdTree<WayNodes>(in);
}

void saveWayNodes() {
    if (wayNodes != NULL) {
        const std::string filename = tempdir + "/" + mapname + ".w2n";
        Error::debug("Writing to '%s' (mapping ways to nodes they span over)", filename.c_str());
        std::ofstream wayNodeFile(filename);
        boost::iostreams::filtering_ostream out;
        out.push(boost::iostreams::gzip_compressor());
        out.push(wayNodeFile);
        wayNodes->write(out);
    } else
        Error::err("Cannot save wayNodes, variable is NULL");
}

void loadRelMem() {
    const std::string filename = tempdir + "/" + mapname + ".relmem";
    Error::debug("Reading from '%s' (mapping relations to their members)", filename.c_str());
    std::ifstream relmemfile(filename);
    relMembers = new IdTree<RelationMem>(relmemfile);
    relmemfile.close();
}

void saveRelMem() {
    if (relMembers != NULL) {
        const std::string filename = tempdir + "/" + mapname + ".relmem";
        Error::debug("Writing to '%s' (mapping relations to their members)", filename.c_str());
        std::ofstream relmemfile(filename);
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
    const std::string filename = tempdir + "/" + mapname + ".sweden";
    Error::debug("Reading from '%s'", filename.c_str());
    std::ifstream swedenfile(filename);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(swedenfile);
    sweden = new Sweden(in);
}

void saveSweden() {
    if (sweden != NULL) {
        const std::string filename = tempdir + "/" + mapname + ".sweden";
        Error::debug("Writing to '%s'", filename.c_str());
        std::ofstream swedenfile(filename);
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

    const std::string filename = tempdir + "/" + mapname + ".texttree";
    if (testNonEmptyFile(filename)) {
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
}

GlobalObjectManager::~GlobalObjectManager() {
#define delete_and_set_NULL(a) { delete a; a=NULL; }
    Error::debug("Going to shut down, free'ing memory");

    Timer timer;
    timer.start();
    if (swedishTextTree != NULL)
        delete_and_set_NULL(swedishTextTree);
    if (node2Coord != NULL)
        delete_and_set_NULL(node2Coord);
    if (nodeNames != NULL)
        delete_and_set_NULL(nodeNames);
    if (wayNames != NULL)
        delete_and_set_NULL(wayNames);
    if (relationNames != NULL)
        delete_and_set_NULL(relationNames);
    if (wayNodes != NULL)
        delete_and_set_NULL(wayNodes);
    if (relMembers != NULL)
        delete_and_set_NULL(relMembers);
    if (sweden != NULL)
        delete_and_set_NULL(sweden);
    int64_t cputime, walltime;
    timer.elapsed(&cputime, &walltime);
    Error::info("Spent CPU time to free memory: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
}

void GlobalObjectManager::load() {
    Timer timer;
    try
    {
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
    } catch (std::exception const &ex) {
        Error::err("Exception during thread processing while loading from files: %s", ex.what());
    }

    Error::debug("All load threads joined, now loading 'sweden'");
    try
    {
        loadSweden();
    } catch (std::exception const &ex) {
        Error::err("Exception during loading data from files: %s", ex.what());
    }
    int64_t cputime, walltime;
    timer.elapsed(&cputime, &walltime);
    Error::info("Spent CPU time to read files: %.1fms == %.1fs  (wall time: %.1fms == %.1fs)", cputime / 1000.0, cputime / 1000000.0, walltime / 1000.0, walltime / 1000000.0);
}

void GlobalObjectManager::save() const {
    Timer timer;
    try
    {
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
    } catch (std::exception const &ex) {
        Error::err("Exception during thread processing while saving to files: %s", ex.what());
    }
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

PidFile::PidFile()
{
    if (pidfilename.empty()) {
        /// Invalid pidfilename
        Error::err("Invalid pidfilename");
        return /*false*/;
    }

    std::ofstream pidfile(pidfilename);
    if (!pidfile.good()) {
        /// Cannot open/write to pidfile
        Error::err("Cannot open/write to pidfile: %s", pidfilename.c_str());
    }

    const int pid = getpid();
    pidfile << pid << std::endl;
    if (!pidfile.good()) {
        /// Could not write number to pidfile
        Error::err("Could not write number to pidfile: %s", pidfilename.c_str());
    }

    pidfile.close();

    Error::info("Created PID file in '%s', PID is %d", pidfilename.c_str(), pid);
}

PidFile::~PidFile() {
    unlink(pidfilename.c_str());
}
