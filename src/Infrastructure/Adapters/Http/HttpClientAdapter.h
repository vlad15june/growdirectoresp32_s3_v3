#pragma once

#include <HTTPClient.h>
#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"

/**
 * @brief Implementation of IHTTPClientAdapter using ESP32's HTTPClient
 */
class HttpClientAdapter : public IHttpClientAdapter {
private:
    HTTPClient _client;
    bool _isConnected;
    
public:
    /**
     * @brief Construct a new Http Client Adapter
     * 
     * @param logger Optional logger for debug output
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
    bool begin(const String& url) override {
        
        if (_isConnected) {
            _client.end();
            _isConnected = false;
        }
        
        bool success = _client.begin(url);
        _isConnected = success;
        
        if (!success) {
            Serial.printf("HTTP: Failed to begin connection to %s", url.c_str());
        }
        
        return success;
    }
    
    /**
     * @brief Set a request header
     * 
     * @param headerName Name of the header
     * @param headerValue Value of the header
     */
    void addHeader(const String& headerName, const String& headerValue) override {
        _client.addHeader(headerName, headerValue);
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
    int POST(const String& payload) override {
        int responseCode = _client.POST(payload);
        return responseCode;
    }
    
    /**
     * @brief Get the response as a string
     * 
     * @return Response string
     */
    String getString() override {
        String response = _client.getString();
        return response;
    }
    
    /**
     * @brief End the HTTP connection and free resources
     */
    void end() override {
        _client.end();
        _isConnected = false;
    }
};
