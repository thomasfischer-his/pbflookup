#include "sweden.h"

#include <iostream>
#include <fstream>
#include <deque>

#include "error.h"

class Sweden::Private {
private:
    static const int INT_RANGE;

    Sweden *p;

    struct Land {
        Land(const std::string &_label)
            : label(_label) {}
        Land() {}

        std::string label;
        std::map<int, std::string> municipalities;
    };
    std::map<int, Land> lands;

    inline int lat_to_int(const double &lat) {
        return (lat - min_lat) * INT_RANGE / delta_lat + 0.5;
    }

    inline double int_to_lat(const int &lat) {
        return ((lat - 0.5) * delta_lat / INT_RANGE) + min_lat;
    }

    inline int lon_to_int(const double &lon) {
        return (lon - min_lon) * INT_RANGE / delta_lon + 0.5;
    }

    inline double int_to_lon(const int &lon) {
        return ((lon - 0.5) * delta_lon / INT_RANGE) + min_lon;
    }


public:
    double min_lat = 1000.0, min_lon = 1000.0, max_lat = -1000.0, max_lon = -1000.0;
    double delta_lat = 0.0, delta_lon = 0.0;

    IdTree<Coord> *coords;
    IdTree<WayNodes> *waynodes;
    IdTree<RelationMem> *relmem;

    std::map<int, uint64_t> scbcode_to_relationid, nuts3code_to_relationid;
    std::map<int, std::deque<std::pair<int, int> > > scbcode_to_polygon, nuts3code_to_polygon;

    explicit Private(Sweden *parent, IdTree<Coord> *_coords, IdTree<WayNodes> *_waynodes, IdTree<RelationMem> *_relmem)
        : p(parent), coords(_coords), waynodes(_waynodes), relmem(_relmem) {
        /// nothing
    }

    /**
     * Adding the nodes contained in a WayNodes object to an existing
     * polygon (list of nodes). Emphasis is put on adding a WayNode's
     * nodes only if this way can be attached to the polygon,
     * otherwise 'false' is returned and another WayNodes object may
     * be tested for attachment.
     * If the WayNodes object's nodes can be attached, they will be
     * attached to the right end and in the right order to match the
     * polygon.
     * If the polygon is empty, the WayNodes object will be inserted
     * completely into the polygon.
     */
    bool addWayToPolygon(const WayNodes &wn, std::deque<std::pair<int, int> > &polygon) {
        if (polygon.empty()) {
            /// Always add first way completely
            for (uint32_t j = 0; j < wn.num_nodes; ++j) {
                const uint64_t nodeid = wn.nodes[j];
                Coord coord;
                if (coords->retrieve(nodeid, coord)) {
                    const int lat = lat_to_int(coord.lat);
                    const int lon = lon_to_int(coord.lon);
                    polygon.push_back(std::pair<int, int>(lat, lon));
                } else
                    Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[j]);
            }
            return true;
        }

