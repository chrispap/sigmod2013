#ifndef __SIGMOD_CORE_H_
#define __SIGMOD_CORE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* #DEFINES */
#define MAX_DOC_LENGTH (1<<22)
#define MAX_WORD_LENGTH 31
#define MIN_WORD_LENGTH 4
#define MAX_QUERY_WORDS 5
#define MAX_QUERY_LENGTH ((MAX_WORD_LENGTH+1)*MAX_QUERY_WORDS)

/* Typedefs */
typedef unsigned int QueryID;
typedef unsigned int DocID;
typedef enum { MT_EXACT_MATCH, MT_HAMMING_DIST, MT_EDIT_DIST } MatchType;
typedef enum { EC_SUCCESS, EC_NO_AVAIL_RES, EC_FAIL } ErrorCode;

/* Function prototypes */
ErrorCode InitializeIndex ();
ErrorCode DestroyIndex    ();
ErrorCode StartQuery      (QueryID query_id, const char* query_str, MatchType match_type, unsigned int match_dist);
ErrorCode EndQuery        (QueryID query_id);
ErrorCode MatchDocument   (DocID doc_id, const char* doc_str);
ErrorCode GetNextAvailRes (DocID* p_doc_id, unsigned int* p_num_res, QueryID** p_query_ids);

#ifdef __cplusplus
}
#endif

#endif // __SIGMOD_CORE_H_
