#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <string>
#include <vector>

class Tokenizer
{
public:
    explicit Tokenizer();
    ~Tokenizer();

    int read_words(std::istream &input, std::vector<std::string> &words);

private:
    class Private;
    Private *const d;
};


#endif // TOKENIZER_H
