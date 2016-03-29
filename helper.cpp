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

#include "helper.h"

#include <queue>
#include <set>

#include "globalobjects.h"

OSMElement getNodeInOSMElement(const OSMElement &element) {
    OSMElement cur(element);
    /// If the current element is a way or a relation, resolve it to
    /// a node; a node is needed to retrieve a coordinate from it
    while (cur.type != OSMElement::Node) {
        if (cur.type == OSMElement::Relation) {
            RelationMem rm;
            if (relMembers->retrieve(cur.id, rm) && rm.num_members > 0)
                cur = rm.members[rm.num_members / 2]; ///< take a OSMElement in the middle of list of relation members
            else
                break;
        } else if (cur.type == OSMElement::Way) {
            WayNodes wn;
            if (wayNodes->retrieve(cur.id, wn) && wn.num_nodes > 0)
                cur = OSMElement(wn.nodes[wn.num_nodes / 2], OSMElement::Node, element.realworld_type); ///< take a node in the middle of the way
            else
                break;
        }
    }
    return cur;
}

bool getCenterOfOSMElement(const OSMElement &element, Coord &coord) {
    coord.invalidate();

    std::queue <OSMElement> queue;
    std::set<uint64_t> nodeIds;

    queue.push(element);
    while (!queue.empty()) {
        const OSMElement &cur = queue.front();
        if (cur.type == OSMElement::Node)
            nodeIds.insert(cur.id);
        else if (cur.type == OSMElement::Way) {
            WayNodes wn;
            if (wayNodes->retrieve(cur.id, wn)) {
                nodeIds.insert(wn.nodes[0]); ///< Way's first node
                nodeIds.insert(wn.nodes[wn.num_nodes - 1]); ///< Way's last node
                if (wn.num_nodes > 4) {
                    nodeIds.insert(wn.nodes[wn.num_nodes / 2]); ///< Way's center node
                    if (wn.num_nodes > 16) {
                        nodeIds.insert(wn.nodes[wn.num_nodes / 4]); ///< Way's 1st quartile node
                        nodeIds.insert(wn.nodes[wn.num_nodes * 3 / 4]); ///< Way's 3st quartile node
                    }
                }
            }
        } else if (cur.type == OSMElement::Relation) {
            RelationMem rm;
            if (relMembers->retrieve(cur.id, rm)) {
                for (int i = rm.num_members - 1; i >= 0; --i)
                    queue.push(rm.members[i]);
            }
        }

        queue.pop(); ///< remove front element that was just processed
    }

    if (nodeIds.empty()) return false; ///< no nodes referred to, nothing to do

    int64_t sumX = 0, sumY = 0;
    size_t count = 0;
    for (const uint64_t nodeId : nodeIds) {
        Coord c;
        if (node2Coord->retrieve(nodeId, c)) {
            sumX += c.x;
            sumY += c.y;
            ++count;
        }
    }

    if (count > 0) {
        coord.x = sumX / count;
        coord.y = sumY / count;
        return true;
    } else
        return false;
}

unsigned char utf8tolower(const unsigned char &prev_c, unsigned char c) {
    if ((c >= 'A' && c <= 'Z') ||
            (prev_c == 0xc3 && c >= 0x80 && c <= 0x9e /** poor man's Latin-1 Supplement lower case */))
        c |= 0x20;
    return c;
}

std::string &utf8tolower(std::string &text) {
    unsigned char prev_c = 0;
    for (size_t i = 0; i < text.length(); ++i) {
        const unsigned char c = text[i];
        prev_c = text[i] = utf8tolower(prev_c, c);
    }
    return text;
}
