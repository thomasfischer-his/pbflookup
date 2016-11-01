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

#include <cstdlib>
#include <fstream>

#include <sys/socket.h>
#include <netinet/ip.h>
#include <unistd.h>

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
    unsigned int seed = time(NULL) ^ (getpid() << 8);

    std::ifstream devrandom("/dev/urandom");
    if (devrandom.good())
        devrandom.read((char *)&seed, sizeof(seed));

    srand(seed);
}

bool debugged_with_gdb() {
    /// Check only once if running inside debugger (gdb),
    /// memorize result in static variable for later calls
    /// Values for 'status':
    ///   0: status not yet determined
    ///   1: inside gdb
    ///  -1: not being debugged with gdb
    static int status = 0;
    if (status == 0) {
        /// Running inside debugger means that parent process's
        /// command line ends with "/gdb"
        static const size_t buffer_size = 128;
        const pid_t parent_pid = getppid();
        char procfilename[buffer_size];
        snprintf(procfilename, buffer_size - 1, "/proc/%d/cmdline", parent_pid);
        std::ifstream procfile(procfilename);
        if (procfile.good()) {
            std::string buffer;
            std::getline(procfile, buffer);
            const std::string::size_type len = buffer.length();
            if (!procfile.bad() && len > 4)
                status = buffer[len] == '\0' && buffer[len - 1] == 'b' && buffer[len - 2] == 'd' && buffer[len - 3] == 'g' && buffer[len - 4] == '/' ? 1 : -1;
        }
    }

    return status > 0;
}

bool file_exists_readable(const char *filename) {
    std::ifstream f(filename);
    return f.good();
}

int main(int argc, char *argv[]) {
    if (getuid() == 0)
        Error::err("This program should never be run as root!");

#ifdef DEBUG
    Error::debug("DEBUG flag enabled");
#endif // DEBUG
    init_rand();

    char configfile[maxStringLen];
    memset(configfile, 0, maxStringLen);
    if (argc >= 2) {
        if (argv[argc - 1][0] != '/') {
            getcwd(configfile, maxStringLen / 2 - 10);
            strncat(configfile, "/", 1);
        }
        strncat(configfile, argv[argc - 1], maxStringLen / 2 - 10);
        if (strstr(argv[argc - 1], ".config") == NULL)
            strncat(configfile, ".config", maxStringLen - strlen(configfile) - 2);
    } else {
        getcwd(configfile, maxStringLen - 20);
        strncat(configfile, "/sweden.config", maxStringLen - strlen(configfile) - 2);

    }
    if (!file_exists_readable(configfile))
        Error::err("Provided configuration file '%s' does not exist or is not readable", configfile);
    if (!init_configuration(configfile))
        Error::err("Cannot continue without properly parsing configuration file '%s'", configfile);

    /// Omit debug output if in server mode and not attached to terminal and not debugged,
    /// i.e. when started as a systemd service
    if (server_mode() && !isatty(1) && !debugged_with_gdb() && minimumLoggingLevel < LevelInfo) minimumLoggingLevel = LevelInfo;

    PidFile pidfile;
    GlobalObjectManager gom;

    if (relMembers != nullptr && wayNodes != nullptr && node2Coord != nullptr && nodeNames != nullptr && wayNames != nullptr && relationNames != nullptr && swedishTextTree != nullptr && sweden != nullptr) {
        serverSocket = server_mode() ? socket(PF_INET, SOCK_STREAM, IPPROTO_TCP) : -1;
        if (serverSocket >= 0) {
            HTTPServer httpServer;
            httpServer.run();
            close(serverSocket);
        } else if (!testsets.empty()) {
            Testset testsetRunner;
            testsetRunner.run();
        } else
            Error::warn("Running neither HTTP server nor testset (none is configured)");
    } else
        Error::err("No all variables got initialized correctly: relMembers, wayNodes, node2Coord, nodeNames, wayNames, relationNames, swedishTextTree, sweden");

    return 0;
}

