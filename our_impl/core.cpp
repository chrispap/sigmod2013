#include <core.h>
#include "core.hpp"

#include <cstdio>
#include <cstdlib>

#include <csignal>

#include <cstring>
#include <map>
#include <unordered_map>
#include <set>
#include <queue>
#include <pthread.h>

using namespace std;

/* Prototypes */
static unsigned myHash(const char *c1, const char *c2);
static bool wordsEqual(const char *c1, const char *c2, const char *txt);

/* Definitions */
#define NUM_THREADS         8
#define HASH_EXP            25                          ///< eg: 3
#define HASH_SIZE           (1<<HASH_EXP)               ///< eg: 2^3   = 8 = 0000 0000 0000 1000
#define HASH_MASK           (HASH_SIZE-1)               ///< eg: 2^3-1 = 7 = 0000 0000 0000 0111

/* Global Data */
static Word*                wdb[HASH_SIZE];             ///< Here store pointers to every word encountered
static map<QueryID,Query>   activeQueries;              ///< Active queries
static queue<PendingDoc>    pendingDocs;                ///< Pending documents
static queue<DocResult>     availableDocs;              ///< Ready documents

/* Global data for threading */
static volatile bool        done;                       ///< Used to singal the threads to exit
static pthread_t            threads[NUM_THREADS];       ///<
static pthread_mutex_t      pendingDocs_mutex;          ///<
static pthread_cond_t       pendingDocs_condition;      ///<
static pthread_mutex_t      availableDocs_mutex;        ///<
static pthread_cond_t       availableDocs_condition;    ///<

/* Library Functions */
ErrorCode InitializeIndex()
{
    /* Create the threads, which will enter the waiting state. */
    pthread_mutex_init(&pendingDocs_mutex, NULL);
    pthread_cond_init (&pendingDocs_condition, NULL);
    pthread_mutex_init(&availableDocs_mutex, NULL);
    pthread_cond_init (&availableDocs_condition, NULL);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    done = false;
    long t;
    for (t=0; t< NUM_THREADS; t++) {
        int rc = pthread_create(&threads[t], &attr, ThreadFunc, (void *)t);
        if (rc) { printf("ERROR; return code from pthread_create() is %d\n", rc); exit(-1);}
    }

    return EC_SUCCESS;
}

ErrorCode DestroyIndex()
{
    pthread_mutex_lock(&pendingDocs_mutex);
    pthread_cond_broadcast(&pendingDocs_condition);
    pthread_mutex_unlock(&pendingDocs_mutex);

    done = true;
    int t;
    for (t=0; t<NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }

    return EC_SUCCESS;
}

ErrorCode StartQuery(QueryID query_id, const char* query_str, MatchType match_type, unsigned int match_dist)
{


    Query new_query;
    const char *c1, *c2;
    char num_words=0;

    c2 = query_str;
    while(*c2==' ') ++c2;                                       // Skip any spaces
    for (c1=c2;*c2;c1=c2+1) {                                   // For each query word
        do {++c2;} while (*c2!=' ' && *c2 );                    // Find end of string
        unsigned i = myHash(c1, c2);                            // Calculate the myHash for this word
        while (wdb[i] && !wordsEqual(c1, c2, wdb[i]->txt))      // Resolve any conflicts
             i = (i+1) & HASH_MASK;
        if (!wdb[i]) wdb[i] = new Word(c1, c2);
        wdb[i]->matchingQueries.insert(query_id);
        new_query.words[num_words] = wdb[i];
        num_words++;
    }

    if (num_words) {
        new_query.type = match_type;
        new_query.dist = match_dist;
        new_query.numWords = num_words;
        activeQueries[query_id] = new_query;
    }

    return EC_SUCCESS;
}

ErrorCode EndQuery(QueryID query_id)
{
    auto qer = activeQueries.find(query_id);
    for (int i=0; i<qer->second.numWords; i++)
        qer->second.words[i]->matchingQueries.erase(query_id);
    activeQueries.erase(qer);
    return EC_SUCCESS;
}

ErrorCode MatchDocument(DocID doc_id, const char* doc_str)
{
    char *cur_doc_str = (char *) malloc((1+strlen(doc_str)));

    if (!cur_doc_str){
        printf("Could not allocate memory. \n");
        return EC_FAIL;
    }

    strcpy(cur_doc_str, doc_str);
    PendingDoc newDoc;
    newDoc.id=doc_id;
    newDoc.str=cur_doc_str;

    pthread_mutex_lock(&pendingDocs_mutex);
    while ( pendingDocs.size() > (unsigned)NUM_THREADS )
        pthread_cond_wait(&pendingDocs_condition, &pendingDocs_mutex);

    pendingDocs.push(newDoc);
    ////fprintf(stderr, "DocID: %u pushed \n", newDoc.id); fflush(stdout);
    pthread_cond_broadcast(&pendingDocs_condition);
    pthread_mutex_unlock(&pendingDocs_mutex);

    return EC_SUCCESS;
}

