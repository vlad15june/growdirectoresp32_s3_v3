#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"

/**
 * @brief Implementation of IHttpClientAdapter using ESP32's HTTPClient
 */
class HttpClientAdapter : public IHttpClientAdapter {
private:
    HTTPClient _client;
    WiFiClientSecure _secureClient;
    bool _isConnected;
    String _responseBuffer; // Internal buffer to store response

public:
    /**
     * @brief Construct a new Http Client Adapter
     */
    HttpClientAdapter()
        : _isConnected(false) {
        _secureClient.setInsecure();
        _client.setReuse(true);
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

        String urlStr(url);
        bool success = urlStr.startsWith("https")
            ? _client.begin(_secureClient, urlStr)
            : _client.begin(urlStr);
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
     * @brief Set the budget for opening the connection (TCP connect plus TLS handshake)
     *
     * @param timeout Timeout in milliseconds
     */
    void setConnectTimeout(uint32_t timeout) override {
        _client.setConnectTimeout((int32_t)timeout);
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
    
    /**
     * @brief Get the content length of the response
     * 
     * @return Content length in bytes, -1 if unknown
     */
    int getSize() override {
        return _client.getSize();
    }
    
    /**
     * @brief Get a pointer to the WiFi client stream for reading data
     * 
     * @return Pointer to WiFiClient for streaming operations
     */
    WiFiClient* getStreamPtr() override {
        return _client.getStreamPtr();
    }
    
    /**
     * @brief Check if the connection is still active
     * 
     * @return true if connected
     */
    bool connected() override {
        return _client.connected();
    }
    
    /**
     * @brief Read data from the stream
     * 
     * @param buffer Buffer to store the data
     * @param length Maximum number of bytes to read
     * @return Number of bytes actually read
     */
    size_t readBytes(uint8_t* buffer, size_t length) override {
        WiFiClient* stream = _client.getStreamPtr();
        if (stream != nullptr) {
            return stream->readBytes(buffer, length);
        }
        return 0;
    }
    
    /**
     * @brief Get the number of bytes available for reading
     * 
     * @return Number of available bytes
     */
    int available() override {
        WiFiClient* stream = _client.getStreamPtr();
        if (stream != nullptr) {
            return stream->available();
        }
        return 0;
    }
};