        /// Retrieve the coordinates of one end of the way
        /// as described by the WayNodes object
        Coord coord;
        if (!coords->retrieve(wn.nodes[0], coord)) {
            Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[0]);
            return false;
        }
        const int firstlat = lat_to_int(coord.lat);
        const int firstlon = lon_to_int(coord.lon);

        /// Test if the way can be attached to one side of the (growing) polygon
        if (polygon[0].first == firstlat && polygon[0].second == firstlon) {
            for (uint32_t j = 1; j < wn.num_nodes; ++j) {
                if (coords->retrieve(wn.nodes[j], coord)) {
                    const int lat = lat_to_int(coord.lat);
                    const int lon = lon_to_int(coord.lon);
                    polygon.push_front(std::pair<int, int>(lat, lon));
                } else
                    Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[j]);
            }
            return true;
        } else
            /// Test if the way can be attached to other side of the (growing) polygon
            if (polygon[polygon.size() - 1].first == firstlat && polygon[polygon.size() - 1].second == firstlon) {
                for (uint32_t j = 1; j < wn.num_nodes; ++j) {
                    if (coords->retrieve(wn.nodes[j], coord)) {
                        const int lat = lat_to_int(coord.lat);
                        const int lon = lon_to_int(coord.lon);
                        polygon.push_back(std::pair<int, int>(lat, lon));
                    } else
                        Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[j]);
                }
                return true;
            }

        /// Retrieve the coordinates of the other end of the way
        /// as described by the WayNodes object
        if (!coords->retrieve(wn.nodes[wn.num_nodes - 1], coord)) {
            Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[wn.num_nodes - 1]);
            return false;
        }
        const int lastlat = lat_to_int(coord.lat);
        const int lastlon = lon_to_int(coord.lon);

        /// Test if the way can be attached to one side of the (growing) polygon
        if (polygon[0].first == lastlat && polygon[0].second == lastlon) {
            for (int j = wn.num_nodes - 2; j >= 0; --j) {
                if (coords->retrieve(wn.nodes[j], coord)) {
                    const int lat = lat_to_int(coord.lat);
                    const int lon = lon_to_int(coord.lon);
                    polygon.push_front(std::pair<int, int>(lat, lon));
                } else
                    Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[j]);
            }
            return true;
        } else
            /// Test if the way can be attached to other side of the (growing) polygon
            if (polygon[polygon.size() - 1].first == lastlat && polygon[polygon.size() - 1].second == lastlon) {
                for (int j = wn.num_nodes - 2; j >= 0; --j) {
                    if (coords->retrieve(wn.nodes[j], coord)) {
                        const int lat = lat_to_int(coord.lat);
                        const int lon = lon_to_int(coord.lon);
                        polygon.push_back(std::pair<int, int>(lat, lon));
                    } else
                        Error::warn("Cannot retrieve coordinates for node %llu", wn.nodes[j]);
                }
                return true;
            }

        /// Way could not attached to either side of the polygon
        /*
                Error::debug("firstlat=%.5f  firstlon=%.5f", int_to_lat(firstlat), int_to_lon(firstlon));
                Error::debug("lastlat=%.5f  lastlon=%.5f", int_to_lat(firstlat), int_to_lon(lastlon));
                Error::debug("polygon[0].first=%.5f  polygon[0].second=%.5f", int_to_lat(polygon[0].first), int_to_lon(polygon[0].second));
                Error::debug("polygon[polygon.size() - 1].first=%.5f  polygon[polygon.size() - 1].second=%.5f", int_to_lat(polygon[polygon.size() - 1].first), int_to_lon(polygon[polygon.size() - 1].second));
                */
        return false;
    }

    int nodeIdToCode(uint64_t nodeid, const std::map<int, uint64_t> &code_to_relationid, std::map<int, std::deque<std::pair<int, int> > > &code_to_polygon) {
        if (code_to_polygon.empty()) {
            for (std::map<int, uint64_t>::const_iterator it = code_to_relationid.cbegin(); it != code_to_relationid.cend(); ++it) {
                const uint64_t relid = (*it).second;
                RelationMem rel;
                if (relmem->retrieve(relid, rel) && rel.num_members > 0) {
                    std::deque<std::pair<int, int> > polygon;

                    bool *wayattached = new bool[rel.num_members];
                    uint32_t expected_outer_members = 0;
                    for (int i = rel.num_members - 1; i >= 0; --i) {
                        wayattached[i] = false;
                        if ((rel.member_flags[i] & RelationFlags::RoleOuter) > 0) ++expected_outer_members;
                    }
                    Error::debug("expected_outer_members=%i  rel.num_members=%i", expected_outer_members, rel.num_members);

                    uint32_t successful_additions = 0;
                    /// 'wrap around' is neccessary, as multiple iterations over the set of ways
                    /// may be required to identify all ways in the correct order for insertion
                    for (uint32_t wrap_around = 0; successful_additions < expected_outer_members && wrap_around < rel.num_members + 5; ++wrap_around)
                        for (uint32_t i = 0; i < rel.num_members && successful_additions < expected_outer_members; ++i) {
                            if (wayattached[i]) continue;
                            if ((rel.member_flags[i] & RelationFlags::RoleOuter) == 0) continue; ///< consider only members of role 'outer'

                            WayNodes wn;
#ifdef DEBUG
                            Coord coord;
#endif // DEBUG
                            const uint64_t memid = rel.member_ids[i];
                            if (waynodes->retrieve(memid, wn)) {
                                if (addWayToPolygon(wn, polygon)) {
                                    ++successful_additions;
                                    wayattached[i] = true;
                                }
                            }
#ifdef DEBUG
                            else if (coords->retrieve(rel.member_ids[i], coord)) {
                                /// ignoring node ids
                            } else {
                                /// Warn about member ids that are not ways (and not nodes)
                                Error::warn("Id %llu is member of relation %llu, but no way with this id is not known", rel.member_ids[i], relid);
                            }
#endif // DEBUG
                        }

                    if (successful_additions < expected_outer_members) {
                        Error::warn("The following ways could not be attached to polygon for relation %llu (%i<%i)", relid, successful_additions, expected_outer_members);
                        Error::warn("Polyon start: %.5f, %.5f", int_to_lat((*polygon.cbegin()).first), int_to_lon((*polygon.cbegin()).second));
                        Error::warn("Polyon end: %.5f, %.5f", int_to_lat((*(--polygon.cend())).first), int_to_lon((*(--polygon.cend())).second));
                        for (int i = rel.num_members - 1; i >= 0; --i)
                            if (!wayattached[i]) {
                                Error::warn("  Way %llu", rel.member_ids[i]);
                                WayNodes wn;
                                if (waynodes->retrieve(rel.member_ids[i], wn)) {
                                    Coord coord;
                                    if (coords->retrieve(wn.nodes[0], coord)) {
                                        Error::warn("    Start %.5f, %.5f", coord.lat, coord.lon);
                                    }
                                    if (coords->retrieve(wn.nodes[wn.num_nodes - 1], coord)) {
                                        Error::warn("    End %.5f, %.5f", coord.lat, coord.lon);
                                    }
                                }
                            }
                    }

                    delete[] wayattached;

                    if (successful_additions == expected_outer_members) {
                        /// If all members of the relation could be attached
                        /// to the polygon, the relation is contained completely
                        /// in the geographic database and as such may be considered
                        /// in the following analysis
                        polygon.pop_back();
                        code_to_polygon.insert(std::pair<int, std::deque<std::pair<int, int> > >((*it).first, polygon));
                    } else
                        Error::info("Could not insert relation %llu, not all ways found/known?", relid);
                }
            }
        }

        Coord coord;
        if (coords->retrieve(nodeid, coord)) {
            const int x = lon_to_int(coord.lon);
            const int y = lat_to_int(coord.lat);

            for (std::map<int, std::deque<std::pair<int, int> > >::const_iterator it = code_to_polygon.cbegin(); it != code_to_polygon.cend(); ++it) {
                /// For a good explanation, see here: http://alienryderflex.com/polygon/
                const std::deque<std::pair<int, int> > &polygon = (*it).second;
                const int polyCorners = polygon.size();
                int j = polyCorners - 1;
                bool oddNodes = false;

                for (int i = 0; i < polyCorners; i++) {
                    if (((polygon[i].first < y && polygon[j].first >= y) || (polygon[j].first < y && polygon[i].first >= y)) && (polygon[i].second <= x || polygon[j].second <= x)) {
                        oddNodes ^= polygon[i].second + (y - polygon[i].first) * (polygon[j].second - polygon[i].second) / (double)(polygon[j].first - polygon[i].first) < x;
                    }
                    j = i;
                }

                if (oddNodes)
                    return (*it).first;
            }
        }

        return -1;
    }

    void loadSCBcodeNames() {
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

    void dumpSCBcodeNames()const {
        for (std::map<int, Land>::const_iterator itLand = lands.cbegin(); itLand != lands.cend(); ++itLand) {
            Error::info("Land=%02i %s", (*itLand).first, (*itLand).second.label.c_str());
            for (std::map<int, std::string>::const_iterator itMun = (*itLand).second.municipalities.cbegin(); itMun != (*itLand).second.municipalities.cend(); ++itMun) {
                Error::info("  Municipality=%04i %s", (*itMun).first, (*itMun).second.c_str());
            }
        }
    }
};

