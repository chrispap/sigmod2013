#include <core.h>
#include <set>

using namespace std;

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
    int length;                                 ///< Strlen(txt);
    unsigned letterBits;                        ///< 1 bit for every english lower letter [a-z]
    set<QueryID> querySet[3];                   ///< Sets of queries matching this word. One set for each MatchType
    char txt[MAX_WORD_LENGTH+1];                ///< The actual word :P

    Word (const char *c1, const char *c2)       ///< Store the new word and populate the data structures
    {
        letterBits =0;
        int i=0;
        while (c1!=c2) {
            letterBits |= (1<<(*c1-'a'));       // Set tha bit for that letter ex.: For letter 'd' set bit#3
            txt[i++] = *c1++;
        }
        txt[i]='\0';
        length = i;
    }

    static int letterDiff(Word *w1, Word *w2 )
    {
        return __builtin_popcount(w1->letterBits ^ w2->letterBits);
    }
};

struct Query
{
    MatchType   type;
    char        dist;
    char        numWords;
    Word*       words[MAX_QUERY_WORDS];
};

struct LenComp {
  bool operator() (const Word* lhs, const Word* rhs) const
  {return lhs->length == rhs->length ? lhs < rhs : lhs->length < rhs->length ;}
};
