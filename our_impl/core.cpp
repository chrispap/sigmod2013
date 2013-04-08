#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <pthread.h>
#include <utility>
#include <unordered_map>
#include <queue>
#include <map>
#include <list>
#include <set>

using namespace std;

#include <core.h>
#include "indexHashTable.hpp"
#include "dfatrie.hpp"
#include "word.hpp"
#include "wordHashTable.hpp"
#include "core.hpp"

#define HASH_SIZE    (1<<20)
#define NUM_THREADS  24

enum PHASE { PH_IDLE, PH_01, PH_02, PH_FINISHED };

/* Function prototypes */
static void  printStats ();
static void* Thread (void *param);
static void  ParseDoc (Document &doc, const long thread_id);

/* Globals */
static WordHashTable        GWDB(HASH_SIZE);                ///< Here store pointers to  EVERY  single word encountered.
static IndexHashTable       mDWords(HASH_SIZE,1);           ///< Unique indices of words of ALL PAST Documents
static IndexHashTable       mBatchWords(HASH_SIZE,1);
static IndexHashTable       mQW[3] = {
                             IndexHashTable(HASH_SIZE, 0),  ///< Unique indices of words of EXACT_MATCH Queries
                             IndexHashTable(HASH_SIZE, 1),  ///< Unique indices of words of HAMM_MATCH Queries
                             IndexHashTable(HASH_SIZE, 1)}; ///< Unique indices of words of EDIT_MATCH Queries
static DFATrie              mQWTrie;

/* Documents */
static queue<Document>      mPendingDocs;                   ///< Documents that haven't yet been touched at all.
static vector<Document>     mParsedDocs;                    ///< Documents that have been parsed.
static queue<Document>      mReadyDocs;                     ///< Documents that have been completely processed and are ready for delivery.
static int                  mBatchId;

/* Queries */
static vector<Query>        mActiveQueries;                 ///< Active Query IDs.

/* Threading */
static volatile PHASE       mPhase;                         ///< Indicates in which phase the threads should be.
static pthread_t            mThreads[NUM_THREADS];          ///< Thread objects
static pthread_mutex_t      mParsedDocs_mutex;              ///<
static pthread_mutex_t      mPendingDocs_mutex;             ///<
static pthread_cond_t       mPendingDocs_cond;              ///<
static pthread_mutex_t      mReadyDocs_mutex;               ///<
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

    printStats();

    return EC_SUCCESS;
}

ErrorCode StartQuery(QueryID query_id, const char* query_str, MatchType match_type, unsigned int match_dist)
{
    WordText wtxt;
    const char *c2;
    int num_words=0, i;
    Word* nw;

    if (mActiveQueries.size() < query_id+1)
        mActiveQueries.resize(query_id+1);

    c2 = query_str-1;
    do {
        for (unsigned wi=0; wi<WUNITS_MAX; wi++) wtxt.ints[wi]=0;
        i=0; do {wtxt.chars[i++] = *++c2;} while (*c2!=' ' && *c2 );
        i--; while (i<MAX_WORD_LENGTH+1) wtxt.chars[i++] = 0;

        GWDB.insert(wtxt, &nw);
        mActiveQueries[query_id].words[num_words] = nw;
        if (match_type!=MT_EXACT_MATCH && mQW[match_type].insert(nw->gwdbIndex)) {
            nw->qwindex[match_type] = mQW[match_type].size()-1;     // The index of that word to the table of that specific match-type words.
            mQWTrie.insertWord(nw);
        }
        num_words++;
    } while (*c2);

    mActiveQueries[query_id].type = match_type;
    mActiveQueries[query_id].dist = match_dist;
    mActiveQueries[query_id].numWords = num_words;

    return EC_SUCCESS;
}

ErrorCode EndQuery(QueryID query_id)
{
    mActiveQueries[query_id].numWords=0;
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
    newDoc.words = new IndexHashTable(HASH_SIZE, 1);
    newDoc.matchingQueries = new vector<QueryID>();

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

    if (*p_num_res) {
        QueryID *mq = (QueryID*) malloc (*p_num_res * sizeof(QueryID));
        auto qi = res.matchingQueries->begin();
        for(unsigned i=0; i!=*p_num_res ; i++) mq[i] = *qi++;
        *p_query_ids = mq;
    }
    else *p_query_ids=NULL;

    delete res.matchingQueries;
    delete res.words;
    free(res.str);

    //~ fprintf(stderr, "Doc %-4u delivered \n", res.id);fflush(NULL);
    pthread_cond_broadcast(&mReadyDocs_cond);
    pthread_mutex_unlock(&mReadyDocs_mutex);
    return EC_SUCCESS;
}

/* Our Functions */
void printStats()
{
    fprintf(stdout, "\n=== STATS ================================== BATCH =============================================================================\n");
    fprintf(stdout, "GWDB     Exact   Hamming   Edit    Approx   |  BatchID   ActiveQueries   batchDocs   batchWords   \n");
    fprintf(stdout, "%-6u   %-5u   %-7u   %-5u   %-6u   |  %-7d   %-13lu   %-9lu   %-10u   \n",
                     GWDB.size(), mQW[0].size(), mQW[1].size(), mQW[2].size(), mQWTrie.size(), mBatchId, (unsigned long) mActiveQueries.size(), (unsigned long) mParsedDocs.size(), mBatchWords.size());
    fprintf(stdout, "================================================================================================================================\n");
    fflush(NULL);
}

