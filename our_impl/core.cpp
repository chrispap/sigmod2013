#include <core.h>
#include "core.hpp"
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <queue>
#include <map>
#include <set>
#include <cmath>
#include <unordered_map>
#include <utility>

#include "core.hpp"
#include "word.hpp"
#include "wordHashTable.hpp"
#include "indexHashTable.hpp"
#include "automata.hpp"

#define HASH_SIZE    (1<<18)
#define NUM_THREADS  7

enum PHASE { PH_IDLE, PH_01, PH_02, PH_FINISHED };

using namespace std;

/* Function prototypes */
static void*    Thread      (void *param);
static void     ParseDoc    (Document &doc, const long thread_id);

static WordHashTable        GWDB(HASH_SIZE);                ///< Here store pointers to e v e r y single word encountered.

/* Documents */
static queue<Document>      mPendingDocs;                   ///< Documents that haven't yet been touched at all.
static vector<Document>     mParsedDocs;                    ///< Documents that have been parsed.
static queue<Document>      mReadyDocs;                     ///< Documents that have been completely processed and are ready for delivery.
static IndexHashTable       mBatchWords(HASH_SIZE,true);    ///< Words that have been already encountered in the current batch.
static DFATrie              mTrie;
static int                  mBatchId;

/* Queries */
static map<QueryID,Query>   mActiveQueries;                 ///< Active Query IDs.
static IndexHashTable       mApproxWords(HASH_SIZE,false);  ///< Active words for approximate matcing.

/* Threading */
static volatile PHASE       mPhase;                         ///< Indicates in which phase the threads should be.
static pthread_t            mThreads[NUM_THREADS];          ///< Thread objects
static pthread_mutex_t      mParsedDocs_mutex;              ///< Protect the access to parsed Documents
static pthread_mutex_t      mPendingDocs_mutex;             ///< Protect the access to pending Documents.
static pthread_cond_t       mPendingDocs_cond;              ///<
static pthread_mutex_t      mReadyDocs_mutex;               ///< Protect the access to ready Documents.
static pthread_cond_t       mReadyDocs_cond;                ///<
static pthread_barrier_t    mBarrier;

/* Library Functions */
ErrorCode InitializeIndex()
{
    /* Create the mThreads, which will enter the waiting state. */
    pthread_mutex_init(&mPendingDocs_mutex, NULL);
    pthread_mutex_init(&mParsedDocs_mutex,  NULL);
    pthread_cond_init (&mPendingDocs_cond,  NULL);
    pthread_mutex_init(&mReadyDocs_mutex,   NULL);
    pthread_cond_init (&mReadyDocs_cond,    NULL);
    pthread_barrier_init(&mBarrier, NULL,   NUM_THREADS);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    mPhase = PH_IDLE;
    mBatchId=0;

    for (long t=0; t< NUM_THREADS; t++) {
        int rc = pthread_create(&mThreads[t], &attr, Thread, (void *)t);
        if (rc) { fprintf(stderr, "ERROR; return code from pthread_create() is %d\n", rc); exit(-1);}
    }

    return EC_SUCCESS;
}

ErrorCode DestroyIndex()
{
    pthread_mutex_lock(&mPendingDocs_mutex);
    mPhase = PH_FINISHED;
    pthread_cond_broadcast(&mPendingDocs_cond);
    pthread_mutex_unlock(&mPendingDocs_mutex);

    for (long t=0; t<NUM_THREADS; t++) {
        pthread_join(mThreads[t], NULL);
    }

    fprintf(stdout, "mTrie SIZE:        %u \n", mTrie.wordCount());
    fprintf(stdout, "GWDB SIZE:         %u \n", GWDB.size());
    fprintf(stdout, "mApproxWords SIZE: %u \n", mApproxWords.size());
    return EC_SUCCESS;
}

