#include <core.h>
#include "core.hpp"
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <unordered_map>
#include <queue>
#include <map>
#include <set>

enum PHASE { PH_01, PH_02, PH_FINISHED };

#define HASH_SIZE    (1<<18)
#define NUM_THREADS  8

using namespace std;

/* Function prototypes */
static int      HammingDist (Word *wa, Word *wb);
static int      EditDist    (Word *wa, Word *wb);
static void*    ThreadFunc  (void *param);
static void     ParseDoc    (PendingDoc doc, const long thread_id);
static void     Match       (  );

/* Global Word Data Base */
static WordHashTable        GWDB(HASH_SIZE);                ///< Here store pointers to e v e r y single word encountered !!!

/* Global Data for Documents that are still processed */
static vector<PendingDoc>   mPendingDocs;                   ///< Pending Documents
static int                  mPendingIndex;                  ///< Points the next Document that should be acquired for parsing

/* Global Data for Active Queries */
static map<QueryID,Query>   mActiveQueries;                 ///< Active Query IDs
static set<Word*>           mActiveApproxWords;             ///< Active words for approximate matcing

/* Global Data for Documents Ready for Delivery */
static queue<DocResult>     mAvailableDocs;                 ///< Ready Documents

/* Global Data for Threading */
static PHASE                mPhase;                         ///< Indicates in which phase the threads should be.
static pthread_t            mThreads[NUM_THREADS];          ///< Thread objects
static pthread_mutex_t      mPendingDocs_mutex;             ///< Protect the access to pending Documents
static pthread_cond_t       mPendingDocs_cond;              ///<    -//-
static pthread_mutex_t      mAvailableDocs_mutex;           ///< Protect the access to ready Documents
static pthread_cond_t       mAvailableDocs_cond;            ///<    -//-
static pthread_barrier_t    mBarrier_DocBegin;              ///< OI BARRIERS POU PISTEYW OTI XREIAZONTAI
static pthread_barrier_t    mBarrier_DocsAnalyzed;
static pthread_barrier_t    mBarrier_ResultStored;

/* Library Functions */
ErrorCode InitializeIndex()
{
    /* Create the mThreads, which will enter the waiting state. */
    pthread_mutex_init(&mPendingDocs_mutex, NULL);
    pthread_cond_init (&mPendingDocs_cond, NULL);
    pthread_mutex_init(&mAvailableDocs_mutex, NULL);
    pthread_cond_init (&mAvailableDocs_cond, NULL);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    mPendingIndex = 0;
    mPhase = PH_01;

    long t;
    for (t=0; t< NUM_THREADS; t++) {
        int rc = pthread_create(&mThreads[t], &attr, ThreadFunc, (void *)t);
        if (rc) { fprintf(stderr, "ERROR; return code from pthread_create() is %d\n", rc); exit(-1);}
    }

    return EC_SUCCESS;
}

ErrorCode DestroyIndex()
{
    pthread_mutex_lock(&mPendingDocs_mutex);
    pthread_cond_broadcast(&mPendingDocs_cond);
    pthread_mutex_unlock(&mPendingDocs_mutex);

    mPhase = PH_FINISHED;
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
        unsigned nw_index;
        GWDB.insert(c1, c2, &nw_index, &nw);                    // Store the word in the GWDB

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
    pthread_mutex_lock(&mPendingDocs_mutex);
    mPhase = PH_01;
    pthread_mutex_unlock(&mPendingDocs_mutex);

    char *new_doc_str = (char *) malloc((1+strlen(doc_str)));

    if (!new_doc_str){
        fprintf(stderr, "Could not allocate memory. \n");fflush(stderr);
        return EC_FAIL;
    }

    strcpy(new_doc_str, doc_str);
    PendingDoc newDoc;
    newDoc.id = doc_id;
    newDoc.str = new_doc_str;
    newDoc.wordIndices = new IndexHashTable(HASH_SIZE);

    pthread_mutex_lock(&mPendingDocs_mutex);
    mPendingDocs.push_back(newDoc);
    pthread_cond_broadcast(&mPendingDocs_cond);
    pthread_mutex_unlock(&mPendingDocs_mutex);

    return EC_SUCCESS;
}

