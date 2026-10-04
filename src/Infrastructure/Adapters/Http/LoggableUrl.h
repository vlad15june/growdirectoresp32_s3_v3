#pragma once

#include <cstring>

/**
 * @brief How much of a URL a log line may repeat, in one place for everyone who logs one.
 *
 * Every URL this firmware builds for the backend carries the account token in its query, and the
 * token is a long-lived secret: whoever reads it speaks to the backend as this module's account.
 * The query is therefore not logged - by the transport that builds the URL, nor by the client that
 * fails to open it. What is left of the query is no loss: the module UID travels in the payloads
 * and the status filters are the same on every request.
 *
 * The answer is a length rather than a string, so a caller prints with "%.*s" and needs no second
 * buffer for the shortened copy.
 */
namespace HttpLog
{
    /**
     * @brief Length of the part before the query: the whole string when there is no query.
     * @param url URL to measure; nullptr answers 0
     */
    inline int loggablePathLength(const char* url)
    {
        if (url == nullptr)
        {
            return 0;
        }
        const char* query = strchr(url, '?');
        return query != nullptr ? (int)(query - url) : (int)strlen(url);
    }
}
