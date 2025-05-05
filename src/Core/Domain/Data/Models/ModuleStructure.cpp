#include "ModuleStructure.h"
#include <algorithm>
#include <sstream>

// Helper function to extract value between two delimiters
static std::string extractValue(const std::string& src, const std::string& key) {
    auto keyPos = src.find("\"" + key + "\"");
    if (keyPos == std::string::npos) return "";
    auto colon = src.find(':', keyPos);
    if (colon == std::string::npos) return "";
    auto start = src.find_first_of("\"", colon + 1);
    if (start == std::string::npos) return "";
    auto end = src.find_first_of("\"", start + 1);
    if (end == std::string::npos) return "";
    return src.substr(start + 1, end - start - 1);
}

// Helper function to extract array section
static std::string extractArray(const std::string& src, const std::string& key) {
    auto keyPos = src.find("\"" + key + "\"");
    if (keyPos == std::string::npos) return "";
    auto start = src.find('[', keyPos);
    if (start == std::string::npos) return "";
    int depth = 1;
    auto end = start + 1;
    while (end < src.size() && depth > 0) {
        if (src[end] == '[') ++depth;
        if (src[end] == ']') --depth;
        ++end;
    }
    return src.substr(start, end - start);
}

// Helper function to split array items (top-level only)
static std::vector<std::string> splitObjects(const std::string& arrayStr) {
    std::vector<std::string> result;
    int depth = 0;
    size_t last = 0;
    for (size_t i = 0; i < arrayStr.size(); ++i) {
        if (arrayStr[i] == '{') {
            if (depth == 0) last = i;
            ++depth;
        } else if (arrayStr[i] == '}') {
            --depth;
            if (depth == 0) {
                result.push_back(arrayStr.substr(last, i - last + 1));
            }
        }
    }
    return result;
}

// Helper function to extract int value
static int extractIntValue(const std::string& src, const std::string& key) {
    auto keyPos = src.find("\"" + key + "\"");
    if (keyPos == std::string::npos) return 0;
    auto colon = src.find(':', keyPos);
    if (colon == std::string::npos) return 0;
    auto start = src.find_first_of("0123456789-", colon + 1);
    if (start == std::string::npos) return 0;
    auto end = src.find_first_not_of("0123456789-", start);
    return std::stoi(src.substr(start, end - start));
}

bool ModuleStructure::fromJson(const std::string& jsonStr) {
    nodes.clear();
    growdirectorUid = extractValue(jsonStr, "GrowdirectorUid");
    firmware = extractValue(jsonStr, "firmware");
    email = extractValue(jsonStr, "email");
    accountUid = extractValue(jsonStr, "accountUid");

    std::string nodesArr = extractArray(jsonStr, "nodes");
    auto nodeObjs = splitObjects(nodesArr);
    for (const auto& nodeStr : nodeObjs) {
        Node node;
        node.type = extractValue(nodeStr, "type");
        node.subtype = extractValue(nodeStr, "subtype");
        node.bootType = extractValue(nodeStr, "bootType");
        node.fileSystem = extractValue(nodeStr, "fileSystem");
        node.uid = extractValue(nodeStr, "uid");
        node.firmware = extractValue(nodeStr, "firmware");
        node.ssid = extractValue(nodeStr, "ssid");
        node.ip = extractValue(nodeStr, "ip");

        // Sensors
        std::string sensorsArr = extractArray(nodeStr, "sensors");
        auto sensorObjs = splitObjects(sensorsArr);
        for (const auto& sensorStr : sensorObjs) {
            Sensor sensor;
            sensor.uid = extractValue(sensorStr, "uid");
            sensor.type = extractValue(sensorStr, "type");
            sensor.pin = extractIntValue(sensorStr, "pin");
            node.sensors.push_back(sensor);
        }

        // Relays
        std::string relaysArr = extractArray(nodeStr, "relays");
        auto relayObjs = splitObjects(relaysArr);
        for (const auto& relayStr : relayObjs) {
            Relay relay;
            relay.uid = extractValue(relayStr, "uid");
            relay.pin = extractIntValue(relayStr, "pin");
            node.relays.push_back(relay);
        }

        // Pumps
        std::string pumpsArr = extractArray(nodeStr, "pumps");
        auto pumpObjs = splitObjects(pumpsArr);
        for (const auto& pumpStr : pumpObjs) {
            Pump pump;
            pump.uid = extractValue(pumpStr, "uid");
            pump.pin = extractIntValue(pumpStr, "pin");
            node.pumps.push_back(pump);
        }

        nodes.push_back(node);
    }
    return true;
}

std::string ModuleStructure::toJson() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"GrowdirectorUid\":\"" << growdirectorUid << "\"";
    if (!firmware.empty()) oss << ",\"firmware\":\"" << firmware << "\"";
    if (!email.empty()) oss << ",\"email\":\"" << email << "\"";
    if (!accountUid.empty()) oss << ",\"accountUid\":\"" << accountUid << "\"";
    oss << ",\"nodes\":[";
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node& node = nodes[i];
        oss << "{";
        oss << "\"type\":\"" << node.type << "\"";
        if (!node.subtype.empty()) oss << ",\"subtype\":\"" << node.subtype << "\"";
        if (!node.bootType.empty()) oss << ",\"bootType\":\"" << node.bootType << "\"";
        if (!node.fileSystem.empty()) oss << ",\"fileSystem\":\"" << node.fileSystem << "\"";
        oss << ",\"uid\":\"" << node.uid << "\"";
        if (!node.firmware.empty()) oss << ",\"firmware\":\"" << node.firmware << "\"";
        if (!node.ssid.empty()) oss << ",\"ssid\":\"" << node.ssid << "\"";
        if (!node.ip.empty()) oss << ",\"ip\":\"" << node.ip << "\"";

        // Sensors
        if (!node.sensors.empty()) {
            oss << ",\"sensors\":[";
            for (size_t j = 0; j < node.sensors.size(); ++j) {
                const Sensor& s = node.sensors[j];
                oss << "{\"uid\":\"" << s.uid << "\"";
                if (!s.type.empty()) oss << ",\"type\":\"" << s.type << "\"";
                oss << ",\"pin\":" << s.pin << "}";
                if (j + 1 < node.sensors.size()) oss << ",";
            }
            oss << "]";
        }

        // Relays
        if (!node.relays.empty()) {
            oss << ",\"relays\":[";
            for (size_t j = 0; j < node.relays.size(); ++j) {
                const Relay& r = node.relays[j];
                oss << "{\"uid\":\"" << r.uid << "\",\"pin\":" << r.pin << "}";
                if (j + 1 < node.relays.size()) oss << ",";
            }
            oss << "]";
        }

        // Pumps
        if (!node.pumps.empty()) {
            oss << ",\"pumps\":[";
            for (size_t j = 0; j < node.pumps.size(); ++j) {
                const Pump& p = node.pumps[j];
                oss << "{\"uid\":\"" << p.uid << "\",\"pin\":" << p.pin << "}";
                if (j + 1 < node.pumps.size()) oss << ",";
            }
            oss << "]";
        }

        oss << "}";
        if (i + 1 < nodes.size()) oss << ",";
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

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