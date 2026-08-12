#include <unity.h>
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"

// GDV1-3782: VPD as a computed channel of a ModBus sensor.

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

// INV-2: Magnus formula, kPa, no rounding
static void test_vpd_compute_reference_values(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.267f, VpdChannelUseCase::compute(25.0f, 60.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.468f, VpdChannelUseCase::compute(20.0f, 80.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.546f, VpdChannelUseCase::compute(30.0f, 40.0f));
}

// INV-2, D-11: 100 % humidity publishes a plain zero, the firmware does not adjust it
static void test_vpd_compute_saturated_air_is_zero(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, VpdChannelUseCase::compute(25.0f, 100.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, VpdChannelUseCase::compute(10.0f, 100.0f));
}

// INV-1, INV-3: the pair exists only when BOTH channels are present on the same sensor
static void test_vpd_find_air_pair_requires_both_channels(void)
{
    std::vector<ModbusRegisterConfig> pair;
    pair.push_back(makeConfig(1, "air_temp"));
    pair.push_back(makeConfig(2, "humidity"));

    uint8_t tempCh = 0;
    uint8_t humCh = 0;
    TEST_ASSERT_TRUE(VpdChannelUseCase::findAirPair(pair, tempCh, humCh));
    TEST_ASSERT_EQUAL_UINT8(1, tempCh);
    TEST_ASSERT_EQUAL_UINT8(2, humCh);

    std::vector<ModbusRegisterConfig> tempOnly;
    tempOnly.push_back(makeConfig(1, "air_temp"));
    TEST_ASSERT_FALSE(VpdChannelUseCase::findAirPair(tempOnly, tempCh, humCh));

    std::vector<ModbusRegisterConfig> humOnly;
    humOnly.push_back(makeConfig(1, "humidity"));
    TEST_ASSERT_FALSE(VpdChannelUseCase::findAirPair(humOnly, tempCh, humCh));

    std::vector<ModbusRegisterConfig> soil;
    soil.push_back(makeConfig(1, "soil_temp"));
    soil.push_back(makeConfig(2, "soil_moisture"));
    TEST_ASSERT_FALSE(VpdChannelUseCase::findAirPair(soil, tempCh, humCh));
    TEST_ASSERT_FALSE(VpdChannelUseCase::hasVpdChannel(soil));
}

// INV-5: max(channel) + 1, not the count - gaps in numbering are not filled
static void test_vpd_channel_number_is_max_plus_one(void)
{
    std::vector<ModbusRegisterConfig> plain;
    plain.push_back(makeConfig(1, "air_temp"));
    plain.push_back(makeConfig(2, "humidity"));

    uint8_t channel = 0;
    TEST_ASSERT_TRUE(VpdChannelUseCase::vpdChannelNumber(plain, channel));
    TEST_ASSERT_EQUAL_UINT8(3, channel);

    std::vector<ModbusRegisterConfig> withGap;
    withGap.push_back(makeConfig(1, "air_temp"));
    withGap.push_back(makeConfig(2, "humidity"));
    withGap.push_back(makeConfig(5, "co2"));

    TEST_ASSERT_TRUE(VpdChannelUseCase::vpdChannelNumber(withGap, channel));
    TEST_ASSERT_EQUAL_UINT8(6, channel);

    // Order of configs must not matter
    std::vector<ModbusRegisterConfig> shuffled;
    shuffled.push_back(makeConfig(5, "co2"));
    shuffled.push_back(makeConfig(2, "humidity"));
    shuffled.push_back(makeConfig(1, "air_temp"));

    TEST_ASSERT_TRUE(VpdChannelUseCase::vpdChannelNumber(shuffled, channel));
    TEST_ASSERT_EQUAL_UINT8(6, channel);
}

// INV-5: the uid suffix is two digits, so 99 is the last usable channel
static void test_vpd_channel_number_overflow(void)
{
    std::vector<ModbusRegisterConfig> configs;
    configs.push_back(makeConfig(1, "air_temp"));
    configs.push_back(makeConfig(99, "humidity"));

    uint8_t channel = 0;
    TEST_ASSERT_FALSE(VpdChannelUseCase::vpdChannelNumber(configs, channel));
    TEST_ASSERT_FALSE(VpdChannelUseCase::hasVpdChannel(configs));

    // 98 still fits and yields 99
    std::vector<ModbusRegisterConfig> fits;
    fits.push_back(makeConfig(1, "air_temp"));
    fits.push_back(makeConfig(98, "humidity"));
    TEST_ASSERT_TRUE(VpdChannelUseCase::vpdChannelNumber(fits, channel));
    TEST_ASSERT_EQUAL_UINT8(99, channel);
}

// INV-4: same format as register channels, two-digit suffix
static void test_vpd_channel_uid_format(void)
{
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:2C.00.03",
                             VpdChannelUseCase::channelUid("84:F7:03:FE:88:2C.00", 3).c_str());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:2C.00.10",
                             VpdChannelUseCase::channelUid("84:F7:03:FE:88:2C.00", 10).c_str());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:2C.00.99",
                             VpdChannelUseCase::channelUid("84:F7:03:FE:88:2C.00", 99).c_str());
}

