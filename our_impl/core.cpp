#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <pthread.h>
#include <utility>
#include <algorithm>
#include <unordered_map>
#include <queue>
#include <map>
#include <list>
#include <set>

#define HASH_SIZE    (1<<19)
#define NUM_THREADS  24

enum PHASE { PH_IDLE, PH_01, PH_02, PH_FINISHED };

using namespace std;

#include <core.h>
#include "word.hpp"
#include "wordHashTable.hpp"
#include "indexHashTable.hpp"
#include "core.hpp"

/* Function prototypes */
static void  PrintStats ();
static void* Thread (void *param);
static inline void  Prepare ();
static inline void  Match (long thread_id);
static inline void  Intersect (long thread_d);
static inline void  ParseDoc (Document &doc, const long thread_id);
static inline int   EditDist (char *ds, int dn, char *qs, unsigned qn, int *T, unsigned *qi);
static inline int   HammingDist (char *dtxt, char *qtxt);

/* Globals */
static WordHashTable        GWDB(HASH_SIZE);                ///< Here store pointers to  EVERY  single word encountered.
static IndexHashTable       mBatchWords(HASH_SIZE,1);

/* Documents */
static queue<Document>      mPendingDocs;                   ///< Documents that haven't yet been touched at all.
static vector<Document>     mParsedDocs;                    ///< Documents that have been parsed.
static queue<Document>      mReadyDocs;                     ///< Documents that have been completely processed and are ready for delivery.
static unsigned             mBatchId;

/* Queries */
static vector<Query>        mActiveQueries;
static IndexHashTable       mQWHash[2];
static vector<QWordE>       mQWEdit;
static unsigned             mQWLastEdit;
static vector<QWMap>        mQWordsHamm;

/* Threading */
static volatile PHASE       mPhase;                         ///< Indicates in which phase the threads should be.
static pthread_t            mThreads[NUM_THREADS];          ///<
static pthread_mutex_t      mParsedDocs_mutex;              ///<
static pthread_mutex_t      mPendingDocs_mutex;             ///<
static pthread_cond_t       mPendingDocs_cond;              ///<
static pthread_mutex_t      mReadyDocs_mutex;               ///<
static pthread_cond_t       mReadyDocs_cond;                ///<
static pthread_barrier_t    mBarrier;                       ///<

struct LTWE {
    bool operator()(const QWordE &qw1, const QWordE &qw2 ) const {
        return strcmp(qw1.txt.chars, qw2.txt.chars ) < 0;
    }
} ltw;

struct LTWH {
    bool operator()(const QWordH &qw1, const QWordH &qw2 ) const {
        return strcmp(qw1.txt.chars, qw2.txt.chars ) < 0;
    }
} ltwh;

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

    mQWordsHamm.resize(mBatchId+1);
    mPhase = PH_IDLE;

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

    PrintStats();

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
        wtxt.chars[--i] = 0;

        GWDB.insert(wtxt, &nw);
        mActiveQueries[query_id].words[num_words] = nw;
        if (match_type!=MT_EXACT_MATCH && mQWHash[match_type-1].insert(nw->gwdbIndex)) {
            nw->qwindex[match_type] = mQWHash[match_type-1].size()-1;
            if (match_type==MT_EDIT_DIST) mQWEdit.emplace_back(nw, match_type);
            else mQWordsHamm[mBatchId][nw->length].emplace_back(nw, match_type);
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
void PrintStats()
{
    fprintf(stdout, "\n=== STATS ================================== BATCH ===================================\n");
    fprintf(stdout, "GWDB     Exact   Hamming   Edit    |  BatchID   ActiveQueries   batchDocs   batchWords   \n");
    fprintf(stdout, "%-6u     -     %-7u   %-5u   |  %-7d   %-13lu   %-9lu   %-10u   \n",
                     GWDB.size(), mQWHash[0].size(), mQWHash[1].size(), mBatchId, (unsigned long) mActiveQueries.size(), (unsigned long) mParsedDocs.size(), mBatchWords.size());
    fprintf(stdout, "======================================================================================\n");
    fflush(NULL);
}

void* Thread(void *param)
{
    const long myThreadId = (long) param;

    while (1)
    {
        pthread_barrier_wait(&mBarrier);
        /** PHASE 01 */
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

        /* Finish detected, thread should exit. */
        if (mPhase == PH_FINISHED) break;
        pthread_barrier_wait(&mBarrier);

        /** PHASE 01 */
        if (myThreadId==0) Prepare();
        pthread_barrier_wait(&mBarrier);

        Intersect(myThreadId);
        pthread_barrier_wait(&mBarrier);

        Match(myThreadId);
        pthread_barrier_wait(&mBarrier);

        /* Batch completed */
        if (myThreadId==0) {
            mParsedDocs.clear();
            mBatchWords.clear();
        }

    }

    return NULL;
}

/** Parse the space separated words and discard duplicates */
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
        wtxt.chars[--i] = 0;

        GWDB.insert(wtxt, &nw);
        doc.words->insert(nw->gwdbIndex);

    } while (*c2);

}

