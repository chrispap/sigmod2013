#include "../include/core.h"
#include "core.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <pthread.h>

#define NUM_THREAD 4

using namespace std;

/* Global Data */
pthread_t threads[NUM_THREAD];

vector<Document> pendingDocs;                   ///< Pending documents
pthread_mutex_t  pendingDocs_mutex;
pthread_cond_t   pendingDocs_condition;

vector<Document> readyDocs;                     ///< Ready documents
pthread_mutex_t  readyDocs_mutex;
pthread_cond_t   readyDocs_condition;

vector<Query> queries;                          ///< Active queries


/* Library Functions */
ErrorCode InitializeIndex()
{
    pthread_mutex_init(&pendingDocs_mutex, NULL);
    pthread_cond_init (&pendingDocs_condition, NULL);

    pthread_mutex_init(&readyDocs_mutex, NULL);
    pthread_cond_init (&readyDocs_condition, NULL);

    /* Create the threads, which will enter the waiting state. */
    int t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    for (t=0; t< NUM_THREAD; t++) {
        int rc = pthread_create(&threads[t], &attr, ThreadFunc, (void *)t);
        if (rc) {
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(-1);
        }
    }

    return EC_SUCCESS;
}

ErrorCode DestroyIndex()
{
    return EC_SUCCESS;
}

ErrorCode StartQuery(QueryID query_id, const char* query_str, MatchType match_type, unsigned int match_dist)
{
    Query query;
    query.query_id=query_id;
    strcpy(query.str, query_str);
    query.match_type=match_type;
    query.match_dist=match_dist;
    // Add this query to the active query set
    queries.push_back(query);
    return EC_SUCCESS;
}

ErrorCode EndQuery(QueryID query_id)
{
    // Remove this query from the active query set
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
    char cur_doc_str[MAX_DOC_LENGTH];
    strcpy(cur_doc_str, doc_str);

    unsigned int i, n=queries.size();
    vector<unsigned int> query_ids;

    // Iterate on all active queries to compare them with this new document
    for(i=0;i<n;i++)
    {
        bool matching_query=true;
        Query* quer=&queries[i];

        int iq=0;
        while(quer->str[iq] && matching_query)
        {
            while(quer->str[iq]==' ') iq++;
            if(!quer->str[iq]) break;
            char* qword=&quer->str[iq];

            int lq=iq;
            while(quer->str[iq] && quer->str[iq]!=' ') iq++;
            char qt=quer->str[iq];
            quer->str[iq]=0;
            lq=iq-lq;

            bool matching_word=false;

            int id=0;
            while(cur_doc_str[id] && !matching_word)
            {
                while(cur_doc_str[id]==' ') id++;
                if(!cur_doc_str[id]) break;
                char* dword=&cur_doc_str[id];

                int ld=id;
                while(cur_doc_str[id] && cur_doc_str[id]!=' ') id++;
                char dt=cur_doc_str[id];
                cur_doc_str[id]=0;

                ld=id-ld;

                if(quer->match_type==MT_EXACT_MATCH)
                {
                    if(strcmp(qword, dword)==0) matching_word=true;
                }
                else if(quer->match_type==MT_HAMMING_DIST)
                {
                    unsigned int num_mismatches=HammingDistance(qword, lq, dword, ld);
                    if(num_mismatches<=quer->match_dist) matching_word=true;
                }
                else if(quer->match_type==MT_EDIT_DIST)
                {
                    unsigned int edit_dist=EditDistance(qword, lq, dword, ld);
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

    Document doc;
    doc.doc_id=doc_id;
    doc.num_res=query_ids.size();
    doc.query_ids=0;
    if(doc.num_res) doc.query_ids=(unsigned int*)malloc(doc.num_res*sizeof(unsigned int));
    for(i=0;i<doc.num_res;i++) doc.query_ids[i]=query_ids[i];
    // Add this result to the set of undelivered results
    readyDocs.push_back(doc);

    return EC_SUCCESS;
}

ErrorCode GetNextAvailRes(DocID* p_doc_id, unsigned int* p_num_res, QueryID** p_query_ids)
{
    // Get the first undeliverd resuilt from "readyDocs" and return it
    *p_doc_id=0; *p_num_res=0; *p_query_ids=0;
    if(readyDocs.size()==0) return EC_NO_AVAIL_RES;
    *p_doc_id=readyDocs[0].doc_id; *p_num_res=readyDocs[0].num_res; *p_query_ids=readyDocs[0].query_ids;
    readyDocs.erase(readyDocs.begin());
    return EC_SUCCESS;
}

void *ThreadFunc(void *param)
{

    return NULL;
}

/* Hepler Functions */
/**
 * Computes edit distance between a null-terminated string "a" with length "na"
 * and a null-terminated string "b" with length "nb"
 */
int EditDistance(char* a, int na, char* b, int nb)
{
    int oo=0x7FFFFFFF;

    static int T[2][MAX_WORD_LENGTH+1];

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

/**
 * Computes Hamming distance between a null-terminated string "a" with length "na"
 *  and a null-terminated string "b" with length "nb"
 */
unsigned int HammingDistance(char* a, int na, char* b, int nb)
{
    int j, oo=0x7FFFFFFF;
    if(na!=nb) return oo;

    unsigned int num_mismatches=0;
    for(j=0;j<na;j++) if(a[j]!=b[j]) num_mismatches++;

    return num_mismatches;
}
