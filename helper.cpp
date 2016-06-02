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
                if (wn.num_nodes == 0)
                    Error::err("Got %s without nodes: %llu", cur.operator std::string().c_str(), cur.id);
                else if (wn.num_nodes == 1) {
                    Error::warn("%s has only a single node: %llu", cur.operator std::string().c_str(), wn.nodes[0]);
                    nodeIds.insert(wn.nodes[0]); ///< Way's only node
                } else { /** wn.num_nodes>=2 */
                    nodeIds.insert(wn.nodes[0]); ///< Way's first node
                    nodeIds.insert(wn.nodes[wn.num_nodes - 1]); ///< Way's last node
                    if (wn.num_nodes > 3) {
                        nodeIds.insert(wn.nodes[wn.num_nodes / 2]); ///< Way's middle node
                        if (wn.num_nodes > 6) {
                            nodeIds.insert(wn.nodes[wn.num_nodes / 4]); ///< Way's 1st quartile node
                            nodeIds.insert(wn.nodes[wn.num_nodes * 3 / 4]); ///< Way's 3st quartile node
                            if (wn.num_nodes > 12) {
                                nodeIds.insert(wn.nodes[wn.num_nodes / 8]);
                                nodeIds.insert(wn.nodes[wn.num_nodes * 3 / 8]);
                                nodeIds.insert(wn.nodes[wn.num_nodes * 5 / 8]);
                                nodeIds.insert(wn.nodes[wn.num_nodes * 7 / 8]);
                            }
                        }
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

bool handleCombiningDiacriticalMark(std::string &text, size_t i) {
    /// Assumption: text[i] == 0xcc
    const unsigned char textPlusOne = (unsigned char)(text[i + 1]);
    const unsigned char textMinusOne = (unsigned char)(text[i - 1]);
    if (i > 0 && i < text.length() - 1 && textPlusOne >= 0x80 && textPlusOne <= 0x8a && textMinusOne >= 0x41 && textMinusOne <= 0x75) {
        /// Combining Diacritical Mark detected
        if (textPlusOne == 0x81) { /// Combining Acute Accent
            if (textMinusOne == 'e') {
                text[i - 1] = 0xc3; text[i] = 0xa9;
                text.erase(i + 1, 1);
                return true;
            } else if (textMinusOne == 'E') {
                text[i - 1] = 0xc3; text[i] = 0x89;
                text.erase(i + 1, 1);
                return true;
            }
        } else if (textPlusOne == 0x88) { /// Combining Diaeresis
            if (textMinusOne == 'a') {
                text[i - 1] = 0xc3; text[i] = 0xa4;
                text.erase(i + 1, 1);
                return true;
            } else if (textMinusOne == 'A') {
                text[i - 1] = 0xc3; text[i] = 0x84;
                text.erase(i + 1, 1);
                return true;
            } else if (textMinusOne == 'o') {
                text[i - 1] = 0xc3; text[i] = 0xb6;
                text.erase(i + 1, 1);
                return true;
            } else if (textMinusOne == 'O') {
                text[i - 1] = 0xc3; text[i] = 0x96;
                text.erase(i + 1, 1);
                return true;
            }
        } else if (textPlusOne == 0x8a) { /// Combining Ring Above
            if (textMinusOne == 'a') {
                text[i - 1] = 0xc3; text[i] = 0xa5;
                text.erase(i + 1, 1);
                return true;
            } else if (textMinusOne == 'A') {
                text[i - 1] = 0xc3; text[i] = 0x85;
                text.erase(i + 1, 1);
                return true;
            }
        }
    }

    return false;
}

bool extendedLatinToAscii(std::string &text, size_t i) {
    const unsigned char c = text[i];
    const unsigned char next_c = i < text.length() - 1 ? text[i + 1] : 0;
    if (c == 0xc3) {
        if (next_c >= 0x80 && next_c <= 0x83) {
            text[i] = 'A';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0x86) {
            text[i] = 'A';
            text[i + 1] = 'E';
            return true;
        } else if (next_c == 0x87) {
            text[i] = 'C';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0x88 && next_c <= 0x8b) {
            text[i] = 'E';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0x8c && next_c <= 0x8f) {
            text[i] = 'I';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0x90) {
            text[i] = 'D';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0x91) {
            text[i] = 'N';
            text.erase(i + 1, 1);
            return true;
        } else if (((next_c >= 0x92 && next_c <= 0x95) || next_c == 0x98)) {
            text[i] = 'O';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0x99 && next_c <= 0x9c) {
            text[i] = 'I';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0x9d) {
            text[i] = 'Y';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0x9f) {
            text[i] = 's';
            text[i + 1] = 's';
            return true;
        } else if (next_c >= 0xa0 && next_c <= 0xa3) {
            text[i] = 'a';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0xa6) {
            text[i] = 'a';
            text[i + 1] = 'e';
            return true;
        } else if (next_c == 0xa7) {
            text[i] = 'c';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0xa8 && next_c <= 0xab) {
            text[i] = 'e';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0xac && next_c <= 0xaf) {
            text[i] = 'i';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0xb0) {
            text[i] = 'd';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0xb1) {
            text[i] = 'n';
            text.erase(i + 1, 1);
            return true;
        } else if (((next_c >= 0xb2 && next_c <= 0xb5) || next_c == 0xb8)) {
            text[i] = 'o';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0xb9 && next_c <= 0xbc) {
            text[i] = 'i';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0xbd) {
            text[i] = 'y';
            text.erase(i + 1, 1);
            return true;
        }
    } else if (c == 0xc4) {
        if (next_c >= 0x80 && next_c <= 0x85) {
            if ((next_c & 1) == 0) ///< upper-case 'A'
                text[i] = 'A';
            else ///< lower-case 'a'
                text[i] = 'a';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0x86 && next_c <= 0x8d) {
            if ((next_c & 1) == 0) ///< upper-case 'C'
                text[i] = 'C';
            else ///< lower-case 'c'
                text[i] = 'c';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0x8e && next_c <= 0x91) {
            if ((next_c & 1) == 0) ///< upper-case 'D'
                text[i] = 'D';
            else ///< lower-case 'd'
                text[i] = 'd';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0x92 && next_c <= 0x9b) {
            if ((next_c & 1) == 0) ///< upper-case 'E'
                text[i] = 'E';
            else ///< lower-case 'e'
                text[i] = 'e';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0x9c && next_c <= 0xa3) {
            if ((next_c & 1) == 0) ///< upper-case 'G'
                text[i] = 'G';
            else ///< lower-case 'g'
                text[i] = 'g';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0xa4 && next_c <= 0xa7) {
            if ((next_c & 1) == 0) ///< upper-case 'H'
                text[i] = 'H';
            else ///< lower-case 'h'
                text[i] = 'h';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0xa8 && next_c <= 0xb1) {
            if ((next_c & 1) == 0) ///< upper-case 'H'
                text[i] = 'H';
            else ///< lower-case 'h'
                text[i] = 'h';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0xb2) {
            text[i] = 'I';
            text[i + 1] = 'J';
            return true;
        } else if (next_c == 0xb3) {
            text[i] = 'i';
            text[i + 1] = 'j';
            return true;
        } else if (next_c >= 0xb4 && next_c <= 0xb5) {
            if ((next_c & 1) == 0) ///< upper-case 'J'
                text[i] = 'J';
            else ///< lower-case 'j'
                text[i] = 'j';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0xb9 && next_c <= 0xbe) {
            if ((next_c & 1) == 1) ///< upper-case 'L'
                text[i] = 'L';
            else ///< lower-case 'l'
                text[i] = 'l';
            text.erase(i + 1, 1);
            return true;
        }
    } else if (c == 0xc5) {
        if (next_c >= 0x83 && next_c <= 0x88) {
            if ((next_c & 1) == 1) ///< upper-case 'N'
                text[i] = 'N';
            else ///< lower-case 'n'
                text[i] = 'n';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0x8a && next_c <= 0x8b) {
            if ((next_c & 1) == 0) ///< upper-case 'N'
                text[i] = 'N';
            else ///< lower-case 'n'
                text[i] = 'n';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0x8c && next_c <= 0x91) {
            if ((next_c & 1) == 0) ///< upper-case 'O'
                text[i] = 'O';
            else ///< lower-case 'o'
                text[i] = 'o';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0x9a && next_c <= 0xa1) {
            if ((next_c & 1) == 0) ///< upper-case 'S'
                text[i] = 'S';
            else ///< lower-case 's'
                text[i] = 's';
            text.erase(i + 1, 1);
            return true;
        }
    } else if (c == 0xc8) {
        if (next_c >= 0xa6 && next_c <= 0xa7) {
            if ((next_c & 1) == 0) ///< upper-case 'A'
                text[i] = 'A';
            else ///< lower-case 'a'
                text[i] = 'a';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0xa8 && next_c <= 0xa9) {
            if ((next_c & 1) == 0) ///< upper-case 'E'
                text[i] = 'E';
            else ///< lower-case 'e'
                text[i] = 'e';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c >= 0xaa && next_c <= 0xb1) {
            if ((next_c & 1) == 0) ///< upper-case 'O'
                text[i] = 'O';
            else ///< lower-case 'o'
                text[i] = 'o';
            text.erase(i + 1, 1);
            return true;
        }
    } else {
        const unsigned char next2_c = i < text.length() - 2 ? text[i + 2] : 0;

        if (c == 0xe1) {
            if (next_c == 0xba && next2_c >= 0xa0 && next2_c <= 0xb7) {
                if ((next2_c & 1) == 0) ///< upper-case 'A'
                    text[i] = 'A';
                else ///< lower-case 'a'
                    text[i] = 'a';
                text.erase(i + 1, 2);
                return true;
            } else if (((next_c == 0xba && next2_c >= 0xb8) || (next_c == 0xbb && next2_c <= 0x87))) {
                if ((next2_c & 1) == 0) ///< upper-case 'E'
                    text[i] = 'E';
                else ///< lower-case 'e'
                    text[i] = 'e';
                text.erase(i + 1, 2);
                return true;
            } else if (next_c == 0xbb && next2_c >= 0x88 && next2_c <= 0x8b) {
                if ((next2_c & 1) == 0) ///< upper-case 'I'
                    text[i] = 'I';
                else ///< lower-case 'i'
                    text[i] = 'i';
                text.erase(i + 1, 2);
                return true;
            } else if (next_c == 0xbb && next2_c >= 0x8c && next2_c <= 0xa3) {
                if ((next2_c & 1) == 0) ///< upper-case 'O'
                    text[i] = 'O';
                else ///< lower-case 'o'
                    text[i] = 'o';
                text.erase(i + 1, 2);
                return true;
            } else if (next_c == 0xbb && next2_c >= 0xa4 && next2_c <= 0xb1) {
                if ((next2_c & 1) == 0) ///< upper-case 'U'
                    text[i] = 'U';
                else ///< lower-case 'u'
                    text[i] = 'u';
                text.erase(i + 1, 2);
                return true;
            } else if (next_c == 0xbb && next2_c >= 0xb2 && next2_c <= 0xb9) {
                if ((next2_c & 1) == 0) ///< upper-case 'Y'
                    text[i] = 'Y';
                else ///< lower-case 'y'
                    text[i] = 'y';
                text.erase(i + 1, 2);
                return true;
            }
        }

        // TODO
    }
    // TODO

    return false;
}


bool symbolsToAscii(std::string &text, size_t &i) {
    const unsigned char c = text[i];
    const unsigned char next_c = i < text.length() - 1 ? text[i + 1] : 0;

    if (c == 0xc2) {
        if (next_c == 0xa1) {
            /// Inverted Exclamation Mark
            text[i] = '!';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0xb0) {  /// Degree symbol, erase it
            text.erase(i, 2);
            --i;
            return true;
        } else if (next_c == 0xb2) {  /// Power-of-2 symbol
            text[i] = '2';
            text.erase(i + 1, 1);
            return true;
        } else if (next_c == 0xb4) {  /// Acute Accent (spacing character)
            text[i] = '\'';
            text.erase(i + 1, 1);
            return true;
        }
    } else if (c == 0xe2) {
        const unsigned char next2_c = i < text.length() - 2 ? text[i + 2] : 0;
        if (next_c == 0x80) {
            if (next2_c >= 0x92 && next2_c <= 0x95) {
                /// Some form of a dash
                text[i] = '-';
                text.erase(i + 1, 2);
                return true;
            } else if (next2_c == 0xa6) {
                /// Horizontal Ellipsis: ...
                text[i] = '.';
                text[i + 1] = '.';
                text[i + 2] = '.';
                return true;
            }
            // TODO
        } else if (next_c == 0x86) {
            if (next2_c >= 0x90 || next2_c >= 0x92 || next2_c >= 0x94 || next2_c >= 0xbc || next2_c >= 0xbd) {
                /// Some form of an arrow
                text[i] = '-';
                text.erase(i + 1, 2);
                return true;
            }
            // TODO
        } else if (next_c == 0x87) {
            if (next2_c >= 0x80 || next2_c >= 0x81 || next2_c >= 0x90 || next2_c >= 0x92) {
                /// Some form of an arrow
                text[i] = '-';
                text.erase(i + 1, 2);
                return true;
            }
            // TODO
        } else if (next_c == 0x88) {
            if (next2_c == 0x92) {
                /// Minus sign
                text[i] = '-';
                text.erase(i + 1, 2);
                return true;
            }
            // TODO
        }
        // TODO
    }

    return false;
}

bool correctutf8mistakes(std::string &text, size_t &i) {
    const unsigned char c = text[i];
    const unsigned char next_c = i < text.length() - 1 ? text[i + 1] : 0;
    const unsigned char next2_c = i < text.length() - 2 ? text[i + 2] : 0;

    if (c == 0xc2 && next_c == 0xae) { /// Registered Trademark, ...
        text.erase(i, 2);
        --i;
        return true;
    } else if (c == 0xe2 && next_c == 0x84 && next2_c == 0xa2) { /// Registered Trademark, ...
        text.erase(i, 3);
        --i;
        return true;
    } else if (c == 0xe2 && next_c == 0x80) {
        if ((next2_c >= 0x9c && next2_c <= 0x9f) || next2_c == 0xb3 || next2_c == 0xb6) {
            /// Something like a double quotation mark
            text[i] = '"';
            text.erase(i + 1, 2);
            return true;
        } else if ((next2_c >= 0x98 && next2_c <= 0x9b) || next2_c == 0xb2 || next2_c == 0xb5) {
            /// Something like a single quotation mark
            text[i] = '\'';
            text.erase(i + 1, 2);
            return true;
        } else if (next2_c == 0xa2) {
            /// Bullet symbol
            text[i] = '.';
            text.erase(i + 1, 2);
            return true;
        }
    }

    return false;
}

unsigned char utf8tolower(const unsigned char &prev_c, unsigned char c) {
    if ((c >= 'A' && c <= 'Z') ||
            (prev_c == 0xc3 && c >= 0x80 && c <= 0x9e && c != 0x97 /** poor man's Latin-1 Supplement lower case */))
        c |= 0x20; ///< set bit 0x20
    else if (prev_c == 0xc4 && c >= 0x80 && c <= 0xb7)
        c |= 0x01;
    else if (prev_c == 0xc5 && c >= 0x8a && c <= 0xbe)
        c |= 0x01;
    return c;
}

unsigned char utf8toupper(const unsigned char &prev_c, unsigned char c) {
    if ((c >= 'a' && c <= 'z') ||
            (prev_c == 0xc3 && c >= 0xa0 && c <= 0xbe && c != 0xb7 /** poor man's Latin-1 Supplement upper case */))
        c &= 0xdf; ///< remove bit 0x20
    else if (prev_c == 0xc4 && c >= 0x80 && c <= 0xb7)
        c &= 0xfe;
    else if (prev_c == 0xc5 && c >= 0x8a && c <= 0xbe)
        c &= 0xfe;
    return c;
}

std::string &utf8tolower(std::string &text) {
    unsigned char prev_c = 0;
    for (size_t i = 0; i < text.length(); ++i) {
        const unsigned char c = text[i];
        if (i > 0 && c == 0xcc) {
            handleCombiningDiacriticalMark(text, i);
            prev_c = text[i] = utf8tolower(prev_c, text[i]);
        } else if (c == 0xc2 || c == 0xe2) {
            /// Observation: mapper use by mistake UTF-8 sequences starting with 0xc2 or 0xe2
            const bool madeChanges = correctutf8mistakes(text, i);
            if (!madeChanges)
                symbolsToAscii(text, i);
            prev_c = text[i];
        } else if ((c >= 0xc3 && c <= 0xc5) || c == 0xe1) {
            /// Rewrite extended Latin characters into their plain ASCII counterparts
            extendedLatinToAscii(text, i);
            prev_c = text[i] = utf8tolower(prev_c, text[i]);
        } else
            prev_c = text[i] = utf8tolower(prev_c, c);
    }
    return text;
}

/// found online: http://stackoverflow.com/questions/236129/split-a-string-in-c
std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems, bool skip_empty) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (skip_empty && item.empty()) continue;
        elems.push_back(item);
    }
    return elems;
}

#ifdef LATEX_OUTPUT
std::string teXify(const std::string &input) {
    std::string output = input;
    std::string::size_type p = 0;
    while ((p = output.find("&", p)) != std::string::npos) {
        output.replace(p, 2, "\\&");
        p += 2;
    }
    return output;
}

std::string rewrite_TeX_spaces(const std::string &input) {
    size_t space_counter = 0;
    std::string output;
    for (const char c : input) {
        if (c == ' ' || c == '\r' || c == '\n'){
            if (!output.empty())
            ++space_counter;
        } else {
            if (space_counter == 1)
                output.append(" ", 1);
            else if (space_counter > 1)
                output.append("\\hspace*{1em plus 1.5em minus 0.5em}");
            space_counter = 0;
            output.append(&c, 1);
        }
    }
    return output;
}
#endif // LATEX_OUTPUT
