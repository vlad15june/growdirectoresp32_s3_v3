#include <unity.h>
#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"

// GDV1-3782: first native test of SensorService - the computed VPD channel in the read loop
// and in the sensor update packet.

static MockModuleConfig g_moduleConfig;
static MockSettings g_settings;
static MockNTPTimeAdapter g_timeAdapter;
static MockSensorRepository g_sensorRepository;
static MockSensorValueModel g_sensorValueModel;
static MockSensorReader g_sensorReader;
static MockArduinoAdapter g_arduinoAdapter;
static MockHybridAPIService g_hybridApiService;
static MockLogger g_logger;
static MockDebugSystem g_debugSystem;
static MockMqttManager g_mqttManager;
static ModuleStructure g_moduleStructure;
// The computed channels arrive as a dependency, exactly as in globals.h (PR #17 review)
static VpdChannelUseCase g_vpdChannelUseCase;
static VirtualSensorChannelsUseCase g_virtualChannels(&g_vpdChannelUseCase);
static SensorService *g_sensorService;

static const char *SENSOR_UID = "84:F7:03:FE:88:2C.00";
static const char *TEMP_UID = "84:F7:03:FE:88:2C.00.01";
static const char *HUM_UID = "84:F7:03:FE:88:2C.00.02";
static const char *VPD_UID = "84:F7:03:FE:88:2C.00.03";

static ModbusRegisterConfig makeConfig(uint8_t channel, const std::string &sensorType)
{
    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = 0;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.offset = 0.0f;
    config.channelName = sensorType;
    config.channel = channel;
    config.sensorType = sensorType;
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    return config;
}

static SensorEntity makeAirSensor(bool isError)
{
    SensorEntity sensor;
    sensor.uid = SENSOR_UID;
    sensor.model = "ry_ws301";
    sensor.protocol = "ModBus";
    sensor.pin = 0;
    sensor.isActive = true;
    sensor.isError = isError;
    sensor.modbusDeviceAddress = 1;
    sensor.modbusRegisterConfigs.push_back(makeConfig(1, "air_temp"));
    sensor.modbusRegisterConfigs.push_back(makeConfig(2, "humidity"));
    return sensor;
}

static void sensor_service_setup(void)
{
    g_settings.reset();
    g_moduleConfig.reset();
    g_moduleConfig.setSettings(&g_settings);
    g_sensorRepository.sensors.clear();
    g_sensorRepository.modbusSensors.clear();
    g_sensorValueModel.reset();
    g_sensorReader.reset();
    g_arduinoAdapter.reset();
    g_arduinoAdapter.setMillis(0);
    g_hybridApiService.reset();
    g_logger.reset();
    g_debugSystem.reset();
    g_debugSystem.featureEnabled = true;
    g_mqttManager.reset();
    g_moduleStructure.nodes.clear();

    g_sensorService = new SensorService(
        &g_moduleConfig,
        &g_moduleStructure,
        &g_sensorRepository,
        &g_sensorReader,
        &g_sensorValueModel,
        &g_timeAdapter,
        &g_arduinoAdapter,
        &g_hybridApiService,
        &g_logger,
        &g_debugSystem,
        nullptr, // udpManager - UDP path is out of scope here
        nullptr, // jsonConverter
        nullptr, // plausibilityUseCase - the gate has its own suite (GDV1-3810)
        &g_virtualChannels);
}

static void sensor_service_teardown(void)
{
    delete g_sensorService;
    g_sensorService = nullptr;
}

static const SensorDto *findSensorDto(const UpdateDto &dto, const std::string &uid)
{
    for (const auto &sensorDto : dto.sensors)
    {
        if (sensorDto.uid == uid)
        {
            return &sensorDto;
        }
    }
    return nullptr;
}

// INV-1, INV-2: both sources read -> the computed channel lands in the channel cache
static void test_read_sensor_computes_vpd_from_both_sources(void)
{
    sensor_service_setup();

    SensorEntity sensor = makeAirSensor(false);
    g_sensorReader.multiChannelValues[TEMP_UID] = 25.0f;
    g_sensorReader.multiChannelValues[HUM_UID] = 60.0f;

    TEST_ASSERT_TRUE(g_sensorService->readSensor(sensor));

    TEST_ASSERT_TRUE(g_sensorValueModel.hasChannelValues(SENSOR_UID));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.267f, g_sensorValueModel.getChannelValue(SENSOR_UID, VPD_UID));

    // INV-7: the read timestamp stays on the parent sensor, VPD shares it
    TEST_ASSERT_EQUAL_UINT32(1, g_sensorValueModel.setLastReadTimeCallCount);

    sensor_service_teardown();
}

// Owner decision 2026-08-12: a partial read leaves the previous VPD value untouched
static void test_read_sensor_partial_read_keeps_previous_vpd(void)
{
    sensor_service_setup();

    SensorEntity sensor = makeAirSensor(false);

    // First read: both sources present
    g_sensorReader.multiChannelValues[TEMP_UID] = 25.0f;
    g_sensorReader.multiChannelValues[HUM_UID] = 60.0f;
    TEST_ASSERT_TRUE(g_sensorService->readSensor(sensor));
    float firstVpd = g_sensorValueModel.getChannelValue(SENSOR_UID, VPD_UID);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.267f, firstVpd);

    // Second read: humidity register did not answer
    g_sensorReader.multiChannelValues.clear();
    g_sensorReader.multiChannelValues[TEMP_UID] = 30.0f;
    TEST_ASSERT_TRUE(g_sensorService->readSensor(sensor));

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, firstVpd, g_sensorValueModel.getChannelValue(SENSOR_UID, VPD_UID));

    sensor_service_teardown();
}