/** Prepare the necessary structures for the intersection */
void Prepare()
{
    sort(mQWEdit.begin()+mQWLastEdit, mQWEdit.end(), ltw);

    if (mQWEdit.size() > mQWLastEdit+1) {
        char *s0 = mQWEdit[mQWLastEdit].txt.chars;
        for (unsigned j=mQWLastEdit+1; j<mQWEdit.size() ; j++) {
            char *s1 = mQWEdit[j].txt.chars;
            unsigned i=0;
            while (s0[i] == s1[i]) i++;
            mQWEdit[j].common_prefix = i;
            s0=s1;
        }
    }
    mQWLastEdit = mQWEdit.size();

    for (int len=MIN_WORD_LENGTH; len<=MAX_WORD_LENGTH; len++) {
        sort (mQWordsHamm[mBatchId][len].begin(), mQWordsHamm[mBatchId][len].end(), ltwh);

        //~ if (mQWordsHamm[mBatchId][len].size()>1) {
            //~ char *s0 = mQWordsHamm[mBatchId][len][0].txt.chars;
            //~ for (unsigned j=1; j<mQWordsHamm[mBatchId][len].size() ; j++) {
                //~ char *s1 = mQWordsHamm[mBatchId][len][j].txt.chars;
                //~ unsigned i=0;
                //~ while (s0[i] == s1[i]) i++;
                //~ mQWordsHamm[mBatchId][len][j].common_prefix = i;
                //~ s0=s1;
            //~ }
        //~ }
    }
    mBatchId++;
    mQWordsHamm.resize(mBatchId+1);

    pthread_mutex_lock(&mPendingDocs_mutex);
    if (mPhase!=PH_FINISHED) mPhase=PH_IDLE;
    pthread_cond_broadcast(&mPendingDocs_cond);
    pthread_mutex_unlock(&mPendingDocs_mutex);

    for (Document &doc : mParsedDocs)
        for (unsigned index : doc.words->indexVec)
            mBatchWords.insert(index);

}

/** For every dword of this batch, update its matching lists */
void Intersect(long myThreadId)
{
    int T[32*32];
    for (unsigned index = myThreadId ; index < mBatchWords.size() ; index += NUM_THREADS)
    {
        Word *wd = GWDB.getWord(mBatchWords.indexVec[index]);

        unsigned qi=0;
        WordText dtxt = wd->txt;
        unsigned last_check_edit = wd->lastCheck_edit;
        unsigned last_check_hamm = wd->lastCheck_hamm;
        int dn = wd->length;
        unsigned letter_bits = wd->letterBits;

        for (unsigned j=last_check_edit ; j<mQWEdit.size() ; j++) {
            QWordE &qw = mQWEdit[j];
            qi=min(qi, qw.common_prefix);
            if (abs(qw.length - dn)<=3 && Word::letterDiff(letter_bits, qw.letterBits)<=6) {
                int dist = EditDist(dtxt.chars, dn, qw.txt.chars, qw.length, T, &qi);
                if (dist<=3) wd->editMatches[dist].push_back(qw.qwindex);
            }

        }

        wd->lastCheck_edit = mQWEdit.size();
        for (unsigned j=last_check_hamm ; j<mBatchId ; j++) {
            for (QWordH &qw : mQWordsHamm[j][dn]) {
                if (Word::letterDiff(letter_bits, qw.letterBits)<=6) {
                    int dist = HammingDist(dtxt.chars, qw.txt.chars);
                    if (dist<=3) wd->hammMatches[dist].push_back(qw.qwindex);
                }
            }
        }
        wd->lastCheck_hamm = mBatchId;
    }
}

/** Determine the matches and deliver the results */
void Match(long myThreadId)
{
    char* qwH = (char*) malloc(mQWHash[MT_HAMMING_DIST-1].size());
    char* qwE = (char*) malloc(mQWHash[MT_EDIT_DIST-1].size());

    for (unsigned index=myThreadId ; index < mParsedDocs.size() ; index += NUM_THREADS)
    {
        Document &doc = mParsedDocs[index];

        for (unsigned i=0 ; i<mQWHash[MT_EDIT_DIST-1].size() ; i++) qwE[i] = 10;
        for (unsigned i=0 ; i<mQWHash[MT_HAMMING_DIST-1].size() ; i++) qwH[i] = 10;

        for (unsigned index : doc.words->indexVec) {
            Word *wd = GWDB.getWord(index);
            for (int k=3 ; k>=0 ; k--) {
                for (unsigned qw : wd->editMatches[k]) if (k < qwE[qw]) qwE[qw] = k;
                for (unsigned qw : wd->hammMatches[k]) if (k < qwH[qw]) qwH[qw] = k;
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
}

int EditDist(char *ds, int dn, char *qs, unsigned qn, int *T, unsigned *qi)
{
    int di, ret=0x7F;
    int diag_di=*qi+dn-qn;
    int *L = T+(*qi)*(dn+1);

    if (!(*qi)) for(di=0;di<=dn;di++) T[di]=di;
    else if (diag_di>0 && L[diag_di]>3) return 4;

    for((*qi)++;(*qi)<=qn;(*qi)++)
    {
        diag_di++;
        L+=(dn+1);
        L[0]=(*qi);

        for(di=1;di<=dn;di++)
        {
            L[di]=0x7F;
            ret =    L [di-dn-1]  +1;
            int d2 = L [di-1] +1;
            int d3 = L [di-dn-2]; if(qs[(*qi)-1]!=ds[di-1]) d3++;
            if(d2<ret) ret=d2;
            if(d3<ret) ret=d3;
            L[di]=ret;
        }

        if ((diag_di)>0 && L[diag_di]>3) return 4;
    }

    return ret;
}

int HammingDist(char *ds, char *qs)
{
    int num_mismatches = 0;
    int qi=0;

    while(qs[qi]) {
        if(ds[qi]!=qs[qi]) num_mismatches++;
        qi++;
        if (num_mismatches>3) return num_mismatches;
    }

    return num_mismatches;
}
