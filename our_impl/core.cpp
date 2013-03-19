#include <core.h>
#include "core.hpp"
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <unordered_map>
#include <queue>
#include <map>
#include <set>

using namespace std;

/* Function prototypes */
static int      HammingDist (Word *wa, Word *wb);
static int      EditDist    (Word *wa, Word *wb);
static void*    ThreadFunc  (void *param);
static void     ParseDoc    (PendingDoc doc, const long thread_id);
static void     Match       (  /* to: Mariaki ???? */ );

/* Global Data */
#define NUM_THREADS         8

static queue<PendingDoc>    mPendingDocs;                   ///< Pending Documents
static queue<DocResult>     mAvailableDocs;                 ///< Ready Documents
static WordHashTable        GWDB(1<<18);                    ///< Here store pointers to e v e r y single word encountered !!!
static WordHashTable*       tWDB[NUM_THREADS];              ///< 1 hash table per thread
static WordHashTable        mActiveApproxWords(10000);      ///< Active words for approximate matcing
static map<QueryID,Query>   mActiveQueries;                 ///< Active Query IDs

/* Global data for threading */
static volatile bool        mDone;                          ///< Used to singal the mThreads to exit
static pthread_t            mThreads[NUM_THREADS];          ///< Thread objects
static pthread_mutex_t      mPendingDocs_mutex;             ///< Protect the access to pending Documents
static pthread_cond_t       mPendingDocs_condition;         ///<    -//-
static pthread_mutex_t      mAvailableDocs_mutex;           ///< Protect the access to ready Documents
static pthread_cond_t       mAvailableDocs_condition;       ///<    -//-

/* Library Functions */
ErrorCode InitializeIndex()
{
    /* Create the mThreads, which will enter the waiting state. */
    pthread_mutex_init(&mPendingDocs_mutex, NULL);
    pthread_cond_init (&mPendingDocs_condition, NULL);
    pthread_mutex_init(&mAvailableDocs_mutex, NULL);
    pthread_cond_init (&mAvailableDocs_condition, NULL);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    mDone = false;
    long t;
    for (t=0; t< NUM_THREADS; t++) {
        tWDB[t] = new WordHashTable(10000);
        int rc = pthread_create(&mThreads[t], &attr, ThreadFunc, (void *)t);
        if (rc) { fprintf(stderr, "ERROR; return code from pthread_create() is %d\n", rc); exit(-1);}
    }

    return EC_SUCCESS;
}

ErrorCode DestroyIndex()
{
    pthread_mutex_lock(&mPendingDocs_mutex);
    pthread_cond_broadcast(&mPendingDocs_condition);
    pthread_mutex_unlock(&mPendingDocs_mutex);

    mDone = true;
    int t;
    for (t=0; t<NUM_THREADS; t++) {
        pthread_join(mThreads[t], NULL);
    }

    return EC_SUCCESS;
}

ErrorCode StartQuery(QueryID query_id, const char* query_str, MatchType match_type, unsigned int match_dist)
{
    Query new_query;
    const char *c1, *c2;
    int num_words=0;

    c2 = query_str;
    while(*c2==' ') ++c2;                                       // Skip any spaces
    for (c1=c2;*c2;c1=c2+1) {                                   // For each query word
        do {++c2;} while (*c2!=' ' && *c2 );                    // Find end of string

        Word* nw;
        GWDB.insert(c1, c2, &nw);                               // Store the word in the GWDB
        nw->querySet[match_type].insert(query_id);              // Update the appropriate query set based on the match_type
        new_query.words[num_words] = nw;                        // Add the word to the query
        if (match_type != MT_EXACT_MATCH)                       // ONLY for hamming | edit dist queries
            mActiveApproxWords.insert(nw);                      // Add the word to the set with the approximate words

        num_words++;
    }

    new_query.type = match_type;
    new_query.dist = match_dist;
    new_query.numWords = num_words;
    mActiveQueries[query_id] = new_query;

    return EC_SUCCESS;
}

ErrorCode EndQuery(QueryID query_id)
{
    auto del_query_it = mActiveQueries.find(query_id);          // Iterator to the query that must be deleted
    auto del_query = del_query_it->second;                      // The actual query

    for (int i=0; i<del_query.numWords; i++)                    // Erase this query from all the words it contained
        del_query.words[i]->querySet[del_query.type].erase(query_id);
    mActiveQueries.erase(del_query_it);                         // Erase the query
    return EC_SUCCESS;
}

