// A change of the catalog reaches the module whole, and only a change does.
//
// The catalog synchronization stores the server's sensor models and gives the stored sensors the
// registers of their model (SyncSensorTypesUseCase). What it stores, what it writes the sensors
// with and when it sends the structure again is decided by comparing what the server published
// with what the module holds. These tests drive that comparison through the files it ends in -
// the catalog (/data/sensorTypes.json) and the sensor registry (/data/sensors.json) - and count the
// writes of each:
//
//   - models the server sends without a timestamp, as it sends a model nobody has edited since it
//     was created, and as the module holds them: nothing is written at any boot - on a module that
//     holds the catalog, and on a fresh one; numbers that went to flash and came back, and
//     sub-types listed in another order, are not taken for an edit either;
//   - an edit made around the platform, which leaves the model's timestamp where it was - the
//     registers alone (the migration that multiplied the ry-gw's lux by ten), the baud rate alone, or
//     the fields, the channel type and the sub-type another migration changed: it is stored, the
//     sensors follow it, and the structure goes out describing the channel by its new type;
//   - an edit that gives the sensors nothing new: the catalog is written, the sensors are not;
//   - an edit of the channels: the catalog is written once, the sensors of that model once each,
//     the structure goes out once, and a sensor of another model is not touched;
//   - a sensor whose storing the flash refused: no structure goes out for it, and the next
//     synchronization stores it - by comparing the sensor with its model, which is stored by then.
//
// Models with one to three channels: on this host a registry row of eight channels does not fit
// the document the converter reads a row with (pool slots are twice the size the board's are), so
// a wider model is tested over the registry in memory (test_readings_go_with_the_registers.cpp).
//
// What runs is production code: SyncSensorTypesUseCase wired as globals.h wires it - the fetch goes
// to the outbox, the answer is merged on the loop, the real SensorTypeCatalogWriter stores it on its
// own task - over the real SensorTypeRepository and SensorRepository, SpiffsSensorTypeDao and
// SpiffsSensorDao, and the converter the device runs, so every number goes to flash as text and
// comes back from it as the device reads it. The answer is server text read by the reader the
// transport reads it with (SensorTypeStreamReader, through ArduinoJsonConverter). The structure is
// rebuilt by the real RepublishModuleStructureUseCase over the real SensorStructureUseCase.
// Replaced are the flash device (Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h, which counts the
// rewrites of each file and can refuse one the way the device refuses one - the open fails), the
// outbox's queue and the server behind it, and time. A restart is a new module built over the
// same flash: the catalog and the registry it starts with are the ones the files hold.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/Storage/SensorTypeCatalogWriter.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorDao.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Repositories/SensorRepository.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"

#include "Mock/Application/MockOutbox.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::VirtualClock;

