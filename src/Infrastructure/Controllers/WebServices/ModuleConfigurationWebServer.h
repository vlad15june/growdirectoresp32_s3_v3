#ifndef MODULE_CONFIGURATION_WEB_SERVER_H
#define MODULE_CONFIGURATION_WEB_SERVER_H

#include <WebServer.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Enums/ModuleModeEnum.h" // Added include for ModuleModeEnum
#include "Application/Interfaces/WebServices/IWebServerLooper.h" // Added include for IWebServerLooper

class ModuleConfigurationWebServer : public IWebServerLooper
{
private:
    WebServer &server;
    WiFiMulti &wifiMulti;
    ILogger *logger;
    IDebugSystem *debugSystem;
    ISettings *settings;
    IModuleConfig *moduleConfig;

    void log(const char *format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println(buffer);
        }
    }

    // Stub methods for handlers
    String getUid()
    {
        return WiFi.macAddress();
    }

    uint8_t getMode()
    {
        if (settings != nullptr)
        {
            return settings->getIfConfigured();
        }
        log("Warning: settings is null in getMode(), returning default value");
        return 250;
    }

    int getWifiStatus()
    {
        return WiFi.status();
    }

    bool isWifiConnected()
    {
        return WiFi.status() == WL_CONNECTED;
    }

    void handleReset()
    {
        if (settings != nullptr)
        {
            settings->setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);

            if (moduleConfig != nullptr)
            {
                moduleConfig->setRequestedModuleMode(ModuleModeEnum::CONFIGURED_NEED_RESTART);
            }
        }
        else
        {
            log("Warning: settings is null in handleReset(), cannot reset configuration");
        }
    }

    void saveWifiCredentials(const char *newSsid, const char *newPassword)
    {
        if (settings != nullptr)
        {
            settings->setSsid(newSsid);
            settings->setPassword(newPassword);
            log("Saved WiFi credentials for SSID: %s", newSsid);
        }
        else
        {
            log("Warning: settings is null in saveWifiCredentials(), cannot save credentials");
        }
    }

    void connectToWifi(const char *wifiSsid, const char *wifiPassword)
    {
        WiFi.begin(wifiSsid, wifiPassword);
    }

    void disconnectWifi()
    {
        WiFi.disconnect();
    }

    void saveServerConfig(const char *newUser, const char *newToken, const char *newServer,
                          const char *newSsid, const char *newPassword, const char *newAccountUid,
                          float newLat, float newLng)
    {
        if (settings != nullptr)
        {
            settings->setUser(newUser);
            settings->setToken(newToken);
            settings->setServerUrl(newServer);
            settings->setSsid(newSsid);
            settings->setPassword(newPassword);
            settings->setAccountUid(newAccountUid);
            settings->setLatitude(newLat);
            settings->setLongitude(newLng);

            log("Saved server configuration for user: %s, server: %s", newUser, newServer);
        }
        else
        {
            log("Warning: settings is null in saveServerConfig(), cannot save server configuration");
        }
    }