ErrorCode GetNextAvailRes(DocID* p_doc_id, unsigned int* p_num_res, QueryID** p_query_ids)
{
    /* Get the first undeliverd result from "availableDocs" and return it */
    pthread_mutex_lock(&availableDocs_mutex);
    while ( availableDocs.empty() )
        pthread_cond_wait(&availableDocs_condition, &availableDocs_mutex);

    *p_doc_id=0;
    *p_num_res=0;
    *p_query_ids=0;

    if(availableDocs.empty())
        return EC_NO_AVAIL_RES;

    DocResult res = availableDocs.front();
    availableDocs.pop();

    *p_doc_id = res.docID;
    *p_num_res = res.numRes;
    *p_query_ids = res.queryIDs;

    //fprintf(stderr, "DocID: %u returned \n", *p_doc_id); fflush(stdout);

    pthread_cond_broadcast(&availableDocs_condition);
    pthread_mutex_unlock(&availableDocs_mutex);
    return EC_SUCCESS;
}

/* Our Functions */
void* ThreadFunc(void *param)
{
    long myThreadId = (long)param;

    while (1)
    {
        /* Wait untill new doc has arrived or "done" is set. */
        pthread_mutex_lock(&pendingDocs_mutex);
        while (!done && pendingDocs.empty())
            pthread_cond_wait(&pendingDocs_condition, &pendingDocs_mutex);

        if (done){
            pthread_mutex_unlock(&pendingDocs_mutex);
            break;
        }

        /* Get a document from the pending list */
        PendingDoc doc = pendingDocs.front();
        pendingDocs.pop();
        //fprintf(stderr, "DocID: %u retrieved by Thread_%ld for matching \n", doc.id, myThreadId);
        fflush(stdout);
        pthread_cond_broadcast(&pendingDocs_condition);
        pthread_mutex_unlock(&pendingDocs_mutex);

        /* Process the document */
        set<QueryID> matched_query_ids;
        matched_query_ids.clear();
        Match(doc.str, matched_query_ids);
        free(doc.str);

        /* Create the result array */
        DocResult result;
        result.docID=doc.id;
        result.numRes=matched_query_ids.size();
        result.queryIDs=0;

        if(result.numRes){
            unsigned int i;
            set<unsigned int>::const_iterator qi;
            result.queryIDs=(QueryID*)malloc(result.numRes*sizeof(QueryID));
            qi = matched_query_ids.begin();
            for(i=0;i<result.numRes;i++)
                result.queryIDs[i] = *qi++;
        }

        /* Store the result */
        pthread_mutex_lock(&availableDocs_mutex);
        availableDocs.push(result);
        pthread_cond_broadcast(&availableDocs_condition);
        pthread_mutex_unlock(&availableDocs_mutex);
    }

    //fprintf(stderr, "Thread#%2ld: Exiting. \n", myThreadId); fflush(stdout);
    return NULL;
}

unsigned myHash(const char *c1, const char *c2)
{
    unsigned val = 0;
    while (c1!=c2) val = ((*c1++) + 61 * val);
    val=val%HASH_SIZE;
    return val;
}

void Match(char *doc_str, set<unsigned int> &matchingQueries)
{
    map<QueryID,set<Word *>> query_stats;

    const char *c1, *c2;
    c2 = doc_str;
    while(*c2==' ') ++c2;                                       // Skip any spaces
    for (c1=c2;*c2;c1=c2+1) {
        do {++c2;} while (*c2!=' ' && *c2 );                    // Find end of string
        unsigned i = myHash(c1, c2);                            // Calculate the myHash for this word
        while (wdb[i] && !wordsEqual(c1, c2, wdb[i]->txt))      // Skip any conflicts
            i = (i+1) & HASH_MASK;
        if (!wdb[i]) continue;                                  // H leksi den yparxei sto TABLE

        for (auto &q : wdb[i]->matchingQueries)
            query_stats[q].insert(wdb[i]);
    }

    for (auto &x : query_stats){
        if (x.second.size() == (unsigned) activeQueries[x.first].numWords) {
            matchingQueries.insert(x.first);
        }
    }

    return;
}

/* Hepler Functions */
bool wordsEqual(const char *c1, const char *c2, const char *txt)
{
    while (c1!=c2 && *txt) if (*txt++ != *c1++) return false;
    if (c1==c2 && !*txt) return true;
    else return false;
}

int EditDist(char* a, int na, char* b, int nb)
{
    int oo=0x7FFFFFFF;

    int T[2][MAX_WORD_LENGTH+1];

    int ia, ib;

    int cur=0;
    ia=0;

    for(ib=0;ib<=nb;ib++)
        T[cur][ib]=ib;

    cur=1-cur;

    for(ia=1;ia<=na;ia++)
    {
        for(ib=0;ib<=nb;ib++)
            T[cur][ib]=oo;

        int ib_st=0;
        int ib_en=nb;

        if(ib_st==0)
        {
            ib=0;
            T[cur][ib]=ia;
            ib_st++;
        }

        for(ib=ib_st;ib<=ib_en;ib++)
        {
            int ret=oo;

            int d1=T[1-cur][ib]+1;
            int d2=T[cur][ib-1]+1;
            int d3=T[1-cur][ib-1]; if(a[ia-1]!=b[ib-1]) d3++;

            if(d1<ret) ret=d1;
            if(d2<ret) ret=d2;
            if(d3<ret) ret=d3;

            T[cur][ib]=ret;
        }

        cur=1-cur;
    }

    int ret=T[1-cur][nb];

    return ret;
}

int HammingDist(char* a, int na, char* b, int nb)
{
    int j, oo=0x7FFFFFFF;
    if(na!=nb) return oo;

    unsigned int num_mismatches=0;
    for(j=0;j<na;j++) if(a[j]!=b[j]) num_mismatches++;

    return num_mismatches;
}