namespace {

const char* const MODULE_UID = "t107-module";
const char* const REGISTRY_FILE = "/data/sensors.json";
const char* const CATALOG_FILE = "/data/sensorTypes.json";

const char* const XF700A_UID = "5ec10934-c2d9-4eff-a637-e7e320ef855c";
const char* const RY_GW_UID = "500f6788-fc70-44fd-8e0f-42c00ae32fd8";
const char* const RY_WS12S_UID = "a54de3e2-b492-4262-8da2-2313f564d84f";

// ----------------------------------------------------------------------------------------------
// The server's answer, as text: the shape SensorModelHwDto is serialized in - camel case, the
// protocol by its name, no timestamp as null, members the module does not read beside the ones it
// does.
// ----------------------------------------------------------------------------------------------

struct Channel {
    int channel;
    int startRegister;
    const char* multiplier;
    const char* offset;
    const char* type;
    const char* name;
    const char* maxValue;
};

struct SubType {
    const char* name;
    const char* minValue;
    const char* maxValue;
    const char* step;
};

struct Model {
    const char* uid;
    const char* name;
    const char* displayName;
    const char* updatedAt; // nullptr: the server sends null
    int writeRegistersCommand;
    int modbusPingAddress;
    int registerToStoreModbusDeviceAddress;
    int modbusDeviceAddress;
    int baudRate = 9600;
    std::vector<Channel> channels;
    std::vector<SubType> subTypes;
};

std::string serverText(const Model& model)
{
    std::string text = "{";
    text += std::string("\"uid\":\"") + model.uid + "\",";
    text += "\"createdAt\":\"2026-04-21T08:05:59.388726Z\",";
    text += model.updatedAt != nullptr ? std::string("\"updatedAt\":\"") + model.updatedAt + "\","
                                       : std::string("\"updatedAt\":null,");
    text += "\"sensorTypes\":[";
    for (size_t i = 0; i < model.subTypes.size(); i++) {
        const SubType& sub = model.subTypes[i];
        text += std::string(i == 0 ? "" : ",") + "{\"name\":\"" + sub.name + "\",\"displayName\":\"" +
                sub.name + "\",\"isAnalog\":false,\"canCalibrate\":false,\"validCalibrationPeriodDays\":0,"
                "\"calibrationTimeInSeconds\":0,\"minValue\":" + sub.minValue + ",\"maxValue\":" +
                sub.maxValue + ",\"limitMinValue\":" + sub.minValue + ",\"limitMaxValue\":" + sub.maxValue +
                ",\"unit\":\"u\",\"step\":" + sub.step + ",\"precisionPercent\":2,\"precisionValue\":0.5}";
    }
    text += "],";
    text += std::string("\"name\":\"") + model.name + "\",\"displayName\":\"" + model.displayName + "\",";
    text += "\"canScan\":true,\"protocol\":\"Modbus\",\"isSupported\":true,\"canSelect\":true,"
            "\"isAvailableForSelection\":true,\"priorityForScan\":1,\"i2cAddress\":\"\",\"chipType\":null,";
    char numbers[200];
    snprintf(numbers, sizeof(numbers),
             "\"modbusDeviceAddress\":%d,\"writeRegistersCommand\":%d,\"modbusPingAddress\":%d,"
             "\"registerToStoreModbusDeviceAddress\":%d,\"baudRate\":%d,",
             model.modbusDeviceAddress, model.writeRegistersCommand, model.modbusPingAddress,
             model.registerToStoreModbusDeviceAddress, model.baudRate);
    text += numbers;
    text += "\"i2CRegisterConfigs\":[],\"modbusRegisterConfigs\":[";
    for (size_t i = 0; i < model.channels.size(); i++) {
        const Channel& c = model.channels[i];
        char head[96];
        snprintf(head, sizeof(head), "{\"functionCode\":3,\"startRegister\":%d,\"numRegisters\":1,",
                 c.startRegister);
        text += std::string(i == 0 ? "" : ",") + head + "\"multiplier\":" + c.multiplier +
                ",\"offset\":" + c.offset + ",\"channelName\":\"" + c.name + "\",\"channel\":" +
                std::to_string(c.channel) + ",\"sensorType\":\"" + c.type +
                "\",\"canConnectMinValue\":0,\"canConnectMaxValue\":" + c.maxValue + "}";
    }
    text += "],\"analogPinConfigs\":[]}";
    return text;
}

std::string serverText(const std::vector<Model>& models)
{
    std::string text = "[";
    for (size_t i = 0; i < models.size(); i++) {
        text += (i == 0 ? "" : ",") + serverText(models[i]);
    }
    return text + "]";
}

/** @brief The XF700A weather station before the platform's edit: eight channels. */
Model xf700aBefore(const char* updatedAt)
{
    Model model;
    model.uid = XF700A_UID;
    model.name = "XF700A";
    model.displayName = "Weather Station";
    model.updatedAt = updatedAt;
    model.writeRegistersCommand = 6;
    model.modbusPingAddress = 9;
    model.registerToStoreModbusDeviceAddress = 0;
    model.modbusDeviceAddress = 1;
    model.channels = {
        {0, 9, "0.01", "-40", "air_temp", "Ambient Temperature", "100"},
        {1, 10, "0.01", "0", "humidity", "Relative Humidity", "100"},
        {2, 11, "0.1", "0", "atmospheric_pressure_dig", "Atmospheric Pressure", "10000"},
        {3, 12, "0.01", "0", "wind_speed_dig", "Wind Speed", "100"},
        {4, 13, "0.1", "0", "wind_direction_dig", "Wind Direction", "360"},
        {5, 14, "0.1", "0", "rainfall_dig", "Rainfall", "1000"},
        {6, 15, "1", "0", "solar_radiation_dig", "Solar Radiation", "10000"},
        {7, 16, "0.01", "0", "lux", "Illuminance", "100000"},
    };
    model.subTypes = {
        {"air_temp", "-40", "80", "0.1"},
        {"humidity", "0", "100", "0.1"},
        {"atmospheric_pressure_dig", "300", "1100", "0.1"},
        {"wind_speed_dig", "0", "60", "0.01"},
        {"wind_direction_dig", "0", "360", "1"},
        {"rainfall_dig", "0", "1000", "0.1"},
        {"solar_radiation_dig", "0", "2000", "1"},
        {"lux", "5000", "60000", "1"},
    };
    return model;
}

/** @brief The XF700A as the edit left it: channel 6 gone, rainfall and illuminance x10. */
Model xf700aAfter(const char* updatedAt)
{
    Model model = xf700aBefore(updatedAt);
    model.channels.erase(model.channels.begin() + 6);
    model.channels[5].multiplier = "10";
    model.channels[6].multiplier = "10";
    model.subTypes.erase(model.subTypes.begin() + 6);
    return model;
}

const char* const RY_WS301_UID = "4719a986-a300-48d5-bd1f-e34a1329798a";

/** @brief The ry-gw light sensor: one channel, lux, on register 0, multiplied by `multiplier`. */
Model ryGw(const char* multiplier)
{
    Model model;
    model.uid = RY_GW_UID;
    model.name = "ry-gw";
    model.displayName = "Illuminance Sensor";
    model.updatedAt = "2026-02-26T14:59:49.817582Z";
    model.writeRegistersCommand = 16;
    model.modbusPingAddress = 1;
    model.registerToStoreModbusDeviceAddress = 1;
    model.modbusDeviceAddress = 255;
    model.channels = {{1, 0, multiplier, "0", "lux", "lux", "4000"}};
    model.subTypes = {{"lux", "5000", "60000", "1"}};
    return model;
}

/** @brief The RY-WS301: air temperature on channel 1, humidity on channel 2 - and so a VPD on 3. */
Model ryWs301(const char* updatedAt)
{
    Model model;
    model.uid = RY_WS301_UID;
    model.name = "ry_ws301";
    model.displayName = "RY-WS301 Environmental Sensor";
    model.updatedAt = updatedAt;
    model.writeRegistersCommand = 16;
    model.modbusPingAddress = 0;
    model.registerToStoreModbusDeviceAddress = 1;
    model.modbusDeviceAddress = 255;
    model.channels = {
        {1, 0, "0.01", "-40", "air_temp", "air_temp", "105"},
        {2, 1, "0.01", "0", "humidity", "humidity", "100"},
    };
    model.subTypes = {{"air_temp", "-40", "80", "0.1"}, {"humidity", "0", "100", "0.1"}};
    return model;
}

/**
 * @brief The RY-WS12S water sensor before the platform's migration of 08.07: its one channel typed
 *        water_level, and the fields the module writes a Modbus address with not set.
 */
Model ryWs12sBefore()
{
    Model model;
    model.uid = RY_WS12S_UID;
    model.name = "RY-WS12S";
    model.displayName = "RY-WS12S Temperature Sensor";
    model.updatedAt = "2026-03-09T10:23:33.876803Z";
    model.writeRegistersCommand = 0;
    model.modbusPingAddress = 0;
    model.registerToStoreModbusDeviceAddress = 0;
    model.modbusDeviceAddress = 1;
    model.channels = {{0, 0, "0.01", "-50", "water_level", "Water temp", "100"}};
    model.subTypes = {{"water_level", "0", "100", "0.1"}};
    return model;
}

/**
 * @brief What that migration made of it, in SQL and so with the timestamp where it was: the channel
 *        and the sub-type water_temp, and the Modbus-write fields set.
 */
Model ryWs12sAfter()
{
    Model model = ryWs12sBefore();
    model.writeRegistersCommand = 6;
    model.modbusPingAddress = 0;
    model.registerToStoreModbusDeviceAddress = 255;
    model.channels[0].type = "water_temp";
    model.subTypes = {{"water_temp", "0", "100", "0.1"}};
    return model;
}

std::string uidOf(int pin)
{
    char uid[48];
    snprintf(uid, sizeof(uid), "%s.%02d", MODULE_UID, pin);
    return std::string(uid);
}

std::string channelUid(int pin, int channel)
{
    char uid[56];
    snprintf(uid, sizeof(uid), "%s.%02d.%02d", MODULE_UID, pin, channel);
    return std::string(uid);
}

/**
 * @brief The flash device: files in memory, every rewrite of a file counted, and the rewrites of
 *        one file refused when told - the way the device refuses one: the open fails.
 */
class Flash : public MockSpiffsAdapter {
public:
    std::string refusedFile;

