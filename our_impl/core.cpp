#include <core.h>
#include "core.hpp"
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <queue>
#include <map>
#include <set>

#define HASH_SIZE    (1<<18)
#define NUM_THREADS  8

enum PHASE { PH_01, PH_02, PH_FINISHED };

using namespace std;

/* Function prototypes */
static void*    ThreadFunc  (void *param);
static void     ParseDoc    (PendingDoc doc, const long thread_id);

static WordHashTable        GWDB(HASH_SIZE);                ///< Here store pointers to e v e r y single word encountered

/* Documents */
static queue<DocResult>     mAvailableDocs;                 ///< Documents that have been completely processed and are ready for delivery.
static vector<PendingDoc>   mPendingDocs;                   ///< Documents that haven't yet been completely processed.
static unsigned             mPendingIndex;                  ///< Points the next Document that should be acquired for parsing
static IndexHashTable       mBatchWords(HASH_SIZE,false);     ///<

/* Queries */
static map<QueryID,Query>   mActiveQueries;                 ///< Active Query IDs
static set<Word*>           mActiveApproxWords;             ///< Active words for approximate matcing

/* Threading */
static PHASE                mPhase;                         ///< Indicates in which phase the threads should be.
static pthread_t            mThreads[NUM_THREADS];          ///< Thread objects
static pthread_mutex_t      mPendingDocs_mutex;             ///< Protect the access to pending Documents
static pthread_cond_t       mPendingDocs_cond;              ///<
static pthread_mutex_t      mAvailableDocs_mutex;           ///< Protect the access to ready Documents
static pthread_cond_t       mAvailableDocs_cond;            ///<
static pthread_barrier_t    mBarrier_DocBegin;              ///< OI BARRIERS POU PISTEYW OTI XREIAZONTAI
static pthread_barrier_t    mBarrier_DocsAnalyzed;          ///<     -//-
static pthread_barrier_t    mBarrier_ResultStored;          ///<     -//-