ErrorCode GetNextAvailRes(DocID* p_doc_id, unsigned int* p_num_res, QueryID** p_query_ids)
{
    pthread_mutex_lock(&mPendingDocs_mutex);
    mPhase = PH_02;
    pthread_mutex_unlock(&mPendingDocs_mutex);

    pthread_mutex_lock(&mAvailableDocs_mutex);
    while ( mAvailableDocs.empty() )
        pthread_cond_wait(&mAvailableDocs_cond, &mAvailableDocs_mutex);

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

    pthread_cond_broadcast(&mAvailableDocs_cond);
    pthread_mutex_unlock(&mAvailableDocs_mutex);
    return EC_SUCCESS;
}

/* Our Functions */
void* ThreadFunc(void *param)
{
    long myThreadId = (long) param;
    fprintf(stderr, "Thread#%2ld: Starting. \n", myThreadId); fflush(stdout);

    /* MAIN LOOP */
    while (1)
    {
        /* PHASE 1 LOOP */
        while (1)
        {
            pthread_mutex_lock(&mPendingDocs_mutex);
            while ( mPhase == PH_01 && (mPendingDocs.empty() || mPendingIndex>= mPendingDocs.size()) )
                pthread_cond_wait(&mPendingDocs_cond, &mPendingDocs_mutex);
            if ( mPendingDocs.empty() || mPendingIndex >= mPendingDocs.size()) break;

            /* Get a document from the pending list */
            PendingDoc doc = mPendingDocs[mPendingIndex++]; // Pairnw ena document kai ayksanw to pending index
            pthread_cond_broadcast(&mPendingDocs_cond);
            pthread_mutex_unlock(&mPendingDocs_mutex);

            /* Parse the document */
            set<QueryID> matchingQueries;
            matchingQueries.clear();
            ParseDoc(doc, myThreadId);
            free(doc.str);
        }

        /* FINISH DETECTED */
        if (mPhase == PH_FINISHED)
        {
            pthread_mutex_unlock(&mPendingDocs_mutex);
            break;
        }

        /* PHASE 2 LOOP */
        /**************************************************************************************************
        for (int d=0 ; d < plithos_apo_documents ; d++ )
        {
            //~ Den xreiazetai kapoios sygxronismos, oso vriskontai ola ta thread  sto idio document,
            //~ arkei na eksasfalisoume oti se kathe document mpainoun ola mazi.
            //~ Arkei enas barrier edw:

            pthread_barrier_wait(&mBarrier_DocBegin);

            //~ Edw prepei kathe thread
            //~ na analysei to kommati
            //~ pou tou antistoixei
            //~ apo kathe document.
            //~ Einai simantiko kathe thread na parei diaforetiko kommati !!!
        }
        **************************************************************************************************/


        pthread_barrier_wait(&mBarrier_DocsAnalyzed);

        /* Thread [0]: Create and store the results */
        if (myThreadId==0)
        {
            /*
                pthread_mutex_lock(&mAvailableDocs_mutex);

                //~ Edw prepei na apothikeytoun ta results

                for (int d=0 ; d < plithos_apo_documents ; d++ )
                {
                    //~ Create the result
                    mAvailableDocs.push(result);
                }

                //~ Edw diagrafontai ta palia documents kai shmatodoteitai
                //~ to main thread pou kata pasa pithanotita perimenei ta apotelesmata
                //~ SOS: Oi 4 parakatw grammes prepei na ginoun me ayti tin seira!

                mPendingDocs.clear();
                mPendingIndex=0;
                pthread_cond_broadcast(&mAvailableDocs_condition);
                pthread_mutex_unlock(&mAvailableDocs_mutex);
            */
        }

        /* Threads [1-N]: Wait for thread 0 to finish storing the results */
        pthread_barrier_wait(&mBarrier_ResultStored);
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
        unsigned nw_index;
        GWDB.insert(c1, c2, &nw_index, &nw);                    // We acquire the unique index for that word

        doc.wordIndices->insert(nw_index);                      // We store the word to the documents set of word indices
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
