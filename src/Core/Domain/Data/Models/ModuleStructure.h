#ifndef MODULE_STRUCTURE_H
#define MODULE_STRUCTURE_H

#include <string>
#include <vector>
#include <Arduino.h>


class Sensor {
public:
    Sensor() = default;
    Sensor(const std::string& uid, const std::string& type, int pin)
        : uid(uid), type(type), pin(pin) {}

    std::string uid;
    std::string type;
    int pin;
};

class Relay {
public:
    Relay() = default;
    Relay(const std::string& uid, int pin)
        : uid(uid), pin(pin) {}

    std::string uid;
    int pin;
};

class Pump {
public:
    Pump() = default;
    Pump(const std::string& uid, int pin)
        : uid(uid), pin(pin) {}

    std::string uid;
    int pin;
};

class Node {
public:
    Node() = default;

    std::string type;
    std::string subtype;
    std::string bootType;
    std::string fileSystem;
    std::string uid;
    std::string firmware;
    std::string ssid;
    std::string ip;
    std::vector<Sensor> sensors;
    std::vector<Relay> relays;
    std::vector<Pump> pumps;
};

class ModuleStructure {
public:
    ModuleStructure() = default;
    
    // Parse from JSON string
    bool fromJson(const std::string& jsonStr);
    
    // Convert to JSON string
    std::string toJson() const;
    
    // Getters
    const std::string& getGrowdirectorUid() const { return growdirectorUid; }
    const std::string& getFirmware() const { return firmware; }
    const std::string& getEmail() const { return email; }
    const std::string& getAccountUid() const { return accountUid; }
    const std::vector<Node>& getNodes() const { return nodes; }
    
    // Find a sensor by UID
    Sensor* findSensorByUid(const std::string& sensorUid);
    
    // Find sensors by type
    std::vector<Sensor*> findSensorsByType(const std::string& type);
    
    // Find a relay by UID
    Relay* findRelayByUid(const std::string& relayUid);
    
    // Find a pump by UID
    Pump* findPumpByUid(const std::string& pumpUid);
    
    // Get all relays
    std::vector<Relay*> getAllRelays();
    
    // Get all pumps
    std::vector<Pump*> getAllPumps();
    
    // Helper method to detect device type
    std::string getDeviceType() const;

private:
    std::string growdirectorUid;
    std::string firmware;
    std::string email;
    std::string accountUid;
    std::vector<Node> nodes;
};

#endif // MODULE_STRUCTURE_H