const int Sweden::Private::INT_RANGE = 0x3fffffff;

Sweden::Sweden(IdTree<Coord> *coords, IdTree<WayNodes> *waynodes, IdTree<RelationMem> *relmem)
    : d(new Sweden::Private(this, coords, waynodes, relmem))
{
    d->loadSCBcodeNames();
}

Sweden::Sweden(std::istream &input, IdTree<Coord> *coords, IdTree<WayNodes> *waynodes, IdTree<RelationMem> *relmem)
    : d(new Sweden::Private(this, coords, waynodes, relmem))
{
    d->loadSCBcodeNames();

    char chr = '\0';
    input.read((char *)&chr, sizeof(chr));
    if (chr == 'S') {
        size_t  num_elements;
        input.read((char *)&num_elements, sizeof(num_elements));
        for (size_t i = 0; i < num_elements; ++i) {
            int code;
            input.read((char *)&code, sizeof(int));
            uint64_t nodeid;
            input.read((char *)&nodeid, sizeof(uint64_t));
            d->scbcode_to_relationid.insert(std::pair<int, uint64_t>(code, nodeid));
        }
    } else
        Error::warn("Expected 'S', got '0x%02x'", chr);

    input.read((char *)&chr, sizeof(chr));
    if (chr == 'n') {
        size_t  num_elements;
        input.read((char *)&num_elements, sizeof(num_elements));
        for (size_t i = 0; i < num_elements; ++i) {
            int code;
            input.read((char *)&code, sizeof(int));
            uint64_t nodeid;
            input.read((char *)&nodeid, sizeof(uint64_t));
            d->nuts3code_to_relationid.insert(std::pair<int, uint64_t>(code, nodeid));
        }
    } else
        Error::warn("Expected 'n', got '0x%02x'", chr);

    input.read((char *)&chr, sizeof(chr));
    if (chr != '_')
        Error::warn("Expected '_', got '0x%02x'", chr);
}