    std::unique_ptr<ISpiffsFileWriter> openForRewrite(const char* filePath) override
    {
        const std::string path = filePath != nullptr ? filePath : "";
        _rewrites[path]++;
        if (!refusedFile.empty() && path == refusedFile) {
            return nullptr;
        }
        return MockSpiffsAdapter::openForRewrite(filePath);
    }

    unsigned rewritesOf(const char* path) const
    {
        std::map<std::string, unsigned>::const_iterator it = _rewrites.find(path);
        return it != _rewrites.end() ? it->second : 0;
    }

private:
    std::map<std::string, unsigned> _rewrites;
};

/** @brief One life of the module over a flash: the catalog side wired the way globals.h wires it. */
struct Module {
    VirtualClock clock;
    HostTaskScheduler storageScheduler;
    SimArduino arduino;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;

    ArduinoJsonConverter json;
    SpiffsSensorDao sensorDao;
    SensorRepository sensors;
    SpiffsSensorTypeDao typeDao;
    SensorTypeRepository types;
    SensorTypeCatalogWriter writer;

    SensorValueModel values;
    ModuleStructure structure;
    MockHybridAPIService server;
    MockOutbox outbox;
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    RepublishModuleStructureUseCase republish;
    SyncSensorTypesUseCase sync;

    explicit Module(Flash& flash)
        : storageScheduler(clock),
          arduino(clock),
          moduleConfig(&logger, &debugSystem, &settings),
          sensorDao(&flash, &json),
          sensors(&sensorDao),
          typeDao(&flash, &json),
          types(&typeDao),
          writer(&typeDao, &storageScheduler, &logger, &debugSystem),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          republish(&structure, &sensors, &server, &sensorStructure, &logger, &debugSystem, &arduino),
          sync(&server, &types, &arduino, &timeAdapter, &json, &logger, &debugSystem, &sensors,
               &moduleConfig)
    {
        debugSystem.featureEnabled = false;
        timeAdapter.setEpochTime(1759500000);
        settings.setUser("t107@example.com");
        settings.setAccountUid("t107-account");
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setSensorPinCount(4);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setSettings(&settings);
        // The node setup builds before the control loop, and so before any synchronization the
        // loop makes.
        structure.nodes.push_back(Node());

        // As initDependencies() wires the synchronization.
        sync.setVirtualSensorChannelsUseCase(&virtualChannels);
        sync.setRepublishModuleStructureUseCase(&republish);
        sync.setSensorValueModel(&values);
        sync.setOutbox(&outbox);
        sync.setCatalogWriter(&writer);
    }

    /**
     * @brief The server answers a catalog fetch with this text, the way the device takes it: the
     *        transport reads it, the answer is merged on the loop, the storage task writes what is
     *        to be written, and the outcome of that write is applied on the loop.
     */
    void catalogArrives(const std::string& answerText)
    {
        fetchAnswered(answerText);
        if (sync.isWritePending()) {
            theWriteLands();
        }
        TEST_ASSERT_FALSE(sync.isWritePending());
        TEST_ASSERT_TRUE_MESSAGE(sync.getLastSyncStatus(), "the synchronization did not succeed");
    }

