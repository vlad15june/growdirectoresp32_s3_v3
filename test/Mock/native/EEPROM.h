#pragma once

// Host stand-in for the ESP32 EEPROM library, reachable only through the include path of
// env:native (see platformio.ini). It exists so that host tests can build the production
// EepromAdapter and Settings, and count what those cost in flash.
//
// The device library is an emulation over a flash partition: read()/write() work on a RAM
// mirror and cost nothing, and commit() erases and reprograms the whole partition sector.
// So the number that matters for endurance is commitCalls - one commit is one erase/program
// cycle of the cell that stores the module's settings. Byte writes are counted too, but only
// to show which cells a path touched.

#include <cstddef>
#include <cstdint>
#include <cstring>

class NativeEepromDevice
{
public:
    // Large enough for the layout Settings uses (highest address in use is 560).
    static const size_t CAPACITY = 4096;

    unsigned long writeCalls = 0;
    unsigned long readCalls = 0;
    unsigned long commitCalls = 0;
    bool beginCalled = false;
    size_t configuredSize = 0;

    NativeEepromDevice() { memset(_cells, 0xFF, sizeof(_cells)); }

    bool begin(size_t size)
    {
        beginCalled = true;
        configuredSize = size;
        return size <= CAPACITY;
    }

    uint8_t read(int address)
    {
        ++readCalls;
        if (address < 0 || static_cast<size_t>(address) >= CAPACITY)
        {
            return 0;
        }
        return _cells[address];
    }

    void write(int address, uint8_t value)
    {
        ++writeCalls;
        if (address < 0 || static_cast<size_t>(address) >= CAPACITY)
        {
            return;
        }
        _cells[address] = value;
    }

    bool commit()
    {
        ++commitCalls;
        return true;
    }

    /** @brief Raw cell image, for a test that has to check a stored layout byte for byte. */
    const uint8_t* cells() const { return _cells; }

    void resetStats()
    {
        writeCalls = 0;
        readCalls = 0;
        commitCalls = 0;
    }

    /** @brief Wipe the array back to erased flash, as a new device would come up. */
    void erase()
    {
        memset(_cells, 0xFF, sizeof(_cells));
        resetStats();
    }

private:
    uint8_t _cells[CAPACITY];
};

inline NativeEepromDevice EEPROM;
