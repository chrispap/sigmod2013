#include <core.h>
#include "worddatabase.hpp"

using namespace std;

WordDatabase *wdb;

ErrorCode InitializeIndex()
{
    wdb = new WordDatabase();
    return EC_SUCCESS;
}

ErrorCode DestroyIndex()
{
    delete wdb;
    return EC_SUCCESS;
}

ErrorCode StartQuery(QueryID query_id, const char* query_str, MatchType match_type, unsigned int match_dist)
{
    return wdb->startQuery(query_id, query_str, match_type, match_dist);
}

ErrorCode EndQuery(QueryID query_id)
{
    return wdb->endQuery(query_id);
}

ErrorCode MatchDocument(DocID doc_id, const char* doc_str)
{
    return wdb->pushDocument(doc_id, doc_str);
}

ErrorCode GetNextAvailRes(DocID* p_doc_id, unsigned int* p_num_res, QueryID** p_query_ids)
{
    return wdb->getNextAvailRes(p_doc_id, p_num_res, p_query_ids);
}
