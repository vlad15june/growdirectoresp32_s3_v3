#pragma once

#include <Arduino.h>

#include "Infrastructure/Adapters/Http/IHttpBodySink.h"

// getStreamPtr() below hands back a WiFiClient, and a pointer needs no definition. Declaring the
// name here makes this header self-contained: on the device the definition arrives with
// <WiFiClientSecure.h> through HttpClientAdapter.h, and a caller that only ever talks to this
// interface - the firmware update path is the sole user of getStreamPtr() - needs no network
// stack at all.
class WiFiClient;

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
     * @brief Set the budget for opening the connection (TCP connect plus TLS handshake)
     *
     * Separate from setTimeout(), which only covers waiting for data on an already open
     * connection: a slow link fails the two stages with two different codes
     * (-1 on connect, -11 on read), so both budgets have to be given.
     *
     * @param timeout Timeout in milliseconds
     */
    virtual void setConnectTimeout(uint32_t timeout) = 0;
    
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
     * @brief Hand the response body to a sink in the pieces it arrives in
     *
     * The counterpart of getString() for an answer whose length is a property of the account
     * rather than of the firmware: a list of sensor types is ~1.4 KB per type, and getString()
     * needs all of it in one heap block before the caller sees the first byte. Here the caller
     * sees the body as the transport reads it and keeps only what it makes out of it.
     *
     * @param sink Where the pieces go; may end the transfer by answering false
     * @return true when the whole body reached the sink
     */
    virtual bool readBodyInto(IHttpBodySink& sink) = 0;

    /**
     * @brief Get the content length of the response
     *
     * @return Content length in bytes, -1 if unknown
     */
    virtual int getSize() = 0;
    
    /**
     * @brief Get a pointer to the WiFi client stream for reading data
     * 
     * @return Pointer to WiFiClient for streaming operations
     */
    virtual WiFiClient* getStreamPtr() = 0;
    
    /**
     * @brief Check if the connection is still active
     * 
     * @return true if connected
     */
    virtual bool connected() = 0;
    
    /**
     * @brief Read data from the stream
     * 
     * @param buffer Buffer to store the data
     * @param length Maximum number of bytes to read
     * @return Number of bytes actually read
     */
    virtual size_t readBytes(uint8_t* buffer, size_t length) = 0;
    
    /**
     * @brief Get the number of bytes available for reading
     * 
     * @return Number of available bytes
     */
    virtual int available() = 0;
    
    /**
     * @brief End the HTTP connection and free resources
     */
    virtual void end() = 0;
};
