#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <queue>
#include <map>
#include <set>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <core.h>

#include "indexHashTable.hpp"
#include "automata.hpp"
#include "word.hpp"
#include "core.hpp"
#include "wordHashTable.hpp"

#define HASH_SIZE    (1<<18)
#define NUM_THREADS  8

enum PHASE { PH_IDLE, PH_01, PH_02, PH_FINISHED };

using namespace std;

/* Function prototypes */
static void* Thread (void *param);
static void  ParseDoc (Document &doc, const long thread_id);


/* Globals */
static WordHashTable        GWDB(HASH_SIZE);                ///< Here store pointers to  EVERY  single word encountered.
static IndexHashTable       mDWords(HASH_SIZE,0);           ///< Unique indices of words of ALL PAST Documents
static IndexHashTable       mBatchWords(HASH_SIZE,1);
static IndexHashTable       mQW[3] = {
                             IndexHashTable(HASH_SIZE, 0),  ///< Unique indices of words of EXACT_MATCH Queries
                             IndexHashTable(HASH_SIZE, 1),  ///< Unique indices of words of HAMM_MATCH Queries
                             IndexHashTable(HASH_SIZE, 1)}; ///< Unique indices of words of EDIT_MATCH Queries
static unsigned             mQWLastHamm=0;                  ///<
static unsigned             mQWLastEdit=0;                  ///<
static DFATrie              mDTrie;                         ///< A Trie containing the words of  ALL  Documents  EVER.
static DFATrie              mDTempTrie;                     ///< A Trie containing the words that appeared for first time in this Batch

/* Documents */
static queue<Document>      mPendingDocs;                   ///< Documents that haven't yet been touched at all.
static vector<Document>     mParsedDocs;                    ///< Documents that have been parsed.
static queue<Document>      mReadyDocs;                     ///< Documents that have been completely processed and are ready for delivery.
static int                  mBatchId;

