#ifndef MODULE_STRUCTURE_CPP
#define MODULE_STRUCTURE_CPP

#include "ModuleStructure.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include <algorithm>

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

void ModuleStructure::addSensor(const SensorEntity& sensorEntity) {
    // ESP32-S3 modules have only one node
    if (nodes.empty()) {
        return;
    }
    
    Node& node = nodes[0];

    auto addOrReplace = [&](const Sensor& s) {
        for (auto& existing : node.sensors) {
            if (existing.uid == s.uid) {
                existing = s;
                return;
            }
        }
        node.sensors.push_back(s);
    };

    if (!sensorEntity.modbusRegisterConfigs.empty()) {
        // Multi-channel ModBus: one Sensor per register config
        for (const auto& config : sensorEntity.modbusRegisterConfigs) {
            Sensor sensor;
            sensor.uid = sensorEntity.uid + "." + (config.channel < 10 ? "0" : "") + std::to_string(config.channel);
            sensor.type = config.sensorType.empty() ? sensorEntity.sensorType : config.sensorType;
            sensor.pin = sensorEntity.pin;
            sensor.group = sensorEntity.modbusDeviceAddress;
            sensor.modelUid = sensorEntity.sensorTypeUid;
            addOrReplace(sensor);
        }
    } else if (!sensorEntity.i2cRegisterConfigs.empty()) {
        // Multi-channel I2C: one Sensor per register config
        for (const auto& config : sensorEntity.i2cRegisterConfigs) {
            Sensor sensor;
            sensor.uid = sensorEntity.uid + "." + (config.channel < 10 ? "0" : "") + std::to_string(config.channel);
            sensor.type = config.sensorType.empty() ? sensorEntity.sensorType : config.sensorType;
            sensor.pin = sensorEntity.pin;
            sensor.group = 1;
            sensor.modelUid = sensorEntity.sensorTypeUid;
            addOrReplace(sensor);
        }
    } else {
        // Single-channel sensor
        Sensor sensor;
        sensor.uid = sensorEntity.uid + ".01";
        sensor.type = sensorEntity.sensorType;
        sensor.pin = sensorEntity.pin;
        sensor.group = 1;
        sensor.modelUid = sensorEntity.sensorTypeUid;
        addOrReplace(sensor);
    }
}

void ModuleStructure::removeSensorsByPin(int pin) {
    // ESP32-S3 modules have only one node
    if (nodes.empty()) {
        return;
    }
    
    Node& node = nodes[0];
    
    // Remove all sensors with matching pin
    node.sensors.erase(
        std::remove_if(node.sensors.begin(), node.sensors.end(),
            [pin](const Sensor& sensor) {
                return sensor.pin == pin;
            }),
        node.sensors.end()
    );
}

#endif