    /** @brief A catalog fetch, answered with this text and merged on the loop; nothing more. */
    void fetchAnswered(const std::string& answerText)
    {
        std::vector<SensorType> answer;
        TEST_ASSERT_TRUE_MESSAGE(json.sensorTypesFromJson(answerText, answer) == JsonListParse::Ok,
                                 "the stand's answer is not a catalog the transport reads");

        const uint32_t asked = outbox.sensorTypesRequests;
        TEST_ASSERT_TRUE(sync.forceSync());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(asked + 1, outbox.sensorTypesRequests,
                                         "the catalog was not asked for through the outbox");

        outbox.sensorTypesResponse = answer;
        outbox.sensorTypesResponseReady = true;
        OutboxCompletion fetched;
        fetched.kind = OutboxKind::SENSOR_TYPES_FETCH;
        fetched.ticket = outbox.lastIssuedTicket();
        fetched.delivered = true;
        fetched.attempts = 1;
        fetched.responseItemCount = static_cast<uint16_t>(answer.size());
        sync.onOutboxCompletion(fetched);
    }

    /** @brief The storage task writes the catalog it was handed, and the loop applies the outcome. */
    void theWriteLands()
    {
        storageScheduler.runForMs(100);
        std::vector<CatalogWriteCompletion> written;
        TEST_ASSERT_TRUE_MESSAGE(writer.drainResults(written), "the storage task did not finish the write");
        for (size_t i = 0; i < written.size(); i++) {
            sync.onCatalogWriteCompletion(written[i]);
        }
    }

    /** @brief The model as the transport reads it from the server's text. */
    SensorType read(const Model& model)
    {
        std::vector<SensorType> answer;
        TEST_ASSERT_TRUE(json.sensorTypesFromJson(serverText(std::vector<Model>(1, model)), answer) ==
                         JsonListParse::Ok);
        TEST_ASSERT_EQUAL_UINT(1, answer.size());
        return answer[0];
    }

    /** @brief A sensor of this model on this port, given the model's registers, stored. */
    void storeSensor(const Model& model, int pin)
    {
        const SensorType type = read(model);
        SensorEntity row;
        row.uid = uidOf(pin);
        row.pin = pin;
        row.sensorType = type.name;
        row.model = type.name;
        row.protocol = type.getProtocolString();
        row.sensorTypeUid = type.uid;
        row.modbusDeviceAddress = MODBUS_ADDRESS_MIN + pin;
        row.baudRate = type.baudRate;
        row.modbusRegisterConfigs = type.modbusRegisterConfig;
        row.isActive = true;
        row.isError = false;
        row.status = SENSOR_STATUS_ADDED;
        TEST_ASSERT_TRUE_MESSAGE(sensors.save(row), "the stand could not store the sensor");
    }

    SensorEntity sensorOn(int pin)
    {
        Utils::Optional<SensorEntity> row = sensors.get(uidOf(pin).c_str());
        TEST_ASSERT_TRUE_MESSAGE(row.has_value(), "no sensor stored on the port");
        return row.value();
    }

    /** @brief A reading of every channel of the sensor on the port, kept as the loop keeps one. */
    void readingsOf(int pin)
    {
        const SensorEntity row = sensorOn(pin);
        values.setSensorMultiChannel(row.uid, true);
        for (size_t i = 0; i < row.modbusRegisterConfigs.size(); i++) {
            values.setChannelValue(row.uid, channelUid(pin, row.modbusRegisterConfigs[i].channel), 1.0f);
        }
        values.setLastReadTime(row.uid, 1000);
        TEST_ASSERT_TRUE_MESSAGE(values.hasChannelValues(row.uid), "the premise: the sensor was read");
    }

    /** @brief The records of the structure on the port, sorted, as "uid:type". */
    std::string structureOfPort(int pin)
    {
        const std::string prefix = uidOf(pin) + ".";
        std::vector<std::string> records;
        for (size_t i = 0; i < structure.nodes[0].sensors.size(); i++) {
            const Sensor& record = structure.nodes[0].sensors[i];
            if (record.uid.compare(0, prefix.size(), prefix) == 0) {
                records.push_back(record.uid + ":" + record.type);
            }
        }
        std::sort(records.begin(), records.end());
        std::string text;
        for (size_t i = 0; i < records.size(); i++) {
            text += (i == 0 ? "" : " ") + records[i];
        }
        return text;
    }
};

/** @brief Writes of the two files over a span of the module's life. */
struct Writes {
    const Flash& flash;
    const unsigned catalogAtStart;
    const unsigned registryAtStart;

    explicit Writes(const Flash& f)
        : flash(f), catalogAtStart(f.rewritesOf(CATALOG_FILE)), registryAtStart(f.rewritesOf(REGISTRY_FILE))
    {
    }

    unsigned catalog() const { return flash.rewritesOf(CATALOG_FILE) - catalogAtStart; }
    unsigned registry() const { return flash.rewritesOf(REGISTRY_FILE) - registryAtStart; }
};

} // namespace

// --------------------------------------------------------------------------------------------
// 1. A model as the module holds it is not written again - with no timestamp too.
// --------------------------------------------------------------------------------------------

static std::vector<Model> twoModels(const Model& first, const Model& second)
{
    std::vector<Model> models;
    models.push_back(first);
    models.push_back(second);
    return models;
}

