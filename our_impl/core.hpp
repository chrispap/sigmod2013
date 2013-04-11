#ifndef CORE_H
#define CORE_H

struct Query
{
    char            numWords;
    Word*           words[MAX_QUERY_WORDS];
    MatchType       type;
    char            dist;
};

struct Document
{
    DocID           id;
    char            *str;
    IndexHashTable  *words;
    vector<QueryID> *matchingQueries;
};

struct QWord {
    int length;
    unsigned letterBits;
    unsigned common_prefix;
    WordText txt;
    unsigned qwindex;
    
    QWord(Word* w, MatchType mt) :
        length(w->length), letterBits(w->letterBits), common_prefix(0), txt(w->txt), qwindex(w->qwindex[mt])  {}
};

#endif
