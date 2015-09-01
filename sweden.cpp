#include "sweden.h"

#include <iostream>
#include <fstream>

#include "error.h"

class Sweden::Private {
private:
    Sweden *p;

public:
    IdTree<Coord> *coords;
    IdTree<WayNodes> *waynodes;
    IdTree<RelationMem> *relmem;

    double min_lat = 1000.0, min_lon = 1000.0, max_lat = -1000.0, max_lon = -1000.0;

    explicit Private(Sweden *parent, IdTree<Coord> *_coords, IdTree<WayNodes> *_waynodes, IdTree<RelationMem> *_relmem)
        : p(parent), coords(_coords), waynodes(_waynodes), relmem(_relmem) {
        /// nothing
    }
};

Sweden::Sweden(IdTree<Coord> *coords, IdTree<WayNodes> *waynodes, IdTree<RelationMem> *relmem)
    : d(new Sweden::Private(this, coords, waynodes, relmem))
{
    char filenamebuffer[1024];
    snprintf(filenamebuffer, 1024, "%s/git/pbflookup/scb-lan-kommuner-kod.csv", getenv("HOME"));
    std::ifstream fp(filenamebuffer, std::ifstream::in | std::ifstream::binary);
    if (fp) {
        std::map<int, Land>::iterator nextLand = lands.begin();
        std::string codeStr;
        while (std::getline(fp, codeStr, ';')) {
            const int code = std::stoi(codeStr);
            std::string label;
            std::getline(fp, label);

            if (code >= 100) {
                /// Municipality
                const int land = code / 100;
                lands[land].municipalities.insert(lands[land].municipalities.begin(), std::pair<int, std::string>(code, label));
            } else if (code > 0) {
                /// Land
                Land land(label);
                nextLand = lands.insert(nextLand, std::pair<int, Land>(code, land));
            } else
                Error::err("Invalid code for label '%s'", label.c_str());
        }
        fp.close();
    } else
        Error::err("Could not open list of muncipalities and lands: %s", filenamebuffer);
}

Sweden::~Sweden()
{
    /// nothing
}

void Sweden::setMinMaxLatLon(double min_lat, double min_lon, double max_lat, double max_lon) {
    d->min_lat = min_lat;
    d->max_lat = max_lat;
    d->min_lon = min_lon;
    d->max_lon = max_lon;
}

void Sweden::dump() {
    for (std::map<int, Land>::const_iterator itLand = lands.cbegin(); itLand != lands.cend(); ++itLand) {
        Error::info("Land=%02i %s", (*itLand).first, (*itLand).second.label.c_str());
        for (std::map<int, std::string>::const_iterator itMun = (*itLand).second.municipalities.cbegin(); itMun != (*itLand).second.municipalities.cend(); ++itMun) {
            Error::info("  Municipality=%04i %s", (*itMun).first, (*itMun).second.c_str());
        }
    }
}

void Sweden::insertSCBcode(const int code, uint64_t relid) {
    scbcode_to_relationid.insert(std::pair<int, uint64_t>(code, relid));
    Error::debug("Adding SCB code %i using relation %llu", code, relid);
}

int Sweden::insideSCBcode(uint64_t nodeid) {
    static const int INT_RANGE = 0x00ffffff;
    const double delta_lat = d->max_lat - d->min_lat;
    const double delta_lon = d->max_lon - d->min_lon;

    if (scbcode_to_polygon.empty()) {
        for (std::map<int, uint64_t>::const_iterator it = scbcode_to_relationid.cbegin(); it != scbcode_to_relationid.cend(); ++it) {
            const uint64_t relid = (*it).second;
            RelationMem rel;
            if (d->relmem->retrieve(relid, rel)) {
                std::vector<std::pair<int, int> > polygon;
                for (uint32_t i = 0; i < rel.num_members; ++i) {
                    WayNodes wn;
                    if (d->waynodes->retrieve(rel.members[i], wn)) {
                        for (uint32_t j = 0; j < wn.num_nodes; ++j) {
                            Coord coord;
                            if (d->coords->retrieve(wn.nodes[j], coord)) {
                                const int lat = (coord.lat - d->min_lat) * INT_RANGE / delta_lat;
                                const int lon = (coord.lon - d->min_lon) * INT_RANGE / delta_lon;
                                polygon.push_back(std::pair<int, int>(lat, lon));
                            }
                        }
                    }
                }
                scbcode_to_polygon.insert(std::pair<int, std::vector<std::pair<int, int> > >((*it).first, polygon));
            }
        }
    }

    Coord coord;
    if (d->coords->retrieve(nodeid, coord)) {
        const int x = (coord.lon - d->min_lon) * INT_RANGE / delta_lon;
        const int y = (coord.lat - d->min_lat) * INT_RANGE / delta_lat;

        for (std::map<int, std::vector<std::pair<int, int> > >::const_iterator it = scbcode_to_polygon.cbegin(); it != scbcode_to_polygon.cend(); ++it) {
            const std::vector<std::pair<int, int> > &polygon = (*it).second;
            const int polyCorners = polygon.size();
            int j = polyCorners - 1;
            bool oddNodes = false;

            for (int i = 0; i < polyCorners; i++) {
                if (((polygon[i].first < y && polygon[j].first >= y) || (polygon[j].first < y && polygon[i].first >= y)) && (polygon[i].second <= x || polygon[j].second <= x)) {
                    oddNodes ^= (polygon[i].second + (y - polygon[i].first) * (polygon[j].second - polygon[i].second) / (double)(polygon[j].first - polygon[i].first) < x);
                }
                j = i;
            }

            if (oddNodes)
                return (*it).first;
        }
    }

    return -1;
}

void Sweden::insertNUTS3code(const int code, uint64_t relid) {
    nuts3code_to_relationid.insert(std::pair<int, uint64_t>(code, relid));
    Error::debug("Adding NUTS3 code %i using relation %llu", code, relid);
}
