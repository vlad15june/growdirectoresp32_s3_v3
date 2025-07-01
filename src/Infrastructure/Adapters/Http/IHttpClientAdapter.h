#pragma once

#include <Arduino.h>

/**
 * @brief Interface for HTTP client operations
 * 
 * This interface abstracts HTTP client operations to allow for 
 * easier testing and potential replacement of the HTTP client.
 */
class IHttpClientAdapter {
public:
    virtual ~IHttpClientAdapter() = default;
    
    /**
     * @brief Begin the HTTP connection
     * 
     * @param url Target URL for the HTTP request
     * @return true if successful
     */
    virtual bool begin(const char* url) = 0;
    
    /**
     * @brief Set a request header
     * 
     * @param headerName Name of the header
     * @param headerValue Value of the header
     */
    virtual void addHeader(const char* headerName, const char* headerValue) = 0;
    
    /**
     * @brief Set request timeout
     * 
     * @param timeout Timeout in milliseconds
     */
    virtual void setTimeout(uint32_t timeout) = 0;
    
    /**
     * @brief Make a GET request
     * 
     * @return HTTP response code
     */
    virtual int GET() = 0;
    
    /**
     * @brief Make a POST request with payload
     * 
     * @param payload Request body
     * @return HTTP response code
     */
    virtual int POST(const char* payload) = 0;
    
    /**
     * @brief Get the response as a string
     * 
     * @return Response as a C-string
     */
    virtual const char* getString() = 0;
    
    /**
     * @brief End the HTTP connection and free resources
     */
    virtual void end() = 0;
};