/* Library Functions */
ErrorCode InitializeIndex()
{
    /* Create the mThreads, which will enter the waiting state. */
    pthread_mutex_init(&mPendingDocs_mutex,   NULL);
    pthread_cond_init (&mPendingDocs_cond,    NULL);
    pthread_mutex_init(&mAvailableDocs_mutex, NULL);
    pthread_cond_init (&mAvailableDocs_cond,  NULL);
    pthread_barrier_init(&mBarrier_DocBegin,     NULL, NUM_THREADS);
    pthread_barrier_init(&mBarrier_DocsAnalyzed, NULL, NUM_THREADS);
    pthread_barrier_init(&mBarrier_ResultStored, NULL, NUM_THREADS);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    mPendingIndex = 0;
    mPhase = PH_01;

    for (long t=0; t< NUM_THREADS; t++) {
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

    while (1)
    {
        /* PHASE 1 */
        while (1)
        {
            pthread_mutex_lock(&mPendingDocs_mutex);
            while ( mPhase == PH_01 && (mPendingDocs.empty() || mPendingIndex>= mPendingDocs.size()) )
                pthread_cond_wait(&mPendingDocs_cond, &mPendingDocs_mutex);
            if ( mPendingDocs.empty() || mPendingIndex >= mPendingDocs.size()) {
                pthread_mutex_unlock(&mPendingDocs_mutex);
                break;
            }

            /* Get a document from the pending list */
            PendingDoc doc = mPendingDocs[mPendingIndex++];
            pthread_cond_broadcast(&mPendingDocs_cond);
            pthread_mutex_unlock(&mPendingDocs_mutex);

            /* Parse the document */
            ParseDoc(doc, myThreadId);

            if (doc.id == 3 ){
            for (unsigned index: doc.wordIndices->indexVec){
                printf("%s\n", GWDB.getWord(index)->txt);
                fflush(NULL);
            }
        }

            free(doc.str);
        }

        /* FINISH DETECTED */
        if (mPhase == PH_FINISHED) break;

        /* PHASE 2 */
        /*************************************************************************************************
        * Maria, se ayto to simeio sou dinw ta eksis:
        *
        *   @info: Kathe Word* exei 3 stl::set pou periexoun ta Queries sta opoia ayti i lexi periexetai.
        *           [1 set gia kathe match_type]
        *           San Word* tha vlepeis esy tis lekseis.
        *
        *   @static stl::vector mPendingDocs
        *       - Periexei ola ta document aytou tou BATCH.
        *       - Kathe document einai ena struct PendingDoc
        *       - To megethos tou mPendingDocs einai mPendingDocs.size()
        *
        *       - Gia na prospelaseis ola ta documents kaneis to eksis:
        *           for (int i=0 ; i<mPendingDocs.size() ; i++) {
        *               mPendingDocs[i].id                          // Ayto einai to id tou document
        *               mPendingDocs[i].wordIndices->indexVec;      // Ayto einai to stl::vector pou periexei ena index gia kathe lexi tou document ( without duplicates )
        *           }
        *
        *        - Gia na prospelaseis oles tis lekseis enos Document kaneis to eksis:
        *           for (unsigned index: mPendingDocs[i].wordIndices->indexVec){    //Anti gia to `:`  mporeis na valeis klassiko for me i apo 0 ews wordIndices->indexVec.size()
        *               Word* w = GWDB.getWord(index);
        *           }
        *
        *
        *   @SOS  ==>   Ayto pou prpei na xwriseis sta 8 kai na anatheseis sta thread sou
        *               einai to stl::vector `wordIndices->indexVec` tou kathe Document.
        *
        *
        *   @static stl::map<QueryID,Query> mActiveQueries
        *        - Periexei ola active queries.
        *        - An thes na ta prospelaseis to kaneis etsi:
        *           for (auto &q : mActiveQueries) {
        *               QueryID qid = q.first;
        *               Query query = q.second;
        *           }
        *        - An thes na vreis ena query an exeis to id tou mporeis na kaneis to eksis:
        *           QueryID qid;
        *           Query query = mActiveQueries[qid];
        *
        *   @static stl::set mActiveApproxWords
        *        - Oi lekseis twn queries pou periexontai se approximate queries ( hamming / edit )
        *        - Me aytes prepei na kaneis elegxo gia approximate matching oles tis lexeis twn document
        *        - Ayti i domi einai ayti pou prepei na allaksei me to trie pou ftiaxneis !!!
        *
        *   @static IndexHashTable mBatchWords
        *        - Edw prepei na vazeis tis lekseis twn documents gia na ksereis an tis exeis ksana synantisei sto trexon BATCH
        *        - Etsi tha elegxeis gia mia leksi.
        *           > Ypothetw oti exeis to `index` tis leksis pou to exeis parei apo to `wordIndices->indexVec` tou document.
        *           >
        *           >   unsigned index = ... ;
        *           >   bool first_occurence = mBatchWords.insert(index);
        *           >   if (first_occurence) {
        *           >       // Edw exeis mia leksi pou den exei ksana-emfanistei se ayto to BATCH,
        *           >   } else {
        *           >       // Edw mallon den tha kaneis tpt afou ayti i leksi exei emfanistei proigoumenws sto BATCH
        *           >   }
        *           >
        *           >   Otan teleiwseis ayto to batch prepei na kaneis clear to mBatchWords
        *           >       mBatchWords.clear();
        *           >
        *
        *
        *--------------------------------------------------------------------------------------------------
        *
        * Ayto pou prepei na kaneis xonrtika einai kati tetoio:
        *
        *   for (int d=0 ; d < plithos_apo_documents ; d++ ) {
        *       // Den xreiazetai kapoios sygxronismos, oso vriskontai ola ta thread  sto idio document,
        *       // arkei na eksasfalisoume oti se kathe document mpainoun ola mazi.
        *       // Arkei aytos o barrier nomizw:
        *
        *       pthread_barrier_wait(&mBarrier_DocBegin);
        *
        *       // Edw prepei kathe thread
        *       // na analysei to kommati
        *       // pou tou antistoixei
        *       // apo kathe document.
        *       // Einai simantiko kathe thread na parei diaforetiko kommati !!!
        *   }
        *
        **********************************************************************************************/

        pthread_barrier_wait(&mBarrier_DocsAnalyzed);

        /* LAST PHASE */
        /*********************************************************************************************
        * - Edw prepei na apothikeytoun ta results
        * - An thes na to kaneis me ena thread ayto enw ta alla na perimenoun,
        *   to parakatw tha se voithisei:
        *
        *   pthread_mutex_lock(&mAvailableDocs_mutex);
        *
        *   if (myThreadId==0) {
        *       for (int D=0 ; D < plithos_apo_documents ; D++ ) {
        *           // Create the result for document: D
        *           // sto struct doc pou antiprosopeyei to doc D prepei na ginei `delete wordIndices`
        *           mAvailableDocs.push(result);
        *       }
        *
        *       // Edw diagrafontai ta palia documents kai shmatodoteitai
        *       // to main thread pou kata pasa pithanotita perimenei ta apotelesmata
        *       // SOS: Oi 4 parakatw grammes prepei na ginoun me ayti tin seira!
        *
        *       mBatchWords.clear();
        *       mPendingDocs.clear();
        *       mPendingIndex=0;
        *       pthread_cond_broadcast(&mAvailableDocs_condition);
        *       pthread_mutex_unlock(&mAvailableDocs_mutex);
        *   }
        *
        **********************************************************************************************/

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