// INV-4: the number depends on the config vector only - a reboot or firmware update does not move it
static void test_vpd_channel_number_is_deterministic(void)
{
    std::vector<ModbusRegisterConfig> configs;
    configs.push_back(makeConfig(1, "air_temp"));
    configs.push_back(makeConfig(2, "humidity"));

    uint8_t first = 0;
    uint8_t second = 0;
    TEST_ASSERT_TRUE(VpdChannelUseCase::vpdChannelNumber(configs, first));
    TEST_ASSERT_TRUE(VpdChannelUseCase::vpdChannelNumber(configs, second));
    TEST_ASSERT_EQUAL_UINT8(first, second);
}

// INV-3, D-12: a separate thermometer plus a separate hygrometer is NOT a pair
static void test_vpd_two_single_channel_sensors_are_not_a_pair(void)
{
    SensorEntity thermometer;
    thermometer.uid = "AA:BB:CC:DD:EE:FF.00";
    thermometer.modbusRegisterConfigs.push_back(makeConfig(1, "air_temp"));

    SensorEntity hygrometer;
    hygrometer.uid = "AA:BB:CC:DD:EE:FF.01";
    hygrometer.modbusRegisterConfigs.push_back(makeConfig(1, "humidity"));

    TEST_ASSERT_FALSE(VpdChannelUseCase::hasVpdChannel(thermometer.modbusRegisterConfigs));
    TEST_ASSERT_FALSE(VpdChannelUseCase::hasVpdChannel(hygrometer.modbusRegisterConfigs));
}

// INV-3: two air sensors each get their own VPD channel, computed from their own pair
static void test_vpd_two_air_sensors_are_independent(void)
{
    SensorEntity first;
    first.uid = "AA:BB:CC:DD:EE:FF.00";
    first.modbusRegisterConfigs.push_back(makeConfig(1, "air_temp"));
    first.modbusRegisterConfigs.push_back(makeConfig(2, "humidity"));

    SensorEntity second;
    second.uid = "AA:BB:CC:DD:EE:FF.01";
    second.modbusRegisterConfigs.push_back(makeConfig(1, "air_temp"));
    second.modbusRegisterConfigs.push_back(makeConfig(2, "humidity"));
    second.modbusRegisterConfigs.push_back(makeConfig(3, "co2"));

    uint8_t firstCh = 0;
    uint8_t secondCh = 0;
    TEST_ASSERT_TRUE(VpdChannelUseCase::vpdChannelNumber(first.modbusRegisterConfigs, firstCh));
    TEST_ASSERT_TRUE(VpdChannelUseCase::vpdChannelNumber(second.modbusRegisterConfigs, secondCh));

    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF.00.03",
                             VpdChannelUseCase::channelUid(first.uid, firstCh).c_str());
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF.01.04",
                             VpdChannelUseCase::channelUid(second.uid, secondCh).c_str());
}

extern "C" void run_vpd_channel_tests(void)
{
    RUN_TEST(test_vpd_compute_reference_values);
    RUN_TEST(test_vpd_compute_saturated_air_is_zero);
    RUN_TEST(test_vpd_find_air_pair_requires_both_channels);
    RUN_TEST(test_vpd_channel_number_is_max_plus_one);
    RUN_TEST(test_vpd_channel_number_overflow);
    RUN_TEST(test_vpd_channel_uid_format);
    RUN_TEST(test_vpd_channel_number_is_deterministic);
    RUN_TEST(test_vpd_two_single_channel_sensors_are_not_a_pair);
    RUN_TEST(test_vpd_two_air_sensors_are_independent);
}