// The server sends a model nobody has edited since it was created with no timestamp at all: it sets
// one only when a model is modified. The module holds two such models - the XF700A weather station,
// and the RY-WS301, a sensor of which it reads on port 1. Every boot asks for the catalog, and the
// server sends the models as they were: three boots, and neither the catalog nor the registry is
// written once.
static void test_models_the_server_sends_without_a_timestamp_are_not_written_again_at_every_boot(void)
{
    const std::string answer = serverText(twoModels(xf700aBefore(nullptr), ryWs301(nullptr)));
    Flash flash;
    {
        Module module(flash);
        module.storeSensor(ryWs301(nullptr), 1);
        module.catalogArrives(answer);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, module.types.count(), "the premise: the module holds the models");
    }

    Writes writes(flash);
    for (int boot = 1; boot <= 3; boot++) {
        Module module(flash);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, module.types.count(),
                                      "the premise: the restarted module reads its catalog back");
        TEST_ASSERT_EQUAL_UINT_MESSAGE(2, module.sensorOn(1).modbusRegisterConfigs.size(),
                                       "the premise: the restarted module reads its sensor back");
        module.catalogArrives(answer);
    }

    printf("  t107: 3 boots with the models the module holds, no timestamp - catalog written %u time(s), "
           "registry %u time(s)\n", writes.catalog(), writes.registry());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.catalog(),
                                   "the catalog was written again for models the module holds");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.registry(),
                                   "the sensor was written again for a model the module holds");
}

// The same on a fresh module: nothing stored, neither a catalog nor a sensor. The first boot stores
// the catalog once; the boots after it store nothing.
static void test_a_fresh_module_stores_the_catalog_once(void)
{
    const std::string answer = serverText(twoModels(xf700aBefore(nullptr), ryWs301(nullptr)));
    Flash flash;
    Writes writes(flash);
    for (int boot = 1; boot <= 3; boot++) {
        Module module(flash);
        if (boot == 1) {
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.types.count(), "the premise: a fresh module holds no catalog");
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.sensors.count(), "the premise: a fresh module holds no sensor");
        }
        module.catalogArrives(answer);
    }

    printf("  t107: a fresh module, 3 boots - catalog written %u time(s), registry %u time(s)\n",
           writes.catalog(), writes.registry());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, writes.catalog(), "a fresh module stored the catalog more than once");
    TEST_ASSERT_EQUAL_UINT(0, writes.registry());
}

// Numbers come back from flash as the numbers that were stored, or as near them as the text keeps.
// Models whose channels carry a spread of multipliers, offsets and ranges - two of them with more
// digits than nine places after the point keep - and whose sub-types carry a spread of ranges and
// steps, go to flash with a sensor of one of them; after a restart the server sends them as they
// were, and nothing is written. A number read back as another number would make a model look edited
// at every boot.
static void test_the_numbers_a_restart_reads_back_are_the_ones_the_server_sent(void)
{
    Model station = xf700aBefore("2026-06-16T11:08:12.458594Z");
    const char* const multipliers[] = {"0.001", "0.1", "0.0123456789", "100", "0.25", "1.5", "0.0001", "3.14159265"};
    const char* const offsets[] = {"-40", "273.15", "-0.5", "-50", "0.001", "12.75", "-273.15", "1000000"};
    const char* const maxima[] = {"100000", "4000", "0.5", "65535", "1e6", "99.99", "2147483", "1.25"};
    for (size_t i = 0; i < station.channels.size(); i++) {
        station.channels[i].multiplier = multipliers[i];
        station.channels[i].offset = offsets[i];
        station.channels[i].maxValue = maxima[i];
    }
    const char* const steps[] = {"0.01", "0.05", "0.001", "0.2", "1", "0.5", "0.1", "0.3"};
    for (size_t i = 0; i < station.subTypes.size(); i++) {
        station.subTypes[i].step = steps[i];
    }
    station.subTypes[0].minValue = "-45.5";
    station.subTypes[2].maxValue = "1013.25";

    Model sensor = ryWs301("2026-03-09T07:54:15.312726Z");
    sensor.channels[0].multiplier = "0.000123456789";
    sensor.channels[0].offset = "-45.25";
    sensor.channels[0].maxValue = "123.456";
    sensor.channels[1].multiplier = "0.3";
    sensor.channels[1].offset = "0.07";
    sensor.channels[1].maxValue = "99.9";
    const std::string answer = serverText(twoModels(station, sensor));

    Flash flash;
    {
        Module module(flash);
        module.storeSensor(sensor, 1);
        module.catalogArrives(answer);
    }

    Writes writes(flash);
    Module module(flash);
    module.catalogArrives(answer);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.catalog(),
                                   "a number of a model read back from flash as another number");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.registry(),
                                   "a number of the sensor read back from flash as another number");
}

// The sub-sensor types of a model are the types it is linked to, and the server lists them out of
// that link. The same types in another order are the same model.
static void test_the_same_sub_types_in_another_order_are_the_same_model(void)
{
    Model station = xf700aBefore("2026-06-16T11:08:12.458594Z");
    Model sensor = ryWs301("2026-03-09T07:54:15.312726Z");
    Flash flash;
    {
        Module module(flash);
        module.storeSensor(sensor, 1);
        module.catalogArrives(serverText(twoModels(station, sensor)));
    }

    std::reverse(station.subTypes.begin(), station.subTypes.end());
    std::reverse(sensor.subTypes.begin(), sensor.subTypes.end());
    Writes writes(flash);
    Module module(flash);
    module.catalogArrives(serverText(twoModels(station, sensor)));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.catalog(), "the order of the sub-types was taken for an edit");
    TEST_ASSERT_EQUAL_UINT(0, writes.registry());
}

// --------------------------------------------------------------------------------------------
// 2. An edit reaches the module, with the timestamp where it was too.
// --------------------------------------------------------------------------------------------

