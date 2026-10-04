#include <HardwareSerial.h>
#include "IModbusRS485.h"
#include "../../../Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#ifndef MODBUS_RS485_H
#define MODBUS_RS485_H
class ModbusRS485 : public IModbusRS485
{
private:
    HardwareSerial &_port; // HardwareSerial object for RS-485 port
    int _controlPin;       // Control pin for receive/transmit switching
    const char *_portName; // Port name for debugging
    IArduinoAdapter &_arduino;
    uint32_t _config;      // Serial configuration
    int _rxPin;           // RX pin
    int _txPin;           // TX pin
    int _sensorPowerPin;  // Sensor power pin
    bool _isPoweredOn;    // Track power state
    PortPower _power;     // When the power pin last moved, see powerRecord()
    uint32_t _droppedFrames = 0;  // Frames refused because the UART was never begun
    bool _dropReported = false;   // The first refusal on this port has been logged

public:
    /** Name of the counters-line column that sums droppedFrameCount() over the ports. */
    static constexpr const char *DROP_COLUMN = "rs485_drop_uninit";

    ModbusRS485(HardwareSerial &port, int controlPin, const char *portName, IArduinoAdapter &arduino, uint32_t config, int rxPin, int txPin, int sensorPowerPin)
        : _port(port), _controlPin(controlPin), _portName(portName), _arduino(arduino), _config(config), _rxPin(rxPin), _txPin(txPin), _sensorPowerPin(sensorPowerPin), _isPoweredOn(false)
    {
        _arduino.pinMode(_controlPin, _arduino.getPinModeOutput());
        _arduino.digitalWrite(_controlPin, _arduino.getDigitalLow());
        _arduino.pinMode(_sensorPowerPin, _arduino.getPinModeOutput());
        _arduino.digitalWrite(_sensorPowerPin, _arduino.getDigitalLow());
    }

    // Initialize RS-485 port
    void begin(unsigned long baud) override
    {
        _port.begin(baud, _config, _rxPin, _txPin);
        _port.setPins(_rxPin, _txPin, -1, _controlPin);
        _port.setMode(UART_MODE_RS485_HALF_DUPLEX);
        Serial.printf("[%s] Initialized at %lu baud (RS485 half-duplex Auto-RTS on pin %d)\n", _portName, baud, _controlPin);
    }