Sweden::~Sweden()
{
    delete d;
}

void Sweden::setMinMaxLatLon(double min_lat, double min_lon, double max_lat, double max_lon) {
    d->min_lat = min_lat;
    d->max_lat = max_lat;
    d->min_lon = min_lon;
    d->max_lon = max_lon;
    d->delta_lat =  d->max_lat - d->min_lat;
    d->delta_lon =  d->max_lon - d->min_lon;
}

void Sweden::dump() {
    d->dumpSCBcodeNames();
}

std::ostream &Sweden::write(std::ostream &output) {
    char chr = 'S';
    output.write((char *)&chr, sizeof(chr));
    size_t  num_elements = d->scbcode_to_relationid.size();
    output.write((char *)&num_elements, sizeof(num_elements));
    for (std::map<int, uint64_t>::const_iterator it = d->scbcode_to_relationid.cbegin(); it != d->scbcode_to_relationid.cend(); ++it) {
        output.write((char *) & ((*it).first), sizeof(int));
        output.write((char *) & ((*it).second), sizeof(uint64_t));
    }

    chr = 'n';
    output.write((char *)&chr, sizeof(chr));
    num_elements = d->nuts3code_to_relationid.size();
    output.write((char *)&num_elements, sizeof(num_elements));
    for (std::map<int, uint64_t>::const_iterator it = d->nuts3code_to_relationid.cbegin(); it != d->nuts3code_to_relationid.cend(); ++it) {
        output.write((char *) & ((*it).first), sizeof(int));
        output.write((char *) & ((*it).second), sizeof(uint64_t));
    }
    chr = '_';
    output.write((char *)&chr, sizeof(chr));

    return output;
}

void Sweden::insertSCBcode(const int code, uint64_t relid) {
    d->scbcode_to_relationid.insert(std::pair<int, uint64_t>(code, relid));
}

int Sweden::insideSCBcode(uint64_t nodeid) {
    return d->nodeIdToCode(nodeid, d->scbcode_to_relationid, d->scbcode_to_polygon);
}

void Sweden::insertNUTS3code(const int code, uint64_t relid) {
    d->nuts3code_to_relationid.insert(std::pair<int, uint64_t>(code, relid));
}

int Sweden::insideNUTS3code(uint64_t nodeid) {
    return d->nodeIdToCode(nodeid, d->nuts3code_to_relationid, d->nuts3code_to_polygon);
}
