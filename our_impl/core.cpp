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

enum PHASE { PH_01, PH_02, PH_FINISHED };

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

static unordered_map<pair<DocID,QueryID>, char> ResultsMap;

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
    mPhase = PH_01;

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

    c2 = query_str;
    while(*c2==' ') ++c2;                                       // Skip any spaces
    for (c1=c2;*c2;c1=c2+1) {                                   // For each query word
        do {++c2;} while (*c2!=' ' && *c2 );                    // Find end of string

        Word* nw;
        unsigned nw_index;
        GWDB.insert(c1, c2, &nw_index, &nw);                    // Store the word in the GWDB

        /* <maria> */
        mBatchWords.insert(nw_index);
        if(nw->querySet[MT_EXACT_MATCH].empty())
            nw->firstQuery = query_id;
        nw->querySet[match_type].insert(query_id);              // Update the appropriate query set based on the match_type
        /* </maria> */

        new_query.words[num_words] = nw;                        // Add the word to the query

        /*
        if (match_type != MT_EXACT_MATCH) {                     // ONLY for hamming | edit dist queries
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
    newDoc.wordIndices = new IndexHashTable(HASH_SIZE);

    pthread_mutex_lock(&mPendingDocs_mutex);
    mPendingDocs.push(newDoc);
    mPhase = PH_01;
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
    *p_num_res = res.numRes;
    *p_query_ids = res.matchingQueryIDs;
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
        /* PHASE 1 */
        while (1)
        {
            pthread_mutex_lock(&mPendingDocs_mutex);
            while (mPendingDocs.empty() && mPhase==PH_01 && mPhase!=PH_FINISHED)
                pthread_cond_wait(&mPendingDocs_cond, &mPendingDocs_mutex);
            if (mPendingDocs.empty() || mPhase==PH_FINISHED )
            {
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
            doc.thread_limit = ceil( (double) doc.wordIndices->indexVec.size()/NUM_THREADS);
            mParsedDocs.push_back(doc);
            pthread_mutex_unlock(&mParsedDocs_mutex);
        }

        /* <maria> */
        pthread_barrier_wait(&mBarrier);
        /* </maria> */

        /* FINISH DETECTED */
        if (mPhase == PH_FINISHED) break;

        /* PHASE 2 */
        /**
         * Here, we update the GWDB Table, so that every word in it(note that the words are unique), has a set of
         * the document ids where it appears. A corresponding Set exists in the word structure to keep track of
         * the queries where it also belongs. The process is done simultaneously, where its thread works on the word
         * set of a specific numbers of documents.
         */
        unsigned i;
        unsigned DocIndex = 0;
        while ( DocIndex < mParsedDocs.size() ) {
            for (unsigned threadInd = 0; threadInd < mParsedDocs[DocIndex].thread_limit ; threadInd++) {
                i = mParsedDocs[DocIndex].thread_limit*myThreadId + threadInd;
                if(i >= mParsedDocs[DocIndex].wordIndices->indexVec.size()) break;
                //~ mBatchWords.insert(mParsedDocs[DocIndex].wordIndices->indexVec[i]);
                Word *w = GWDB.getWord(mParsedDocs[DocIndex].wordIndices->indexVec[i]);
                if(w->docSet.empty())
                    w->firstDoc = mParsedDocs[DocIndex].id;
                w->docSet.insert(mParsedDocs[DocIndex].id);
            }
            DocIndex++;
            pthread_barrier_wait(&mBarrier);
        }

        pthread_barrier_wait(&mBarrier);

        /**
         * The actual matching between documents and quieries. Each thread evaluates a number of words in the GWDB,
         * based on the mBatchWords indexing.When a word in a document appears in a number of queries,
         * we update properly the TableofResults (rows describe documents, columns queries).
         * Be ware, we need synchronization in the updating of the table.
         */
        if (myThreadId==0)
        {
            unsigned thread_limit = (unsigned) ceil( (double) mBatchWords.indexVec.size()/NUM_THREADS);
            i = 0;
            set<DocID>::iterator itd, sd;
            set<QueryID>::iterator itq, sq;

            for (unsigned index = 0; index < thread_limit ; index++) {
                i = thread_limit*myThreadId + index;
                if(i >= mBatchWords.indexVec.size()) break;
                Word *w = GWDB.getWord(mBatchWords.indexVec[i]);
                for (itd = w->docSet.begin(); itd != w->docSet.end(); itd++) {
                    for (itq = w->querySet[MT_EXACT_MATCH].begin(); itq != w->querySet[MT_EXACT_MATCH].end(); itq++) {
                        ResultsMap[make_pair(*itd,*itq)]++;
                    }
                }
            }

            /**
             * Need barrier in order for us to be sure that the ResultsMap is completely updated
             * Every thread evaluates the possible matching between all the active queries and "its" documents.
             */
            i = 0;
            unsigned local_numbers_docs = (unsigned) ceil((double) mParsedDocs.size()/NUM_THREADS);
            int k;
            for ( unsigned index = 0; index < local_numbers_docs ; index++) {
                i = local_numbers_docs*myThreadId + index;
                if(i >= mParsedDocs.size()) break;
                Document &ready = mParsedDocs[i];
                ready.matchingQueryIDs = (QueryID *)malloc(mActiveQueries.size()*sizeof(QueryID));
                k=0;
                DocID firstkey = ready.id;
                for (std::map<QueryID,Query>::iterator it = mActiveQueries.begin(); it!=mActiveQueries.end(); ++it) {
                    if (ResultsMap[make_pair(firstkey,it->first)] == it->second.numWords) {
                        ready.matchingQueryIDs[k] = it->first;
                        k++;
                    }
                }

                ready.numRes = k;
            }

            /* END OF PHASE 2 */

            /*
            for (Document &D : mParsedDocs) {
                for (unsigned index : D.wordIndices->indexVec)
                    mTrie.insertWord(GWDB.getWord(index)->txt);
            }*/

            /* LAST PHASE */
            pthread_mutex_lock(&mReadyDocs_mutex);
            for (Document &D : mParsedDocs)
            {
                delete D.wordIndices;
                mReadyDocs.push(D);
            }

            pthread_cond_broadcast(&mReadyDocs_cond);
            pthread_mutex_unlock(&mReadyDocs_mutex);
            mBatchWords.clear();
            mParsedDocs.clear();
            ResultsMap.clear();
        }

        /* <maria> */
        pthread_barrier_wait(&mBarrier);
        /* </maria> */
    }

    pthread_barrier_wait(&mBarrier);
    fprintf(stdout, "THREAD#%2ld EXITS \n", myThreadId); fflush(stdout);
    return NULL;
}

void ParseDoc(Document &doc, const long thread_id)
{
    const char *c1, *c2 = doc.str;
    unsigned nw_index;
    Word* nw;

    while(*c2==' ') ++c2;                                       // Skip any spaces
    for (c1=c2;*c2;c1=c2+1) {                                   // For each document word
        do {++c2;} while (*c2!=' ' && *c2 );                    // Find end of string
        GWDB.insert(c1, c2, &nw_index, &nw);                    // We acquire the unique index for that word
        doc.wordIndices->insert(nw_index);                      // We store the word to the documents set of word indices
    }

}
