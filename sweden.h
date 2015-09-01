#ifndef SWEDEN_H
#define SWEDEN_H

#include <string>
#include <map>

#include "idtree.h"

class Sweden
{
public:
    explicit Sweden(IdTree<Coord> *coords, IdTree<WayNodes> *waynodes, IdTree<RelationMem> *relmem);
    ~Sweden();

    void setMinMaxLatLon(double min_lat, double min_lon, double max_lat, double max_lon);

    void dump();

    void insertSCBcode(const int code, uint64_t relid);
    int insideSCBcode(uint64_t nodeid);
    void insertNUTS3code(const int code, uint64_t relid);
    int insideNUTS3code(uint64_t nodeid);

private:

    struct Land {
        Land(const std::string &_label)
            : label(_label) {}
        Land() {}

        std::string label;
        std::map<int, std::string> municipalities;
    };
    std::map<int, Land> lands;

private:
    class Private;
    Private *const d;
};

#endif // SWEDEN_H