ErrorCode StartQuery(QueryID query_id, const char* query_str, MatchType match_type, unsigned int match_dist)
{
    Query new_query;
    const char *c1, *c2;
    int num_words=0;
    Word* nw; unsigned nw_index;

    c2 = query_str;
    while(*c2==' ') ++c2;                                       // Skip any spaces
    for (c1=c2;*c2;c1=c2+1) {                                   // For each query word
        do {++c2;} while (*c2!=' ' && *c2 );                    // Find end of string

        GWDB.insert(c1, c2, &nw_index, &nw);                    // Store the word in the GWDB
        nw->querySet[match_type].insert(query_id);              // Update the appropriate query set based on the match_type
        new_query.words[num_words] = nw;                        // Add the word to the query

        /*if (match_type != MT_EXACT_MATCH) {                     // ONLY for hamming | edit dist queries
            if (mApproxWords.insert(nw_index)) {                // Add the word to the set with the approximate words
                nw->dfa = new DFALevenstein(nw->txt ,3);
            }
        }*/

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
    char *new_doc_str = (char *) malloc((1+strlen(doc_str)));
    if (!new_doc_str){ fprintf(stderr, "Could not allocate memory. \n");fflush(stderr); return EC_FAIL;}
    strcpy(new_doc_str, doc_str);

    Document newDoc;
    newDoc.id = doc_id;
    newDoc.str = new_doc_str;
    newDoc.words = new IndexHashTable(HASH_SIZE);
    newDoc.matchingQueries = new set<QueryID>();

    pthread_mutex_lock(&mPendingDocs_mutex);
    mPendingDocs.push(newDoc);
    mPhase = PH_01;
    //~ fprintf(stderr, "Doc %-4u pushed \n", doc_id);fflush(NULL);
    pthread_cond_broadcast(&mPendingDocs_cond);
    pthread_mutex_unlock(&mPendingDocs_mutex);
    return EC_SUCCESS;
}

ErrorCode GetNextAvailRes(DocID* p_doc_id, unsigned int* p_num_res, QueryID** p_query_ids)
{
    pthread_mutex_lock(&mPendingDocs_mutex);
    if (mPhase==PH_01) mPhase = PH_02;
    pthread_cond_broadcast(&mPendingDocs_cond);
    pthread_mutex_unlock(&mPendingDocs_mutex);

    pthread_mutex_lock(&mReadyDocs_mutex);
    while ( mReadyDocs.empty() )
        pthread_cond_wait(&mReadyDocs_cond, &mReadyDocs_mutex);

    *p_doc_id=0;
    *p_num_res=0;
    *p_query_ids=0;
    if(mReadyDocs.empty()) return EC_NO_AVAIL_RES;
    Document res = mReadyDocs.front();
    mReadyDocs.pop();
    *p_doc_id = res.id;
    *p_num_res = res.matchingQueries->size();

    if(*p_num_res) {
        QueryID *mq = (QueryID*) malloc (*p_num_res * sizeof(QueryID));
        auto qi = res.matchingQueries->begin();
        for(unsigned i=0; i!=*p_num_res ; i++) {
            mq[i] = *qi++;
        }
        *p_query_ids = mq;
    }
    else *p_query_ids=NULL;

    delete res.matchingQueries;
    delete res.words;

    //~ fprintf(stderr, "Doc %-4u delivered \n", res.id);fflush(NULL);
    pthread_cond_broadcast(&mReadyDocs_cond);
    pthread_mutex_unlock(&mReadyDocs_mutex);
    return EC_SUCCESS;
}

/* Our Functions */
void* Thread(void *param)
{
    long myThreadId = (long) param;
    fprintf(stdout, "THREAD#%2ld STARTS \n", myThreadId); fflush(stdout);

    while (1)
    {
        /* PHASE 01 */
        while (1)
        {
            pthread_mutex_lock(&mPendingDocs_mutex);
            while (mPendingDocs.empty() && mPhase<PH_02 )
                pthread_cond_wait(&mPendingDocs_cond, &mPendingDocs_mutex);

            if (mPendingDocs.empty() || mPhase==PH_FINISHED) {
                pthread_cond_broadcast(&mPendingDocs_cond);
                pthread_mutex_unlock(&mPendingDocs_mutex);
                break;
            }

            /* Get a document from the pending list */
            Document doc (mPendingDocs.front());
            mPendingDocs.pop();
            pthread_cond_broadcast(&mPendingDocs_cond);
            pthread_mutex_unlock(&mPendingDocs_mutex);

            /* Parse the document */
            ParseDoc(doc, myThreadId);
            free(doc.str);
            pthread_mutex_lock(&mParsedDocs_mutex);
            mParsedDocs.push_back(doc);
            pthread_mutex_unlock(&mParsedDocs_mutex);
        }

        /* FINISH DETECTED */
        if (mPhase == PH_FINISHED) break;

        pthread_barrier_wait(&mBarrier);

        /* LAST PHASE */
        if (myThreadId==0)
        {
            //~ fprintf(stderr, ">> BATCH %-4d  batch Docs = %u  activeQueries = %-4u \n", mBatchId, mParsedDocs.size(), mActiveQueries.size());fflush(NULL);
            mBatchId++;

            pthread_mutex_lock(&mPendingDocs_mutex);
            if (mPhase!=PH_FINISHED) mPhase=PH_IDLE;
            pthread_cond_broadcast(&mPendingDocs_cond);
            pthread_mutex_unlock(&mPendingDocs_mutex);

            pthread_mutex_lock(&mReadyDocs_mutex);
            for (Document &D : mParsedDocs)
            {
                mReadyDocs.push(D);

            }

            pthread_cond_broadcast(&mReadyDocs_cond);
            pthread_mutex_unlock(&mReadyDocs_mutex);
            mBatchWords.clear();
            mParsedDocs.clear();
        }

        pthread_barrier_wait(&mBarrier);
    }


    fprintf(stdout, "THREAD#%2ld EXITS \n", myThreadId); fflush(stdout);
    return NULL;
}

void ParseDoc(Document &doc, const long thread_id)
{
    unordered_map<QueryID, char> query_stats;
    query_stats.reserve(mActiveQueries.size());

    const char *c1, *c2 = doc.str;
    Word* nw; unsigned nw_index;

    while(*c2==' ') ++c2;                                       // Skip any spaces
    for (c1=c2;*c2;c1=c2+1) {                                   // For each document word
        do {++c2;} while (*c2!=' ' && *c2 );                    // Find end of string
        GWDB.insert(c1, c2, &nw_index, &nw);                    // We acquire the unique index for that word

        if (doc.words->insert(nw_index)) {                      // We store the word to the documents set of word indices
            for (QueryID qid : nw->querySet[MT_EXACT_MATCH]) {
                query_stats[qid]++;
            }
        }
    }

    for (auto qc : query_stats) {
        if (qc.second == mActiveQueries[qc.first].numWords) {
            doc.matchingQueries->insert(qc.first);
        }
    }

}
