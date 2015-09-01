#ifndef SWEDEN_H
#define SWEDEN_H

#include <string>
#include <map>

#include "idtree.h"

class Sweden
{
public:
    explicit Sweden();
    ~Sweden();

    void dump();

    void insertSCBcode(const int code, uint64_t relid);
    int insideSCBcode(uint64_t nodeid, IdTree<RelationMem> *relmem,  IdTree<WayNodes> *waynodes,  IdTree<Coord> *coord, double min_lat, double min_lon, double max_lat, double max_lon);
    void insertNUTS3code(const int code, uint64_t relid);

private:

    struct Land {
        Land(const std::string &_label)
            : label(_label) {}
        Land() {}

        std::string label;
        std::map<int, std::string> municipalities;
    };
    std::map<int, Land> lands;

    std::map<int, uint64_t> scbcode_to_relationid;
    std::map<int, std::vector<std::pair<int, int> > > scbcode_to_polygon;
    std::map<int, uint64_t> nuts3code_to_relationid;
};

#endif // SWEDEN_H