// The platform's migration of 23.07 multiplied the ry-gw's lux by ten in SQL and left the model's
// timestamp where it was. The module that holds the ry-gw stores the model again, its sensor gets
// the new multiplier and loses what the old one read - and the next boot, with the model as it now
// is, writes nothing.
static void test_an_edit_of_the_registers_alone_reaches_the_module(void)
{
    Flash flash;
    {
        Module module(flash);
        module.storeSensor(ryGw("1"), 1);
        module.catalogArrives(serverText(std::vector<Model>(1, ryGw("1"))));
    }

    {
        Writes writes(flash);
        Module module(flash);
        module.readingsOf(1);
        module.catalogArrives(serverText(std::vector<Model>(1, ryGw("10"))));

        TEST_ASSERT_EQUAL_UINT_MESSAGE(1, writes.catalog(), "the edited model was not stored");
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(10.0f, module.sensorOn(1).modbusRegisterConfigs[0].multiplier,
                                        "the sensor of the edited model keeps the multiplier it had");
        TEST_ASSERT_EQUAL_UINT(1, writes.registry());
        TEST_ASSERT_FALSE_MESSAGE(module.values.hasChannelValues(uidOf(1)),
                                  "the sensor keeps what the old multiplier read");
    }

    Writes writes(flash);
    Module module(flash);
    module.catalogArrives(serverText(std::vector<Model>(1, ryGw("10"))));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.catalog(), "the model was stored again after it reached the module");
    TEST_ASSERT_EQUAL_UINT(0, writes.registry());
    TEST_ASSERT_EQUAL_FLOAT(10.0f, module.sensorOn(1).modbusRegisterConfigs[0].multiplier);
}

// A model edited in its baud rate alone: the channels as they were and the timestamp where it was, as
// an edit made in SQL leaves it. The sensor of the model is stored once with the baud rate its model
// has now and the structure does not go out - nothing a record describes moved. The restarted module
// reads that baud rate back from the registry, and the next synchronization, the same answer, writes
// neither the catalog nor the registry.
static void test_an_edit_of_the_baud_rate_alone_reaches_the_module(void)
{
    const Model slow = ryWs301("2026-03-09T07:54:15.312726Z");
    Model fast = slow;
    fast.baudRate = 19200;
    Flash flash;
    {
        Module module(flash);
        module.storeSensor(slow, 1);
        module.catalogArrives(serverText(std::vector<Model>(1, slow)));
    }

    {
        Writes writes(flash);
        Module module(flash);
        TEST_ASSERT_EQUAL_INT_MESSAGE(9600, module.sensorOn(1).baudRate,
                                      "the premise: the sensor is stored with the baud rate its model had");
        TEST_ASSERT_TRUE(module.republish.execute("boot"));
        module.server.sendToServerModuleStructureCallCount = 0;

        module.catalogArrives(serverText(std::vector<Model>(1, fast)));

        TEST_ASSERT_EQUAL_UINT_MESSAGE(1, writes.catalog(), "the edited model was not stored");
        TEST_ASSERT_EQUAL_INT_MESSAGE(19200, module.sensorOn(1).baudRate,
                                      "the sensor keeps the baud rate its model no longer has");
        TEST_ASSERT_EQUAL_UINT_MESSAGE(1, writes.registry(), "the registry was not written once for the sensor");
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.server.sendToServerModuleStructureCallCount,
                                      "the structure went out for a baud rate");
    }

    Writes writes(flash);
    Module module(flash);
    TEST_ASSERT_EQUAL_INT_MESSAGE(19200, module.sensorOn(1).baudRate,
                                  "the restarted module reads back the baud rate it had before the edit");
    module.catalogArrives(serverText(std::vector<Model>(1, fast)));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.catalog(), "the model was stored again after it reached the module");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.registry(), "the sensor was written again for a model it follows");
    TEST_ASSERT_EQUAL_INT(19200, module.sensorOn(1).baudRate);
}

// The platform's migration of 08.07, also in SQL: the RY-WS12S's one channel and its sub-type
// water_temp instead of water_level, and the fields the module writes a Modbus address with set. The
// module stores the model as it now is, the sensor takes the new type, and the structure goes out
// again describing the channel by it.
static void test_the_edits_a_migration_made_with_the_timestamp_where_it_was_reach_the_module(void)
{
    Flash flash;
    {
        Module module(flash);
        module.storeSensor(ryWs12sBefore(), 2);
        module.catalogArrives(serverText(std::vector<Model>(1, ryWs12sBefore())));
    }

    Writes writes(flash);
    {
        Module module(flash);
        TEST_ASSERT_TRUE(module.republish.execute("boot"));
        TEST_ASSERT_EQUAL_STRING_MESSAGE("t107-module.02.00:water_level", module.structureOfPort(2).c_str(),
                                         "the premise: the structure describes the channel by its old type");
        module.server.sendToServerModuleStructureCallCount = 0;

        module.catalogArrives(serverText(std::vector<Model>(1, ryWs12sAfter())));

        TEST_ASSERT_EQUAL_UINT_MESSAGE(1, writes.catalog(), "the migrated model was not stored");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("water_temp", module.sensorOn(2).modbusRegisterConfigs[0].sensorType.c_str(),
                                         "the sensor keeps the channel type the model no longer has");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.server.sendToServerModuleStructureCallCount,
                                      "the structure did not go out again");
        TEST_ASSERT_EQUAL_STRING("t107-module.02.00:water_temp", module.structureOfPort(2).c_str());
    }

    Module module(flash);
    const Utils::Optional<SensorType> held = module.types.get(RY_WS12S_UID);
    TEST_ASSERT_TRUE(held.has_value());
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, held.value().writeRegistersCommand,
                                  "the restarted module reads back the write command it had before");
    TEST_ASSERT_EQUAL_INT(255, held.value().registerToStoreModbusDeviceAddress);
    TEST_ASSERT_EQUAL_STRING("water_temp", held.value().sensorTypes[0].name.c_str());
}

