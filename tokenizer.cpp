/***************************************************************************
 *   Copyright (C) 2015 by Thomas Fischer <thomas.fischer@his.se>          *
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

#include "tokenizer.h"

#include <fstream>
#include <iostream>
#include <unordered_set>

#include "error.h"

class Tokenizer::Private
{
private:
    Tokenizer *p;

    std::vector<std::string> stopwords;

    void load_stopwords() {
        stopwords.clear();

        /// Stopword file has to be sorted with
        ///   LC_ALL=C sort -u ...
        std::ifstream stopwordsfile("stopwords.txt");
        if (stopwordsfile.is_open()) {
            std::string line;
            while (getline(stopwordsfile, line)) {
                if (line[0] == 0 || line[0] == '#') continue; // skip empty lines and comments
                stopwords.push_back(line);
            }
            stopwordsfile.close();
        }
    }

public:
    Private(Tokenizer *parent)
        : p(parent) {
        load_stopwords();
    }

    ~Private() {
        /// nothing
    }

    bool is_stopword(const std::string &word) {
        int a = 0, b = stopwords.size() - 1;
        int cmp = a < b ? /** will enter while loop and set value properly */ 0 : (a == b ? /** only single word in stopword list, compare to it */ word.compare(stopwords[a]) : /** stopword list is empty */ 1);
        while (a < b) {
            const int p = (b + a) / 2;
            cmp = word.compare(stopwords[p]);
            if (a + 1 == b /** assert: a==p */)
                return cmp == 0 || word.compare(stopwords[b]) == 0;
            else if (cmp < 0)
                b = p;
            else if (cmp > 0)
                a = p;
            else /** cmp==0, i.e. word equals stopwords[p], i.e. word is a stopword */
                return 1;
        }
        return cmp == 0;
    }

    static unsigned char utf8tolower(const unsigned char &prev_c, unsigned char c) {
        if ((c >= 'A' && c <= 'Z') ||
                (prev_c == 0xc3 && c >= 0x80 && c <= 0x9e /** poor man's Latin-1 Supplement lower case */))
            c |= 0x20;
        return c;
    }

};

Tokenizer::Tokenizer()
    : d(new Tokenizer::Private(this)) {
    if (d == NULL)
        Error::err("Could not allocate memory for Tokenizer::Private");
}

Tokenizer::~Tokenizer() {
    delete d;
}


int Tokenizer::read_words(std::istream &input, std::vector<std::string> &words, Multiplicity multiplicity) {
    std::string line, lastword;
    static const std::string gap(" ?!\"'#%*&()=,;._\n\r\t");
    std::unordered_set<std::string> known_words;

    while (getline(input, line))
    {
        if (line[0] == 0 || line[0] == '#') continue; // skip empty lines and comments

        unsigned char prev_c = 0;
        for (std::string::iterator it = line.begin(); it != line.end(); ++it) {
            if (gap.find(*it) == std::string::npos) {
                unsigned char c = Private::utf8tolower(prev_c, *it);
                lastword.append((char *)(&c), 1);
                prev_c = c;
            } else if (!lastword.empty()) {
                if (!d->is_stopword(lastword)) {
                    if (multiplicity == Duplicates)
                        words.push_back(lastword);
                    else if (multiplicity == Unique && known_words.find(lastword) == known_words.end()) {
                        words.push_back(lastword);
                        known_words.insert(lastword);
                    }
                }
                lastword.clear();
                prev_c = 0;
            }
        }
        if (!lastword.empty()) {
            if (!d->is_stopword(lastword)) {
                if (multiplicity == Duplicates)
                    words.push_back(lastword);
                else if (multiplicity == Unique && known_words.find(lastword) == known_words.end()) {
                    words.push_back(lastword);
                    known_words.insert(lastword);
                }
            }
            lastword.clear();
        }
    }

    return words.size();
}
