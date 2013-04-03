#ifndef CORE_H
#define CORE_H

struct Query
{
    MatchType       type;
    char            dist;
    char            numWords;
    Word*           words[MAX_QUERY_WORDS];

    Query() : dist(0), numWords(0) {}
};

struct Document
{
    DocID           id;
    char            *str;
    IndexHashTable  *words;
    vector<QueryID> *matchingQueries;
};

#endif
