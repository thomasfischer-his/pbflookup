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
#include <fstream>

#include <sys/socket.h>
#include <netinet/ip.h>

#include "global.h"
#include "globalobjects.h"
#include "config.h"
#include "httpserver.h"
#include "testset.h"

inline bool ends_with(std::string const &value, std::string const &ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void init_rand() {
    FILE *devrandom = fopen("/dev/urandom", "r");
    unsigned int seed = time(NULL) ^ (getpid() << 8);
    if (devrandom != NULL) {
        fread((void *)&seed, sizeof(seed), 1, devrandom);
        fclose(devrandom);
    }

    Error::debug("seed=%08x", seed);
    srand(seed);
}

int main(int argc, char *argv[]) {
#ifdef DEBUG
    Error::debug("DEBUG flag enabled");
#endif // DEBUG
    init_rand();

    const char *defaultconfigfile = "sweden.config";
    if (!init_configuration((argc < 2) ? defaultconfigfile : argv[argc - 1])) {
        Error::err("Cannot continue without properly parsing configuration file");
        return 1;
    }

    /// Omit debug output in server mode
    if (server_mode() && minimumLoggingLevel < LevelInfo) minimumLoggingLevel = LevelInfo;

    /// std::unique_ptr will take care of destroying the unique instance of
    /// GlobalObjectManager when this function exists.
    /// Note: 'gom' is not used correctly. Rather, it will initialize various
    /// global variables/objects during creation and free those global variables/
    /// objects during its destruction.
    std::unique_ptr<PidFile> pidFile(new PidFile(pidfilename));
    std::unique_ptr<GlobalObjectManager> gom(new GlobalObjectManager());

    if (relMembers != NULL && wayNodes != NULL && node2Coord != NULL && nodeNames != NULL && wayNames != NULL && relationNames != NULL && swedishTextTree != NULL && sweden != NULL) {
        serverSocket = http_port < 1024 ? -1 : socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (serverSocket >= 0) {
            HTTPServer httpServer;
            httpServer.run();
            close(serverSocket);
        } else if (!testsets.empty()) {
            Testset testsetRunner;
            testsetRunner.run();
        }
    } else
        Error::err("No all variables got initialized correctly: relMembers, wayNodes, node2Coord, nodeNames, wayNames, relationNames, swedishTextTree, sweden");

    return 0;
}

