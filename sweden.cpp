#include "sweden.h"

#include <iostream>
#include <fstream>
#include <deque>

#include "error.h"

class Sweden::Private {
private:
    Sweden *p;

    struct Land {
        Land(const std::string &_label)
            : label(_label) {}
        Land() {}

        std::string label;
        std::map<int, std::string> municipalities;
    };
    std::map<int, Land> lands;

public:
    IdTree<Coord> *coords;
    IdTree<WayNodes> *waynodes;
    IdTree<RelationMem> *relmem;

    double min_lat = 1000.0, min_lon = 1000.0, max_lat = -1000.0, max_lon = -1000.0;

    std::map<int, uint64_t> scbcode_to_relationid, nuts3code_to_relationid;
    std::map<int, std::deque<std::pair<int, int> > > scbcode_to_polygon, nuts3code_to_polygon;

    explicit Private(Sweden *parent, IdTree<Coord> *_coords, IdTree<WayNodes> *_waynodes, IdTree<RelationMem> *_relmem)
        : p(parent), coords(_coords), waynodes(_waynodes), relmem(_relmem) {
        /// nothing
    }

    int nodeIdToCode(uint64_t nodeid, const std::map<int, uint64_t> &code_to_relationid, std::map<int, std::vector<std::pair<int, int> > > &code_to_polygon) {
        static const int INT_RANGE = 0x3fffffff;
        const double delta_lat = max_lat - min_lat;
        const double delta_lon = max_lon - min_lon;

        if (code_to_polygon.empty()) {
            for (std::map<int, uint64_t>::const_iterator it = code_to_relationid.cbegin(); it != code_to_relationid.cend(); ++it) {
                const uint64_t relid = (*it).second;
                RelationMem rel;
                if (relmem->retrieve(relid, rel)) {
                    std::vector<std::pair<int, int> > polygon;
                    for (uint32_t i = 0; i < rel.num_members; ++i) {
                        WayNodes wn;
                        if (waynodes->retrieve(rel.members[i], wn)) {
                            for (uint32_t j = 0; j < wn.num_nodes; ++j) {
                                Coord coord;
                                if (coords->retrieve(wn.nodes[j], coord)) {
                                    const int lat = (coord.lat - min_lat) * INT_RANGE / delta_lat;
                                    const int lon = (coord.lon - min_lon) * INT_RANGE / delta_lon;
                                    polygon.push_back(std::pair<int, int>(lat, lon));
                                }
                            }
                        }
                    }
                    code_to_polygon.insert(std::pair<int, std::vector<std::pair<int, int> > >((*it).first, polygon));
                }
            }
        }

        Coord coord;
        if (coords->retrieve(nodeid, coord)) {
            const int x = (coord.lon - min_lon) * INT_RANGE / delta_lon;
            const int y = (coord.lat - min_lat) * INT_RANGE / delta_lat;

            for (std::map<int, std::deque<std::pair<int, int> > >::const_iterator it = code_to_polygon.cbegin(); it != code_to_polygon.cend(); ++it) {
                /// For a good explanation, see here: http://alienryderflex.com/polygon/
                const std::deque<std::pair<int, int> > &polygon = (*it).second;
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
    /// nothing
}

void Sweden::setMinMaxLatLon(double min_lat, double min_lon, double max_lat, double max_lon) {
    d->min_lat = min_lat;
    d->max_lat = max_lat;
    d->min_lon = min_lon;
    d->max_lon = max_lon;
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
