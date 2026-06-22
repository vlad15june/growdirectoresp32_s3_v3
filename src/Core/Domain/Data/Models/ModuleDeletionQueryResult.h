#ifndef MODULE_DELETION_QUERY_RESULT_H
#define MODULE_DELETION_QUERY_RESULT_H

class ModuleDeletionQueryResult {
public:
    bool received200 = false;
    bool allTries403 = false;
    int isDeletedFlag = -1;
    bool queryFailed = false;
    int finalHttpCode = 0;
};

#endif // MODULE_DELETION_QUERY_RESULT_H
