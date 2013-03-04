#include <list>

using namespace std;

/* Struct definitions */
struct Query
{
    QueryID id;
    char str[MAX_QUERY_LENGTH];
    MatchType match_type;
    unsigned int match_dist;

    /* Constructors */
    Query (QueryID _id) : id(_id) {}

    /* Operator overloadings */
    bool operator< (const Query &q) const { return id<q.id? true: false;}
};

struct DocResult
{
    DocID id;
    unsigned int num_res;
    QueryID* query_ids;
};

struct PendingDoc
{
    DocID id;
    char *str;
};

/* Function prototypes */
void *ThreadFunction(void *param);
void Match(char *cur_doc_str, list<unsigned int> &query_ids);
int EditDistance(char* a, int na, char* b, int nb);
unsigned int HammingDistance(char* a, int na, char* b, int nb);
