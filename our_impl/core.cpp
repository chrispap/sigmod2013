#include "../include/core.h"
#include "core.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <list>
#include <queue>
#include <unistd.h>
#include <pthread.h>

using namespace std;

#define NUM_THREADS 11

/* Global Data */
volatile bool done;
pthread_t           threads[NUM_THREADS];
queue<PendingDoc>   pendingDocs;                   ///< Pending documents
pthread_mutex_t     pendingDocs_mutex;
pthread_cond_t      pendingDocs_condition;
queue<DocResult>    availableDocs;                 ///< Ready documents
pthread_mutex_t     availableDocs_mutex;
pthread_cond_t      availableDocs_condition;
vector<Query>       queries;                       ///< Active queries

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
    int t;
    for (t=0; t< NUM_THREADS; t++) {
        int rc = pthread_create(&threads[t], &attr, ThreadFunction, (void *)t);
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
    Query query;
    query.query_id=query_id;
    strcpy(query.str, query_str);
    query.match_type=match_type;
    query.match_dist=match_dist;
    queries.push_back(query);
    return EC_SUCCESS;
}

ErrorCode EndQuery(QueryID query_id)
{
    unsigned int i, n=queries.size();
    for(i=0;i<n;i++)
    {
        if(queries[i].query_id==query_id)
        {
            queries.erase(queries.begin()+i);
            break;
        }
    }
    return EC_SUCCESS;
}

ErrorCode MatchDocument(DocID doc_id, const char* doc_str)
{
    char *cur_doc_str;

    //int ret = posix_memalign((void**)&cur_doc_str, getpagesize(), (1+strlen(doc_str))*sizeof(char));

    cur_doc_str = (char *) malloc((1+strlen(doc_str)));

    if (!cur_doc_str){
        printf("Could not allocate memory. \n");
        exit(-1);
    }

    strcpy(cur_doc_str, doc_str);
    PendingDoc newDoc;
    newDoc.id=doc_id;
    newDoc.str=cur_doc_str;

    pthread_mutex_lock(&pendingDocs_mutex);
    while ( pendingDocs.size() > (unsigned)NUM_THREADS )
        pthread_cond_wait(&pendingDocs_condition, &pendingDocs_mutex);

    pendingDocs.push(newDoc);
    fprintf(stderr, "DocID: %u pushed \n", newDoc.id); fflush(stdout);
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

    *p_doc_id = res.id;
    *p_num_res = res.num_res;
    *p_query_ids = res.query_ids;

    fprintf(stderr, "DocID: %u returned \n", *p_doc_id); fflush(stdout);

    pthread_cond_broadcast(&availableDocs_condition);
    pthread_mutex_unlock(&availableDocs_mutex);
    return EC_SUCCESS;
}

/* Our Functions */
void *ThreadFunction(void *param)
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
        fprintf(stderr, "DocID: %u retrieved by Thread_%ld for matching \n", doc.id, myThreadId);
        fflush(stdout);
        pthread_cond_broadcast(&pendingDocs_condition);
        pthread_mutex_unlock(&pendingDocs_mutex);

        /* Process the document */
        list<QueryID> matched_query_ids;
        matched_query_ids.clear();
        Match(doc.str, matched_query_ids);
        free(doc.str);

        /* Create the result array */
        DocResult result;
        result.id=doc.id;
        result.num_res=matched_query_ids.size();
        result.query_ids=0;

        if(result.num_res){
            unsigned int i;
            list<unsigned int>::const_iterator qi;
            result.query_ids=(QueryID*)malloc(result.num_res*sizeof(QueryID));
            qi = matched_query_ids.begin();
            for(i=0;i<result.num_res;i++)
                result.query_ids[i] = *qi++;
        }

        /* Store the result */
        pthread_mutex_lock(&availableDocs_mutex);
        availableDocs.push(result);
        pthread_cond_broadcast(&availableDocs_condition);
        pthread_mutex_unlock(&availableDocs_mutex);
    }

    printf("Thread#%2ld: Exiting. \n", myThreadId); fflush(stdout);
    return NULL;
}

void Match(char *cur_doc_str, list<unsigned int> &query_ids)
{
    unsigned int i;
    unsigned int n=queries.size();

    char cur_query_word [MAX_WORD_LENGTH+1];
    char cur_doc_word [MAX_WORD_LENGTH+1];

    /* Iterate on all active queries to compare them with this new document */
    for(i=0;i<n;i++)
    {
        bool matching_query=true;
        Query* quer=&queries[i];

        int iq=0;
        while(quer->str[iq] && matching_query)
        {
            /* Skip any leading spaces */
            while(quer->str[iq]==' ') iq++;
            if(!quer->str[iq]) break;
            char* qword=&quer->str[iq];

            /* Find next space delimiter */
            int lq=iq;
            while(quer->str[iq] && quer->str[iq]!=' ') iq++;
            char qt=quer->str[iq];
            /// PROBLEM X-) quer->str[iq]=0; // Put a zero here to create zero terminated string in place
            lq=iq-lq;
            memcpy(cur_query_word, qword, lq);
            cur_query_word[lq] = 0;

            bool matching_word=false;

            int id=0;
            while(cur_doc_str[id] && !matching_word)
            {
                /* Skip any leading spaces */
                while(cur_doc_str[id]==' ') id++;
                if(!cur_doc_str[id]) break;
                char* dword=&cur_doc_str[id];

                /* Find next space delimiter */
                int ld=id;
                while(cur_doc_str[id] && cur_doc_str[id]!=' ') id++;
                char dt=cur_doc_str[id];
                /// PROBLEM X-) cur_doc_str[id]=0; // Put a zero here to create zero terminated string in place
                ld=id-ld;
                memcpy(cur_doc_word, dword, ld);
                cur_doc_word[ld] = 0;

                if(quer->match_type==MT_EXACT_MATCH) {
                    if(strcmp(cur_query_word, cur_doc_word)==0) matching_word=true;
                }
                else if(quer->match_type==MT_HAMMING_DIST) {
                    unsigned int num_mismatches=HammingDistance(cur_query_word, lq, cur_doc_word, ld);
                    if(num_mismatches<=quer->match_dist) matching_word=true;
                }
                else if(quer->match_type==MT_EDIT_DIST) {
                    unsigned int edit_dist=EditDistance(cur_query_word, lq, cur_doc_word, ld);
                    if(edit_dist<=quer->match_dist) matching_word=true;
                }

                cur_doc_str[id]=dt;
            }

            quer->str[iq]=qt;

            if(!matching_word)
            {
                // This query has a word that does not match any word in the document
                matching_query=false;
            }
        }

        if(matching_query)
        {
            // This query matches the document
            query_ids.push_back(quer->query_id);
        }
    }

    return;
}

/* Hepler Functions */
int EditDistance(char* a, int na, char* b, int nb)
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

unsigned int HammingDistance(char* a, int na, char* b, int nb)
{
    int j, oo=0x7FFFFFFF;
    if(na!=nb) return oo;

    unsigned int num_mismatches=0;
    for(j=0;j<na;j++) if(a[j]!=b[j]) num_mismatches++;

    return num_mismatches;
}