// --------------------------------------------------------------------------------------------
// 3. Writes and structures: as many as the edit needs, no more.
// --------------------------------------------------------------------------------------------

// The catalog gives the RY-WS301, which the module reads on ports 1 and 2, a pressure channel on 3 -
// and with it the VPD moves to 4. The ry-gw on port 3 is of a model the answer carries as it was. One
// catalog write, one write of each RY-WS301, one structure - and the ry-gw's sensor and readings are
// not touched.
static void test_an_edit_writes_the_catalog_once_its_sensors_once_and_the_structure_once(void)
{
    Flash flash;
    {
        Module module(flash);
        module.storeSensor(ryWs301("2026-03-09T07:54:15.312726Z"), 1);
        module.storeSensor(ryWs301("2026-03-09T07:54:15.312726Z"), 2);
        module.storeSensor(ryGw("1"), 3);
        module.catalogArrives(serverText(twoModels(ryWs301("2026-03-09T07:54:15.312726Z"), ryGw("1"))));
    }

    Writes writes(flash);
    Module module(flash);
    module.readingsOf(3);
    TEST_ASSERT_TRUE(module.republish.execute("boot"));
    module.server.sendToServerModuleStructureCallCount = 0;

    Model withPressure = ryWs301("2026-08-01T09:00:00.000000Z");
    withPressure.channels.push_back({3, 2, "0.1", "0", "pressure", "pressure", "1100"});
    withPressure.subTypes.push_back({"pressure", "300", "1100", "0.1"});
    module.catalogArrives(serverText(twoModels(withPressure, ryGw("1"))));

    printf("  t107: an edit of one model with two sensors - catalog written %u time(s), registry %u "
           "time(s), structure sent %d time(s)\n",
           writes.catalog(), writes.registry(), module.server.sendToServerModuleStructureCallCount);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, writes.catalog(), "the catalog was not written once");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2, writes.registry(), "the registry was not written once per edited sensor");
    TEST_ASSERT_EQUAL_UINT(3, module.sensorOn(1).modbusRegisterConfigs.size());
    TEST_ASSERT_EQUAL_UINT(3, module.sensorOn(2).modbusRegisterConfigs.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.server.sendToServerModuleStructureCallCount,
                                  "the structure did not go out once");
    TEST_ASSERT_EQUAL_STRING("t107-module.01.01:air_temp t107-module.01.02:humidity "
                             "t107-module.01.03:pressure t107-module.01.04:vpd",
                             module.structureOfPort(1).c_str());
    TEST_ASSERT_TRUE_MESSAGE(module.values.hasChannelValues(uidOf(3)),
                             "the readings of a sensor of a model the catalog left as it was were dropped");
    TEST_ASSERT_EQUAL_FLOAT(1.0f, module.sensorOn(3).modbusRegisterConfigs[0].multiplier);
}

// An edit that gives the sensors nothing new - the RY-WS301's display name, edited through the
// platform: the catalog is written, the sensors of the model are not, and the structure does not go
// out.
static void test_an_edit_that_gives_the_sensors_nothing_new_writes_only_the_catalog(void)
{
    Flash flash;
    {
        Module module(flash);
        module.storeSensor(ryWs301("2026-03-09T07:54:15.312726Z"), 1);
        module.storeSensor(ryWs301("2026-03-09T07:54:15.312726Z"), 2);
        module.catalogArrives(serverText(std::vector<Model>(1, ryWs301("2026-03-09T07:54:15.312726Z"))));
    }

    Model renamed = ryWs301("2026-08-01T09:00:00.000000Z");
    renamed.displayName = "RY-WS301 Air Sensor";
    Writes writes(flash);
    Module module(flash);
    module.readingsOf(1);
    TEST_ASSERT_TRUE(module.republish.execute("boot"));
    module.server.sendToServerModuleStructureCallCount = 0;
    module.catalogArrives(serverText(std::vector<Model>(1, renamed)));

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, writes.catalog(), "the premise: the edited model was stored");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.registry(), "a sensor was written with what it already had");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.server.sendToServerModuleStructureCallCount,
                                  "the structure went out for channels that did not change");
    TEST_ASSERT_TRUE(module.values.hasChannelValues(uidOf(1)));
}

// --------------------------------------------------------------------------------------------
// 4. A sensor the flash refused to store.
// --------------------------------------------------------------------------------------------

// The ry-gw's lux multiplied by ten, edited through the platform (the timestamp moves), and the flash
// refuses to store the sensor with the new multiplier. The model is stored; the sensor keeps the
// multiplier it had, and what it read by it. The module restarts, and at its synchronization - the
// same answer, the model held as it is - the flash takes the write: the sensor is stored with the new
// multiplier, and what the old one read is dropped.
static void test_a_sensor_the_flash_refused_to_store_is_stored_at_the_next_synchronization(void)
{
    Model edited = ryGw("10");
    edited.updatedAt = "2026-08-01T09:00:00.000000Z";
    Flash flash;
    {
        Module module(flash);
        module.storeSensor(ryGw("1"), 1);
        module.catalogArrives(serverText(std::vector<Model>(1, ryGw("1"))));
    }

    {
        Module module(flash);
        module.readingsOf(1);
        flash.refusedFile = REGISTRY_FILE;
        Writes refused(flash);
        module.catalogArrives(serverText(std::vector<Model>(1, edited)));
        TEST_ASSERT_EQUAL_UINT_MESSAGE(1, refused.catalog(), "the premise: the edited model was stored");
        TEST_ASSERT_EQUAL_UINT_MESSAGE(1, refused.registry(), "the premise: the sensor was offered to the flash");
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1.0f, module.sensorOn(1).modbusRegisterConfigs[0].multiplier,
                                        "the premise: the registry kept the sensor with the multiplier it had");
        TEST_ASSERT_TRUE_MESSAGE(module.values.hasChannelValues(uidOf(1)),
                                 "the readings of a sensor the flash kept as it was were dropped");
        flash.refusedFile.clear();
    }

    Writes writes(flash);
    Module module(flash);
    module.readingsOf(1);
    module.catalogArrives(serverText(std::vector<Model>(1, edited)));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.catalog(), "the premise: the module holds this model already");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(10.0f, module.sensorOn(1).modbusRegisterConfigs[0].multiplier,
                                    "the sensor the flash refused stays with the multiplier its model no longer has");
    TEST_ASSERT_EQUAL_UINT(1, writes.registry());
    TEST_ASSERT_FALSE_MESSAGE(module.values.hasChannelValues(uidOf(1)), "what the old multiplier read stayed");
}

