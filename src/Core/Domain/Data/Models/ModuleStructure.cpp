#ifndef MODULE_STRUCTURE_CPP
#define MODULE_STRUCTURE_CPP

#include "ModuleStructure.h"

Sensor* ModuleStructure::findSensorByUid(const std::string& sensorUid) {
    for (Node& node : nodes) {
        for (Sensor& sensor : node.sensors) {
            if (sensor.uid == sensorUid) {
                return &sensor;
            }
        }
    }
    return nullptr;
}

std::vector<Sensor*> ModuleStructure::findSensorsByType(const std::string& type) {
    std::vector<Sensor*> result;
    for (Node& node : nodes) {
        for (Sensor& sensor : node.sensors) {
            if (sensor.type == type) {
                result.push_back(&sensor);
            }
        }
    }
    return result;
}

Relay* ModuleStructure::findRelayByUid(const std::string& relayUid) {
    for (Node& node : nodes) {
        for (Relay& relay : node.relays) {
            if (relay.uid == relayUid) {
                return &relay;
            }
        }
    }
    return nullptr;
}

Pump* ModuleStructure::findPumpByUid(const std::string& pumpUid) {
    for (Node& node : nodes) {
        for (Pump& pump : node.pumps) {
            if (pump.uid == pumpUid) {
                return &pump;
            }
        }
    }
    return nullptr;
}

std::vector<Relay*> ModuleStructure::getAllRelays() {
    std::vector<Relay*> result;
    for (Node& node : nodes) {
        for (Relay& relay : node.relays) {
            result.push_back(&relay);
        }
    }
    return result;
}

std::vector<Pump*> ModuleStructure::getAllPumps() {
    std::vector<Pump*> result;
    for (Node& node : nodes) {
        for (Pump& pump : node.pumps) {
            result.push_back(&pump);
        }
    }
    return result;
}

std::string ModuleStructure::getDeviceType() const {
    if (nodes.empty()) {
        return "unknown";
    }
    
    // Use the subtype of the first node as device type
    for (const Node& node : nodes) {
        if (!node.subtype.empty()) {
            return node.subtype;
        }
    }
    
    return "unknown";
}

#endif