public:
    ModuleConfigurationWebServer(
        WebServer &webServer,
        WiFiMulti &wifiMultiRef,
        ILogger *loggerInstance = nullptr,
        IDebugSystem *debugSystemInstance = nullptr,
        ISettings *settingsInstance = nullptr,
        IModuleConfig *moduleConfigInstance = nullptr,
        bool setupDebug = true) : server(webServer),
                                  wifiMulti(wifiMultiRef),
                                  logger(loggerInstance),
                                  debugSystem(debugSystemInstance),
                                  settings(settingsInstance),
                                  moduleConfig(moduleConfigInstance) {}

    void setupWebServer()
    {
        log("SETUP: setupWebServer");

        // Test endpoint
        server.on("/test", HTTP_OPTIONS, [this]()
                  {
            log("handle /test options");
            server.send(204); });

        server.on("/test", HTTP_GET, [this]()
                  {
            log("handle /test");
            server.send(200); });

        // Mode endpoint
        server.on("/mode", HTTP_GET, [this]()
                  {
            log("handle /mode");
            StaticJsonDocument<100> doc;
            doc["mode"] = getMode();
            char json[100];
            serializeJson(doc, json);
            server.send_P(200, "application/json", json); });

        // UID endpoint
        server.on("/uid", HTTP_GET, [this]()
                  {
            log("handle /uid");
            StaticJsonDocument<100> doc;
            doc["uid"] = getUid();
            char json[100];
            serializeJson(doc, json);
            server.send_P(200, "application/json", json); });

        // Post endpoint
        server.on("/post", HTTP_POST, [this]()
                  {
            String body = server.arg("plain");
            log(body.c_str());
            server.send(200); });

        // Reset endpoint
        server.on("/reset", HTTP_POST, [this]()
                  {
            log("handle /reset");
            handleReset();
            server.send(200); });

        // Blink endpoint
        server.on("/blink", HTTP_POST, [this]()
                  {
            log("handle /blink");
            server.send(200); });

        // Connect to WiFi endpoint
        server.on("/connect_to_wifi", HTTP_OPTIONS, [this]()
                  {
            log("handle /connect_to_wifi options");
            server.send(204); });

        server.on("/connect_to_wifi", HTTP_POST, [this]()
                  {
            log("handle /connect_to_wifi");
            String body = server.arg("plain");
            log(body.c_str());

            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, body);
            
            if (error) {
                log("Failed to parse JSON");
                server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            
            if (!doc.containsKey("ssid") || !doc.containsKey("password")) {
                log("Missing required fields: ssid or password");
                server.send(400, "application/json", "{\"error\":\"Missing required fields\"}");
                return;
            }

            saveWifiCredentials(doc["ssid"], doc["password"]);
            
            log("Connected to WiFi - ok");
            log("ok, send 200");
            server.send(200); });

        // Add WiFi connection endpoint
        server.on("/add_wifi_connection", HTTP_OPTIONS, [this]()
                  {
            log("handle /add_wifi_connection options");
            server.send(204); });

        server.on("/add_wifi_connection", HTTP_POST, [this]()
                  {
            log("handle /add_wifi_connection");
            String body = server.arg("plain");
            log(body.c_str());

            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, body);
            
            if (error) {
                log("Failed to parse JSON");
                server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            
            if (!doc.containsKey("ssid") || !doc.containsKey("password")) {
                log("Missing required fields: ssid or password");
                server.send(400, "application/json", "{\"error\":\"Missing required fields\"}");
                return;
            }

            saveWifiCredentials(doc["ssid"], doc["password"]);
            wifiMulti.addAP(doc["ssid"], doc["password"]);
            
            if (WiFi.status() != WL_CONNECTED) {
                wifiMulti.run();
                log("Connecting to WiFi..");
            }

            // addWifiToFile(doc["ssid"], doc["password"]);
            log("ok, send 200");
            server.send(200); });

        // Set module to configured endpoint
        server.on("/set_module_to_configured", HTTP_POST, [this]()
                  {
            log("handle /set_module_to_configured");
            
            if (settings != nullptr && settings->getIfConfigured() == ModuleModeEnum::USER_CAN_CONFIGURE) {
                settings->setConfigured(ModuleModeEnum::CONFIGURED_DONE);
                
                if (moduleConfig != nullptr) {
                    moduleConfig->setRequestedModuleMode(ModuleModeEnum::CONFIGURED_NEED_RESTART);
                }
                
                log("ok, send 200");
                server.send(200);
                return;
            }

            log("ok, send 400");
            server.send(400); });

        // Connect to WiFi temp endpoint
        server.on("/connect_to_wifi_temp", HTTP_OPTIONS, [this]()
                  {
            log("handle /connect_to_wifi options");
            server.send(204); });

        server.on("/connect_to_wifi_temp", HTTP_POST, [this]()
                  {
            log("handle /connect_to_wifi_temp");
            String body = server.arg("plain");
            log(body.c_str());

            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, body);
            
            if (error) {
                log("Failed to parse JSON");
                server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            
            if (!doc.containsKey("ssid") || !doc.containsKey("password")) {
                log("Missing required fields: ssid or password");
                server.send(400, "application/json", "{\"error\":\"Missing required fields\"}");
                return;
            }

            char ssid_temp[BUFSIZE];
            char password_temp[BUFSIZE];

            strcpy(ssid_temp, doc["ssid"]);
            strcpy(password_temp, doc["password"]);
            
            connectToWifi(ssid_temp, password_temp);
            
            log("ok, send 200");
            server.send(200); });

        // Disconnect WiFi endpoint
        server.on("/disconnect_wifi", HTTP_POST, [this]()
                  {
            log("handle /disconnect_wifi");
            log("ok, send 200");
            server.send(200);
            
            disconnectWifi();
            
            log("disconnecting");
            if (WiFi.status() != WL_CONNECTED) {
                log("disconnected");
            } });

        // WiFi status endpoint
        server.on("/wifi/status", HTTP_OPTIONS, [this]()
                  {
            log("handle /wifi/status options");
            server.send(204); });

        server.on("/wifi/status", HTTP_GET, [this]()
                  {
            log("handle /wifi/status");
            StaticJsonDocument<100> doc;
            doc["status"] = getWifiStatus();
            doc["connected"] = isWifiConnected();
            char json[100];
            serializeJson(doc, json);
            log(json);
            log("ok, send 200");
            server.send_P(200, "application/json", json); });

        // Connect to server endpoint
        server.on("/connect_to_server", HTTP_OPTIONS, [this]()
                  {
            log("handle /connect_to_server options");
            server.send(204); });

        server.on("/connect_to_server", HTTP_POST, [this]()
                  {
            log("handle /connect_to_server");
            String body = server.arg("plain");
            log(body.c_str());

            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, body);
            
            if (error) {
                log("Failed to parse JSON");
                server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            
            if (!doc.containsKey("user") || !doc.containsKey("token") || !doc.containsKey("server") ||
                !doc.containsKey("ssid") || !doc.containsKey("password") || !doc.containsKey("accountUid") ||
                !doc.containsKey("lat") || !doc.containsKey("lng")) {
                log("Missing required fields in JSON");
                server.send(400, "application/json", "{\"error\":\"Missing required fields\"}");
                return;
            }

            saveServerConfig(
                doc["user"], doc["token"], doc["server"],
                doc["ssid"], doc["password"], doc["accountUid"],
                doc["lat"].as<float>(), doc["lng"].as<float>()
            );
            
            log("ok, send 200");
            server.send(200); });

        // Root endpoint
        server.on("/", HTTP_GET, [this]()
                  {
            server.sendHeader("Connection", "close");
            server.send(200); });

        log("SETUP: setupWebServer all endpoints configured");
    }

    void launchServer()
    {
        log("LAUNCH: Starting web server");
        server.begin();
        log("LAUNCH: Web server started successfully");
    }
    
    void loop()
    {
        server.handleClient();
    }
};

#endif // MODULE_CONFIGURATION_WEB_SERVER_H