    uint16_t calculateCRC(uint8_t *data, uint8_t length) override
    {
        uint16_t crc = 0xFFFF;
        for (uint8_t i = 0; i < length; i++)
        {
            crc ^= data[i];
            for (uint8_t j = 0; j < 8; j++)
            {
                if (crc & 0x0001)
                {
                    crc >>= 1;
                    crc ^= 0xA001;
                }
                else
                {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }

    void sendData(uint8_t *data, uint8_t length) override
    {
        // The 100 ms is a sensor's power-up settling time, so it is owed to the *transition*
        // from unpowered to powered - not to every request. Sleeping on each send would mean, on
        // a port that has been powered since boot, waiting for an event that already happened,
        // once per channel per attempt: 100 ms of every ~145 ms a healthy channel costs, and
        // 300 ms of every silent sensor's 1400 ms.
        //
        // `_isPoweredOn` is maintained by powerOnPort() and
        // powerOffPort(), and the wait is skipped when it is set. State is per instance and each
        // instance owns one SENSOR_POWER_n pin (globals.h, `modbusRS485_pin1` to `modbusRS485_pin4`), so this is per port, not
        // global - two ports sharing a UART still have their own power pin and their own flag.
        if (!_isPoweredOn)
        {
            _arduino.digitalWrite(_sensorPowerPin, _arduino.getDigitalHigh());
            _arduino.delay(100);
            _isPoweredOn = true;
            _power.wentOn(static_cast<uint32_t>(_arduino.millis()));
        }

        // A UART nobody has begun has no driver behind it: HardwareSerial::write() still returns 1
        // and flush() returns at once, but not a bit reaches the TX pin and nothing is ever
        // received (uartWrite(), uartFlushTxOnly() and uartAvailable() return early on a NULL
        // uart). So the frame is refused here instead of being written into that silence: the
        // first refusal on the port is logged, every one is counted. The UARTs are begun once at
        // boot, before any frame (ModuleSetupService::setupSensorBus()), so on a sensor module
        // this stays at zero.
        if (!_port)
        {
            _droppedFrames++;
            if (!_dropReported)
            {
                _dropReported = true;
                Serial.printf("[%s] RS485: frame dropped - port not initialized (DE pin %d); "
                              "further drops on this port are only counted (%s)\n",
                              _portName, _controlPin, DROP_COLUMN);
            }
            return;
        }

        Serial.printf("[%s] Sending request: ", _portName);
        for (int i = 0; i < length; i++)
        {
            Serial.printf("0x%02X ", data[i]);
            _port.write(data[i]);
        }
        _port.flush();
        Serial.println(" | Request sent.");
    }

    int readAvailable(uint8_t *buffer, size_t maxLength) override
    {
        int bytesRead = 0;

        while (_port.available() > 0 && bytesRead < maxLength)
        {
            buffer[bytesRead++] = _port.read();
        }

        return bytesRead;
    }

    int available() override
    {
        return _port.available();
    }

    const char *getPortName() const override
    {
        return _portName;
    }

    /** @brief Frames sendData() refused because this port's UART had not been begun. */
    uint32_t droppedFrameCount() const
    {
        return _droppedFrames;
    }

    bool writeRegister(int slaveAddress, int registerAddress, uint16_t value) override
    {
        // Modbus function code 0x10 (Preset Multiple Registers) for writing single register
        uint8_t request[11] = {
            (uint8_t)slaveAddress,
            0x10,  // Function code: Preset Multiple Registers
            (uint8_t)(registerAddress >> 8),    // Register address high byte
            (uint8_t)(registerAddress & 0xFF),  // Register address low byte
            0x00, 0x01,  // Number of registers (1)
            0x02,        // Byte count (2 bytes for 1 register)
            (uint8_t)(value >> 8),      // Value high byte
            (uint8_t)(value & 0xFF)     // Value low byte
        };

        uint16_t crc = calculateCRC(request, 9);
        request[9] = crc & 0xFF;
        request[10] = (crc >> 8);

        sendData(request, sizeof(request));

        _arduino.delay(300);

        uint8_t response[8];
        int bytesRead = readAvailable(response, sizeof(response));
        
        Serial.printf("[%s] Write register response: %d bytes received\n", _portName, bytesRead);
        
        if (bytesRead >= 6 && response[0] == slaveAddress && response[1] == 0x10)
        {
            Serial.printf("[%s] Successfully wrote register 0x%04X = 0x%04X to device %d\n", 
                         _portName, registerAddress, value, slaveAddress);
            return true;
        }
        else if (bytesRead > 0 && response[1] >= 0x80)
        {
            Serial.printf("[%s] Exception response (0x%02X) when writing register to device %d\n", 
                         _portName, response[1], slaveAddress);
            return false;
        }

        Serial.printf("[%s] Failed to write register to device %d\n", _portName, slaveAddress);
        return false;
    }

    bool writeSingleRegister(int slaveAddress, int registerAddress, uint16_t value) override
    {
        // Modbus function code 0x06 (Write Single Register)
        uint8_t request[8] = {
            (uint8_t)slaveAddress,
            0x06,  // Function code: Write Single Register
            (uint8_t)(registerAddress >> 8),   // Register address high byte
            (uint8_t)(registerAddress & 0xFF), // Register address low byte
            (uint8_t)(value >> 8),             // Value high byte
            (uint8_t)(value & 0xFF)            // Value low byte
        };

        uint16_t crc = calculateCRC(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8);

        sendData(request, sizeof(request));

        _arduino.delay(300);

        uint8_t response[8];
        int bytesRead = readAvailable(response, sizeof(response));

        Serial.printf("[%s] WriteSingleRegister response: %d bytes received\n", _portName, bytesRead);

        if (bytesRead >= 6 && response[0] == slaveAddress && response[1] == 0x06)
        {
            Serial.printf("[%s] Successfully wrote register 0x%04X = 0x%04X to device %d (FC06)\n",
                         _portName, registerAddress, value, slaveAddress);
            return true;
        }
        else if (bytesRead > 0 && response[1] >= 0x80)
        {
            Serial.printf("[%s] Exception response (0x%02X) when writing register to device %d (FC06)\n",
                         _portName, response[1], slaveAddress);
            return false;
        }

        Serial.printf("[%s] Failed to write register to device %d (FC06)\n", _portName, slaveAddress);
        return false;
    }

    bool powerOnPort() override
    {
        if (_isPoweredOn)
        {
            return false;
        }
        _arduino.digitalWrite(_sensorPowerPin, _arduino.getDigitalHigh());
        _arduino.delay(100);
        _isPoweredOn = true;
        _power.wentOn(static_cast<uint32_t>(_arduino.millis()));
        Serial.printf("[%s] Port powered ON\n", _portName);
        return true;
    }

    bool powerOffPort() override
    {
        if (!_isPoweredOn)
        {
            return false;
        }
        _arduino.digitalWrite(_sensorPowerPin, _arduino.getDigitalLow());
        _power.wentOff(static_cast<uint32_t>(_arduino.millis()));
        _arduino.delay(100);
        _isPoweredOn = false;
        Serial.printf("[%s] Port powered OFF\n", _portName);
        return true;
    }

    /**
     * The moments are the ones the sensor sees: the power is there to use once the 100 ms of
     * settling are over, and it is gone the moment the pin goes low. The constructor drives the pin
     * low without a transition, so a port nothing has switched on yet is dark with no record of it.
     */
    PortPower powerRecord() const override
    {
        return _power;
    }

    /** The HardwareSerial this port was built on: ports built on one share a wire (globals.h). */
    const void *wire() const override
    {
        return &_port;
    }
};
#endif