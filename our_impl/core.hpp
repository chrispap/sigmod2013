#include <core.h>
#include <set>

using namespace std;

/* Struct definitions */
struct DocResult
{
    DocID       docID;
    unsigned    numRes;
    QueryID*    queryIDs;
};

struct PendingDoc
{
    DocID       id;
    char        *str;
};

struct Word
{
    char txt[MAX_WORD_LENGTH+1];
    set<QueryID> matchingQueries;

    Word (const char *c1, const char *c2) {int i=0; while (c1!=c2) txt[i++]=*c1++; txt[i]=0;}
};

struct Query
{
    QueryID     id;
    MatchType   type;
    char        dist;
    char        numWords;
    Word*       words[MAX_QUERY_WORDS];
};

/* Function prototypes */
void *ThreadFunc    (void *param);
void  Match         (char *cur_doc_str, set<QueryID> &query_ids);
int   EditDist      (char* a, int na, char* b, int nb);
int   HammingDist   (char* a, int na, char* b, int nb);
