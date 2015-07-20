#ifndef NODETOCOORD_H
#define NODETOCOORD_H

#include <osmpbf/osmpbf.h>

struct N2CNode;

class NodeToCoord
{
public:
    explicit NodeToCoord();
    ~NodeToCoord();

    bool insert(uint64_t id, double lat, double lon);
    bool retrieve(uint64_t id, double &lat, double &lon);

private:
    struct N2CNode *root;
};

#endif // NODETOCOORD_H
