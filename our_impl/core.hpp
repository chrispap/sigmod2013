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

#endif