// INV-1: a sensor without a humidity channel gets no VPD at all
static void test_read_sensor_without_pair_has_no_vpd(void)
{
    sensor_service_setup();

    SensorEntity sensor;
    sensor.uid = SENSOR_UID;
    sensor.protocol = "ModBus";
    sensor.pin = 0;
    sensor.modbusRegisterConfigs.push_back(makeConfig(1, "soil_temp"));
    sensor.modbusRegisterConfigs.push_back(makeConfig(2, "soil_moisture"));

    g_sensorReader.multiChannelValues[TEMP_UID] = 25.0f;
    g_sensorReader.multiChannelValues[HUM_UID] = 60.0f;

    TEST_ASSERT_TRUE(g_sensorService->readSensor(sensor));

    auto channels = g_sensorValueModel.getChannelValues(SENSOR_UID);
    TEST_ASSERT_EQUAL(2, channels.size());
    for (const auto &pair : channels)
    {
        TEST_ASSERT_TRUE(pair.first != VPD_UID);
    }

    sensor_service_teardown();
}

// INV-8, D-10: sensor in error and never read -> VPD is reported as disconnected, not dropped
static void test_update_packet_reports_vpd_disconnected_on_error(void)
{
    sensor_service_setup();

    SensorEntity sensor = makeAirSensor(true);
    g_sensorRepository.sensors.push_back(sensor);
    g_sensorReader.readMultiChannelReturnValue = false;

    // Past both 15 s intervals so loop() reads and then sends the packet
    g_arduinoAdapter.setMillis(20000);
    g_sensorService->loop();

    TEST_ASSERT_TRUE(g_hybridApiService.sendModuleUpdateDataCalled);
    const UpdateDto &packet = g_hybridApiService.lastUpdateData;

    const SensorDto *vpdDto = findSensorDto(packet, VPD_UID);
    TEST_ASSERT_NOT_NULL(vpdDto);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, vpdDto->value);
    TEST_ASSERT_EQUAL_STRING("disconnected", vpdDto->state.c_str());

    // The register channels are still reconstructed next to it
    TEST_ASSERT_NOT_NULL(findSensorDto(packet, TEMP_UID));
    TEST_ASSERT_NOT_NULL(findSensorDto(packet, HUM_UID));

    sensor_service_teardown();
}

// INV-1: sensor in error without a pair -> no VPD record appears in the packet
static void test_update_packet_has_no_vpd_without_pair(void)
{
    sensor_service_setup();

    SensorEntity sensor;
    sensor.uid = SENSOR_UID;
    sensor.protocol = "ModBus";
    sensor.pin = 0;
    sensor.isError = true;
    sensor.modbusRegisterConfigs.push_back(makeConfig(1, "soil_temp"));
    sensor.modbusRegisterConfigs.push_back(makeConfig(2, "soil_moisture"));
    g_sensorRepository.sensors.push_back(sensor);
    g_sensorReader.readMultiChannelReturnValue = false;

    g_arduinoAdapter.setMillis(20000);
    g_sensorService->loop();

    TEST_ASSERT_TRUE(g_hybridApiService.sendModuleUpdateDataCalled);
    TEST_ASSERT_NULL(findSensorDto(g_hybridApiService.lastUpdateData, VPD_UID));

    sensor_service_teardown();
}

// Happy path of the packet needs no extra code: the cached VPD is listed like any other channel
static void test_update_packet_carries_cached_vpd(void)
{
    sensor_service_setup();

    SensorEntity sensor = makeAirSensor(false);
    g_sensorRepository.sensors.push_back(sensor);
    g_sensorRepository.modbusSensors.push_back(sensor);
    g_sensorReader.multiChannelValues[TEMP_UID] = 25.0f;
    g_sensorReader.multiChannelValues[HUM_UID] = 60.0f;

    g_arduinoAdapter.setMillis(20000);
    g_sensorService->loop();

    TEST_ASSERT_TRUE(g_hybridApiService.sendModuleUpdateDataCalled);
    const SensorDto *vpdDto = findSensorDto(g_hybridApiService.lastUpdateData, VPD_UID);
    TEST_ASSERT_NOT_NULL(vpdDto);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.267f, vpdDto->value);
    TEST_ASSERT_EQUAL_STRING("online", vpdDto->state.c_str());

    sensor_service_teardown();
}

extern "C" void run_sensor_service_tests(void)
{
    RUN_TEST(test_read_sensor_computes_vpd_from_both_sources);
    RUN_TEST(test_read_sensor_partial_read_keeps_previous_vpd);
    RUN_TEST(test_read_sensor_without_pair_has_no_vpd);
    RUN_TEST(test_update_packet_reports_vpd_disconnected_on_error);
    RUN_TEST(test_update_packet_has_no_vpd_without_pair);
    RUN_TEST(test_update_packet_carries_cached_vpd);
}
