#pragma once

#include <HTTPClient.h>
#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"

/**
 * @brief Implementation of IHttpClientAdapter using ESP32's HTTPClient
 */
class HttpClientAdapter : public IHttpClientAdapter {
private:
    HTTPClient _client;
    bool _isConnected;
    String _responseBuffer; // Internal buffer to store response
    
public:
    /**
     * @brief Construct a new Http Client Adapter
     */
    HttpClientAdapter() 
        : _isConnected(false) {
    }
    
    /**
     * @brief Destroy the Http Client Adapter
     */
    ~HttpClientAdapter() {
        if (_isConnected) {
            _client.end();
        }
    }
    
    /**
     * @brief Begin the HTTP connection
     * 
     * @param url Target URL for the HTTP request
     * @return true if successful
     */
    bool begin(const char* url) override {
        if (_isConnected) {
            _client.end();
            _isConnected = false;
        }
        
        // We need to convert to String for underlying HTTPClient
        String urlStr(url);
        bool success = _client.begin(urlStr);
        _isConnected = success;
        
        if (!success) {
            Serial.printf("HTTP: Failed to begin connection to %s", url);
        }
        
        return success;
    }
    
    /**
     * @brief Set a request header
     * 
     * @param headerName Name of the header
     * @param headerValue Value of the header
     */
    void addHeader(const char* headerName, const char* headerValue) override {
        // We need to convert to String for underlying HTTPClient
        _client.addHeader(String(headerName), String(headerValue));
    }
    
    /**
     * @brief Set request timeout
     * 
     * @param timeout Timeout in milliseconds
     */
    void setTimeout(uint32_t timeout) override {
        _client.setTimeout(timeout);
    }
    
    /**
     * @brief Make a GET request
     * 
     * @return HTTP response code
     */
    int GET() override {
        int responseCode = _client.GET();
        return responseCode;
    }
    
    /**
     * @brief Make a POST request with payload
     * 
     * @param payload Request body
     * @return HTTP response code
     */
    int POST(const char* payload) override {
        // We need to convert to String for underlying HTTPClient
        int responseCode = _client.POST(String(payload));
        return responseCode;
    }
    
    /**
     * @brief Get the response as a string
     * 
     * @return Response as a C-string
     */
    const char* getString() override {
        _responseBuffer = _client.getString();
        return _responseBuffer.c_str();
    }
    
    /**
     * @brief End the HTTP connection and free resources
     */
    void end() override {
        _client.end();
        _isConnected = false;
    }
};
