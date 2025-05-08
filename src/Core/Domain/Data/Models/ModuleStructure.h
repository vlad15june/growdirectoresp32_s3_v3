#ifndef MODULE_STRUCTURE_H
#define MODULE_STRUCTURE_H

#include <string>
#include <vector>

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
    
    std::string growdirectorUid;
    std::string firmware;
    std::string email;
    std::string accountUid;
    std::vector<Node> nodes;
    double latitude = 0.0;
    double longitude = 0.0;
    
    Sensor* findSensorByUid(const std::string& sensorUid);
    
    std::vector<Sensor*> findSensorsByType(const std::string& type);
    
    Relay* findRelayByUid(const std::string& relayUid);
    
    Pump* findPumpByUid(const std::string& pumpUid);
    
    std::vector<Relay*> getAllRelays();
    
    std::vector<Pump*> getAllPumps();
    
    std::string getDeviceType() const;

private:
    
};

#endif // MODULE_STRUCTURE_H