/* Queries */
static map<QueryID,Query>   mActiveQueries;                 ///< Active Query IDs.

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

    fprintf(stdout, "\n=== WORDS ==========================================\n");
    fprintf(stdout, "GWDB    Exact   Hamming   Edit   DocWords   \n");
    fprintf(stdout, "%-5u   %-5u   %-7u   %-4u   %-8u   \n", GWDB.size(), mQW[0].size(), mQW[1].size(), mQW[2].size(), mDWords.size());
    fprintf(stdout, "====================================================\n");

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
        mQW[match_type].insert(nw_index);
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
    newDoc.words = new IndexHashTable(HASH_SIZE, 1);
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
    fprintf(stdout, ">> THREAD#%2ld STARTS \n", myThreadId); fflush(stdout);

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

            /* Parse the document and append it to mParsedDocs */
            ParseDoc(doc, myThreadId);
            free(doc.str);
            pthread_mutex_lock(&mParsedDocs_mutex);
            mParsedDocs.push_back(doc);
            pthread_mutex_unlock(&mParsedDocs_mutex);
        }

        /* FINISH DETECTED */
        if (mPhase == PH_FINISHED) break;

        pthread_barrier_wait(&mBarrier);

        /* MATCHING PHASE */
        unsigned threadRange, index;

        /* 1) New Query Words => Trie */
        threadRange = (unsigned) ceil( ((double) (mQW[MT_EDIT_DIST].indexVec.size() - mQWLastEdit)) / NUM_THREADS);
        for (unsigned i = 0; i < threadRange; i++) {
            index = threadRange*myThreadId + mQWLastEdit + i;
            vector<Word*> matchList;
            if(index >= mQW[MT_EDIT_DIST].indexVec.size()) break;
            Word *w = GWDB.getWord(mQW[MT_EDIT_DIST].indexVec[index]);
            if (w->dfa == NULL) w->dfa = new DFALevenstein(w->txt ,3);
            mDTrie.dfaIntersect(*w->dfa, matchList);
            //~ fprintf(stdout, " # %-10s was intersected with %-5u words by thread# %ld \n", w->txt, mDTrie.wordCount(), myThreadId);fflush(NULL);
        }
        pthread_barrier_wait(&mBarrier);

        /* 2) Append New Doc Words to the normal Trie and to a new one */
        if (myThreadId==0)
        {
            mQWLastEdit = mQW[MT_EDIT_DIST].size();
            mQWLastHamm = mQW[MT_HAMMING_DIST].size();

            for (unsigned index : mBatchWords.indexVec) {
                if (mDWords.insert(index)) {
                    Word* w = GWDB.getWord(index);
                    mDTrie.insertWord(w);
                    mDTempTrie.insertWord(w);
                }
            }

        }
        pthread_barrier_wait(&mBarrier);

        /* 3) All QWords => Temp Trie*/
        threadRange = (unsigned) ceil( ((double) (mQW[MT_EDIT_DIST].indexVec.size())) / NUM_THREADS);
        for (unsigned i = 0; i < threadRange; i++) {
            index = threadRange*myThreadId + i;
            vector<Word*> matchList;
            if(index >= mQW[MT_EDIT_DIST].indexVec.size()) break;
            Word *w = GWDB.getWord(mQW[MT_EDIT_DIST].indexVec[index]);
            if (w->dfa == NULL) w->dfa = new DFALevenstein(w->txt ,3);
            mDTempTrie.dfaIntersect(*w->dfa, matchList);
            //~ fprintf(stdout, " $ %-10s was intersected with %-5u words by thread# %ld \n", w->txt, mDTempTrie.wordCount(), myThreadId);fflush(NULL);
        }
        //~ pthread_barrier_wait(&mBarrier);

        /* LAST PHASE */
        if (myThreadId==0)
        {/*
            fprintf(stdout, ">> BATCH %-3d:  "
                "batch Docs = %u  |  "
                "activeQueries = %-4u  |  "
                "mQW(edit) = %-4u  |  "
                "newDocWords = %-4u  |  "
                "mDWords = %-5u  |  "
                "\n",
            mBatchId,
            mParsedDocs.size(),
            mActiveQueries.size(),
            mQW[2].size(),
            mDTempTrie.wordCount(),
            mDWords.size());
            fflush(NULL);*/

            pthread_mutex_lock(&mPendingDocs_mutex);
            if (mPhase!=PH_FINISHED) mPhase=PH_IDLE;
            pthread_cond_broadcast(&mPendingDocs_cond);
            pthread_mutex_unlock(&mPendingDocs_mutex);
            pthread_mutex_lock(&mReadyDocs_mutex);
            for (Document &D : mParsedDocs) mReadyDocs.push(D);
            pthread_cond_broadcast(&mReadyDocs_cond);
            pthread_mutex_unlock(&mReadyDocs_mutex);

            mParsedDocs.clear();
            mDTempTrie.clear();
            mBatchWords.clear();
            mBatchId++;
        }

        pthread_barrier_wait(&mBarrier);
    }

    fprintf(stdout, ">> THREAD#%2ld EXITS \n", myThreadId); fflush(stdout);
    return NULL;
}

void ParseDoc(Document &doc, const long thread_id)
{
    unordered_map<QueryID, char> query_stats;
    query_stats.reserve(mActiveQueries.size());

    const char *c1, *c2 = doc.str;
    Word* nw; unsigned nw_index;
    while(*c2==' ') ++c2;

    for (c1=c2;*c2;c1=c2+1) {
        do {++c2;} while (*c2!=' ' && *c2 );

        bool F1 = GWDB.insert(c1, c2, &nw_index, &nw);
        bool F2 = F1 ? false : mQW[MT_EXACT_MATCH].exists(nw_index);
        bool F3 = doc.words->insert(nw_index);

        if (F3) mBatchWords.insert(nw_index);
        if (!F1 &&  F2 &&  F3)
            for (QueryID qid : nw->querySet[MT_EXACT_MATCH])
                query_stats[qid]++;

    }

    for (auto qc : query_stats) {
        if (qc.second == mActiveQueries[qc.first].numWords) {
            doc.matchingQueries->insert(qc.first);
        }
    }

}

