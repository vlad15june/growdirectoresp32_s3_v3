#ifndef MODULE_STRUCTURE_H
#define MODULE_STRUCTURE_H

#include <string>
#include <vector>

// Forward declaration
class SensorEntity;

class Sensor {
public:
    Sensor() : pin(0), group(0) {}
    Sensor(const std::string& uid, const std::string& type, int pin)
        : uid(uid), type(type), pin(pin), group(0), modelUid("") {}

    std::string uid;
    std::string type;
    int pin;
    int group;
    std::string modelUid;
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
    Node() : chip("S3"), board(0) {}

    std::string type;
    std::string subtype;
    std::string bootType;
    std::string fileSystem;
    std::string psram;
    std::string uid;
    std::string firmware;
    std::string ssid;
    std::string ip;
    std::string chip;
    int board;
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
    
    /**
     * @brief Register channels of a sensor only - NOT the records the backend expects
     *
     * Computed channels (VPD) have no register config, so this method cannot know about them and
     * never adds them. Whoever fills the structure for publishing uses ISensorStructureUseCase
     * instead; adding a sensor here left it without its computed channel until the next restart
     * (GDV1-3782). Kept for the data-model tests, no production caller.
     */
    void addSensor(const SensorEntity& sensor);

    void removeSensorsByPin(int pin);

private:
    
};

#endif // MODULE_STRUCTURE_H