ErrorCode MatchDocument(DocID doc_id, const char* doc_str)
{
    char *cur_doc_str = (char *) malloc((1+strlen(doc_str)));

    if (!cur_doc_str){
        fprintf(stderr, "Could not allocate memory. \n");fflush(stderr);
        return EC_FAIL;
    }

    strcpy(cur_doc_str, doc_str);
    PendingDoc newDoc;
    newDoc.id=doc_id;
    newDoc.str=cur_doc_str;

    pthread_mutex_lock(&mPendingDocs_mutex);
    while ( mPendingDocs.size() > (unsigned)NUM_THREADS )
        pthread_cond_wait(&mPendingDocs_condition, &mPendingDocs_mutex);

    mPendingDocs.push(newDoc);
    pthread_cond_broadcast(&mPendingDocs_condition);
    pthread_mutex_unlock(&mPendingDocs_mutex);

    return EC_SUCCESS;
}

ErrorCode GetNextAvailRes(DocID* p_doc_id, unsigned int* p_num_res, QueryID** p_query_ids)
{
    /* Get the first undeliverd result from "mAvailableDocs" and return it */
    pthread_mutex_lock(&mAvailableDocs_mutex);
    while ( mAvailableDocs.empty() )
        pthread_cond_wait(&mAvailableDocs_condition, &mAvailableDocs_mutex);

    *p_doc_id=0;
    *p_num_res=0;
    *p_query_ids=0;

    if(mAvailableDocs.empty())
        return EC_NO_AVAIL_RES;

    DocResult res = mAvailableDocs.front();
    mAvailableDocs.pop();

    *p_doc_id = res.docID;
    *p_num_res = res.numRes;
    *p_query_ids = res.queryIDs;

    pthread_cond_broadcast(&mAvailableDocs_condition);
    pthread_mutex_unlock(&mAvailableDocs_mutex);
    return EC_SUCCESS;
}

/* Our Functions */
void* ThreadFunc(void *param)
{
    long myThreadId = (long) param;
    fprintf(stderr, "Thread#%2ld: Starting. \n", myThreadId); fflush(stdout);

    while (1)
    {
        /* Wait untill new doc has arrived or "mDone" is set. */
        pthread_mutex_lock(&mPendingDocs_mutex);
        while (!mDone && mPendingDocs.empty())
            pthread_cond_wait(&mPendingDocs_condition, &mPendingDocs_mutex);

        if (mDone){
            pthread_mutex_unlock(&mPendingDocs_mutex);
            break;
        }

        /* Get a document from the pending list */
        PendingDoc doc = mPendingDocs.front();
        mPendingDocs.pop();
        pthread_cond_broadcast(&mPendingDocs_condition);
        pthread_mutex_unlock(&mPendingDocs_mutex);

        /* Process the document */
        set<QueryID> matchingQueries;
        matchingQueries.clear();
        ParseDoc(doc, myThreadId);
        free(doc.str);

        /* Create the result array */
        DocResult result;
        result.docID=doc.id;
        result.numRes=matchingQueries.size();
        result.queryIDs=0;

        unsigned int i;
        set<unsigned int>::const_iterator qi;
        result.queryIDs=(QueryID*)malloc(result.numRes*sizeof(QueryID));
        qi = matchingQueries.begin();
        for(i=0;i<result.numRes;i++)
            result.queryIDs[i] = *qi++;

        /* Store the result */
        pthread_mutex_lock(&mAvailableDocs_mutex);
        //mAvailableDocs.push(result);
        pthread_cond_broadcast(&mAvailableDocs_condition);
        pthread_mutex_unlock(&mAvailableDocs_mutex);
    }

    fprintf(stderr, "Thread#%2ld: Exiting. \n", myThreadId); fflush(stdout);
    return NULL;
}

void ParseDoc(PendingDoc doc, const long thread_id)
{
    const char *c1, *c2 = doc.str;

    while(*c2==' ') ++c2;                                       // Skip any spaces

    for (c1=c2;*c2;c1=c2+1) {                                   // For each document word
        do {++c2;} while (*c2!=' ' && *c2 );                    // Find end of string
        Word* nw;
        GWDB.insert(c1, c2, &nw);
        bool already = tWDB[thread_id]->insert(nw);
    }

}

void Match()
{

}

/* Hepler Functions */
int EditDist(Word *wa, Word *wb)
{
    char* a = wa->txt;
    int na = wa->length;
    char* b = wb->txt;
    int nb = wb->length;

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

int HammingDist(Word *wa, Word *wb)
{
    char* a = wa->txt;
    int na = wa->length;
    char* b = wb->txt;
    int nb = wb->length;

    int j, oo=0x7FFFFFFF;
    if(na!=nb) return oo;

    unsigned int num_mismatches=0;
    for(j=0;j<na;j++) if(a[j]!=b[j]) num_mismatches++;

    return num_mismatches;
}
