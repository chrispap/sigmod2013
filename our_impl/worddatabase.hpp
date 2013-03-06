#include <core.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <set>
#include <list>
#include <queue>
#include <unordered_map>
#include <pthread.h>

#define NUM_THREADS 12

using namespace std;

/* Struct definitions */
struct Query
{
    QueryID         id;
    char str[MAX_QUERY_LENGTH];
    MatchType       match_type;
    unsigned int    match_dist;

    Query (QueryID _id) : id(_id) {}
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

struct WordInfo
{
    list<QueryID> matchingQueries;
};

class WordDatabase
{
public:
    WordDatabase();
    ~WordDatabase();
    ErrorCode startQuery(QueryID query_id, const char* query_str, MatchType match_type, unsigned int match_dist);
    ErrorCode endQuery(QueryID query_id);
    ErrorCode pushDocument(DocID doc_id, const char* doc_str);
    ErrorCode getNextAvailRes(DocID* p_doc_id, unsigned int* p_num_res, QueryID** p_query_ids);

private:
    /* Data structs */
    unordered_map<string, WordInfo> wordMap;

    /* Thread */
    volatile bool       done;
    set<Query>          activeQueries;
    pthread_t           threads[NUM_THREADS];
    queue<PendingDoc>   pendingDocs;
    pthread_mutex_t     pendingDocs_mutex;
    pthread_cond_t      pendingDocs_condition;
    queue<DocResult>    availableDocs;
    pthread_mutex_t     availableDocs_mutex;
    pthread_cond_t      availableDocs_condition;

    /** THere is done the actual job */
    void  matchDocument(char *cur_doc_str, list<unsigned int> &query_ids);

    /** Static methods */
    static void* threadFunction(void *param);
    static int EditDistance(char* a, int na, char* b, int nb);
    static unsigned int HammingDistance(char* a, int na, char* b, int nb);
};

struct ThreadParams
{
    WordDatabase *wdb;
    long thread_id;
};
