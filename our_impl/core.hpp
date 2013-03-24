#ifndef CORE_H
#define CORE_H

#include <core.h>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>

#include "word.hpp"
#include "indexHashTable.hpp"

using namespace std;

struct Query
{
    MatchType       type;
    char            dist;
    char            numWords;
    Word*           words[MAX_QUERY_WORDS];
};

struct Document
{
    DocID           id;
    char            *str;
    IndexHashTable  *wordIndices;
    unsigned        numRes;
    QueryID*        matchingQueryIDs;
};

#endif
