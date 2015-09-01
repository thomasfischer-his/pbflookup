#ifndef SWEDEN_H
#define SWEDEN_H

#include <string>
#include <map>

#include "idtree.h"

class Sweden
{
public:
    explicit Sweden(IdTree<Coord> *coords, IdTree<WayNodes> *waynodes, IdTree<RelationMem> *relmem);
    explicit Sweden(std::istream &input, IdTree<Coord> *coords, IdTree<WayNodes> *waynodes, IdTree<RelationMem> *relmem);
    ~Sweden();

    void setMinMaxLatLon(double min_lat, double min_lon, double max_lat, double max_lon);

    void dump();

    std::ostream &write(std::ostream &output);

    void insertSCBcode(const int code, uint64_t relid);
    int insideSCBcode(uint64_t nodeid);
    void insertNUTS3code(const int code, uint64_t relid);
    int insideNUTS3code(uint64_t nodeid);

private:
    class Private;
    Private *const d;
};

#endif // SWEDEN_H