void* Thread(void *param)
{
    long myThreadId = (long) param;
    //~ fprintf(stdout, ">> THREAD#%2ld STARTS \n", myThreadId); fflush(stdout);

    while (1)
    {
        pthread_barrier_wait(&mBarrier);
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

            /* Parse the document and append it to mParsedDocs */
            ParseDoc(doc, myThreadId);

            pthread_mutex_lock(&mParsedDocs_mutex);
            mParsedDocs.push_back(doc);
            pthread_mutex_unlock(&mParsedDocs_mutex);
        }


        /* FINISH DETECTED */
        if (mPhase == PH_FINISHED) break;


        /* MATCHING PHASE */
        /** [00.]
         * Create batchWords
         */
        pthread_barrier_wait(&mBarrier);
        if (myThreadId==0)
        {
            pthread_mutex_lock(&mPendingDocs_mutex);
            if (mPhase!=PH_FINISHED) mPhase=PH_IDLE;
            pthread_cond_broadcast(&mPendingDocs_cond);
            pthread_mutex_unlock(&mPendingDocs_mutex);

            for (Document &doc : mParsedDocs)
                for (unsigned index : doc.words->indexVec)
                    mBatchWords.insert(index);
        }
        pthread_barrier_wait(&mBarrier);


        /** [01.]
         * For every dword of this batch, update its matching lists.
         */
        for (unsigned index = myThreadId ; index < mBatchWords.size() ; index += NUM_THREADS) {
            Word *dw = GWDB.getWord(mBatchWords.indexVec[index]);

            for (unsigned j=dw->lastCheck_edit ; j<mQW[MT_EDIT_DIST].size() ; j++) {
                Word* qw = GWDB.getWord(mQW[MT_EDIT_DIST].indexVec[j]);
                if (abs(qw->length - dw->length)<=3 && qw->letterDiff(dw)<=6) {
                    int dist = qw->EditDist(dw);
                    if (dist<=3) dw->editMatches[dist].push_back(j);
                }
            }
            dw->lastCheck_edit = mQW[MT_EDIT_DIST].size();

            for (unsigned j=dw->lastCheck_hamm ; j<mQW[MT_HAMMING_DIST].size() ; j++) {
                Word* qw = GWDB.getWord(mQW[MT_HAMMING_DIST].indexVec[j]);
                if (qw->length == dw->length && qw->letterDiff(dw)<=6) {
                    int dist = dw->HammingDist(qw);
                    if (dist<=3) dw->hammMatches[dist].push_back(j);
                }
            }
            dw->lastCheck_hamm = mQW[MT_HAMMING_DIST].size();

        }
        pthread_barrier_wait(&mBarrier);


        /** [02.]
         * Determine the matches and deliver docs
         */
        char* qwE = (char*) malloc(mQW[MT_EDIT_DIST].size());
        char* qwH = (char*) malloc(mQW[MT_HAMMING_DIST].size());

         for (unsigned index=myThreadId ; index < mParsedDocs.size() ; index += NUM_THREADS) {
             Document &doc = mParsedDocs[index];

            for (unsigned i=0 ; i<mQW[MT_EDIT_DIST].size() ; i++) qwE[i] = 10;
            for (unsigned i=0 ; i<mQW[MT_HAMMING_DIST].size() ; i++) qwH[i] = 10;

            for (unsigned index : doc.words->indexVec) {
                for (int k=3 ; k>=0 ; k--) {
                    for (unsigned qw : GWDB.getWord(index)->editMatches[k])
                        if (k < qwE[qw]) qwE[qw] = k;
                    for (unsigned qw : GWDB.getWord(index)->hammMatches[k])
                        if (k < qwH[qw]) qwH[qw] = k;
                }
            }

            for (unsigned qid=0 ; qid<mActiveQueries.size() ; qid++) {
                Query &Q = mActiveQueries[qid];
                if (Q.numWords==0) continue;

                int qwc=0;

                if (Q.type==MT_EXACT_MATCH)
                {
                    for (int qwi=0 ; qwi<Q.numWords ; qwi++)
                        if (doc.words->exists(Q.words[qwi]->gwdbIndex)) ++qwc;
                        else break;
                }
                else
                {
                    char *qwVec = Q.type==MT_EDIT_DIST ? qwE : qwH;
                    for (int qwi=0 ; qwi<Q.numWords ; qwi++)
                        if ( qwVec[Q.words[qwi]->qwindex[Q.type]] <= Q.dist) ++qwc;
                        else break;
                }

                if (qwc == Q.numWords) doc.matchingQueries->push_back(qid);
            }

            pthread_mutex_lock(&mReadyDocs_mutex);
            mReadyDocs.push(doc);
            pthread_cond_broadcast(&mReadyDocs_cond);
            pthread_mutex_unlock(&mReadyDocs_mutex);
        }

        free(qwE);
        free(qwH);
        pthread_barrier_wait(&mBarrier);


        /**
         * Finally, clean up our global structures
         */
        if (myThreadId==0)
        {
            //~ printStats();
            mParsedDocs.clear();
            mBatchWords.clear();
            mBatchId++;
        }
        //~ pthread_barrier_wait(&mBarrier);

    }

    //~ fprintf(stdout, ">> THREAD#%2ld EXITS \n", myThreadId); fflush(stdout);
    return NULL;
}

void ParseDoc(Document &doc, const long thread_id)
{
    WordText wtxt;
    const char *c2;
    int i;
    Word* nw;

    c2 = doc.str-1;
    do {
        for (unsigned wi=0; wi<WUNITS_MAX; wi++) wtxt.ints[wi]=0;
        i=0; do {wtxt.chars[i++] = *++c2;} while (*c2!=' ' && *c2 );
        i--; while (i<MAX_WORD_LENGTH+1) wtxt.chars[i++] = 0;

        GWDB.insert(wtxt, &nw);
        doc.words->insert(nw->gwdbIndex);

    } while (*c2);

}
