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

struct QWordH {
    unsigned letterBits;
    unsigned common_prefix;
    WordText txt;
    unsigned qwindex;

    QWordH(Word* w, MatchType mt) :
        letterBits(w->letterBits), common_prefix(0), txt(w->txt), qwindex(w->qwindex[mt])  {}
};

struct QWMap {
    vector<QWordH>& operator[] (int length) { return vec[length-MIN_WORD_LENGTH]; }
protected:
    vector<QWordH> vec[MAX_WORD_LENGTH-MIN_WORD_LENGTH+1];
};

#endif