// The flash refuses to store the RY-WS12S's sensor with the channel type the catalog now gives it.
// The registry keeps the sensor as it was, so the structure - rebuilt from the registry - would say
// what it already says: it does not go out.
static void test_a_sensor_the_flash_refused_to_store_sends_no_structure(void)
{
    Flash flash;
    {
        Module module(flash);
        module.storeSensor(ryWs12sBefore(), 2);
        module.catalogArrives(serverText(std::vector<Model>(1, ryWs12sBefore())));
    }

    Module module(flash);
    TEST_ASSERT_TRUE(module.republish.execute("boot"));
    module.server.sendToServerModuleStructureCallCount = 0;

    Model retyped = ryWs12sAfter();
    retyped.updatedAt = "2026-08-01T09:00:00.000000Z";
    flash.refusedFile = REGISTRY_FILE;
    module.catalogArrives(serverText(std::vector<Model>(1, retyped)));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("water_level", module.sensorOn(2).modbusRegisterConfigs[0].sensorType.c_str(),
                                     "the premise: the registry kept the sensor as it was");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.server.sendToServerModuleStructureCallCount,
                                  "the structure went out for a sensor the registry did not change");
    TEST_ASSERT_EQUAL_STRING("t107-module.02.00:water_level", module.structureOfPort(2).c_str());
}

// A synchronization that finds nothing to store while a write of the catalog is still on its way
// leaves the sensors to that write. The module holds the ry-gw at x10 and its sensor still at x1 -
// the flash refused it. A fetch brings x100, which goes to the storage task; before it lands, another
// fetch brings x10, the catalog the module holds. The sensor is not written for it: it follows the
// catalog the write lands, once.
static void test_the_sensors_are_left_to_a_catalog_write_still_on_its_way(void)
{
    Model atTen = ryGw("10");
    atTen.updatedAt = "2026-08-01T09:00:00.000000Z";
    Model atHundred = ryGw("100");
    atHundred.updatedAt = "2026-08-02T09:00:00.000000Z";
    Flash flash;
    {
        Module module(flash);
        module.storeSensor(ryGw("1"), 1);
        module.catalogArrives(serverText(std::vector<Model>(1, ryGw("1"))));
    }
    {
        Module module(flash);
        flash.refusedFile = REGISTRY_FILE;
        module.catalogArrives(serverText(std::vector<Model>(1, atTen)));
        flash.refusedFile.clear();
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1.0f, module.sensorOn(1).modbusRegisterConfigs[0].multiplier,
                                        "the premise: the sensor is behind its model");
    }

    Writes writes(flash);
    Module module(flash);
    module.fetchAnswered(serverText(std::vector<Model>(1, atHundred)));
    TEST_ASSERT_TRUE_MESSAGE(module.sync.isWritePending(), "the premise: x100 is on its way to flash");
    module.fetchAnswered(serverText(std::vector<Model>(1, atTen)));
    TEST_ASSERT_TRUE(module.sync.isWritePending());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes.registry(),
                                   "the sensor was written for a catalog a write still on its way replaces");

    module.theWriteLands();
    TEST_ASSERT_FALSE(module.sync.isWritePending());
    TEST_ASSERT_EQUAL_FLOAT(100.0f, module.sensorOn(1).modbusRegisterConfigs[0].multiplier);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, writes.registry(), "the sensor was not written once, for the catalog that landed");
}

extern "C" void run_catalog_changes_reach_the_module_tests(void)
{
    RUN_TEST(test_models_the_server_sends_without_a_timestamp_are_not_written_again_at_every_boot);
    RUN_TEST(test_a_fresh_module_stores_the_catalog_once);
    RUN_TEST(test_the_numbers_a_restart_reads_back_are_the_ones_the_server_sent);
    RUN_TEST(test_the_same_sub_types_in_another_order_are_the_same_model);
    RUN_TEST(test_an_edit_of_the_registers_alone_reaches_the_module);
    RUN_TEST(test_an_edit_of_the_baud_rate_alone_reaches_the_module);
    RUN_TEST(test_the_edits_a_migration_made_with_the_timestamp_where_it_was_reach_the_module);
    RUN_TEST(test_an_edit_writes_the_catalog_once_its_sensors_once_and_the_structure_once);
    RUN_TEST(test_an_edit_that_gives_the_sensors_nothing_new_writes_only_the_catalog);
    RUN_TEST(test_a_sensor_the_flash_refused_to_store_is_stored_at_the_next_synchronization);
    RUN_TEST(test_a_sensor_the_flash_refused_to_store_sends_no_structure);
    RUN_TEST(test_the_sensors_are_left_to_a_catalog_write_still_on_its_way);
}
