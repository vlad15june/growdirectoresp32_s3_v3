#pragma once

// Minimal HardwareSerial stand-in for the native (host) test environment.
//
// Same purpose and same precedent as Mock/native/Arduino.h next to it: it exists so that a host
// test can include a production header that is written against the device UART - here
// Infrastructure/Adapters/Rs485/ModbusRS485.h, which is the only place the 100 ms sensor-power
// settle is spent and therefore the only place a claim about that delay can be proven.
//
// Deliberately a dumb pipe. What a device on the line does with the bytes - when it answers, how
// fast, whether it answers at all - is timing, and timing belongs to the test that owns the
// virtual clock, not to a header on the include path. The two hooks below are how the test gets
// in: `onFlush` after a request has been written, `onPoll` before the receive queue is inspected.
//
// Reachable only through the include path of env:native (see platformio.ini); on the device the
// real Arduino core header is used.
//
// A dumb pipe only once begun. Until begin() the device object has no UART driver behind it
// (arduino-esp32 2.0.17: the constructor leaves `_uart` NULL, and uartWrite(), uartFlushTxOnly(),
// uartAvailable(), uartReadBytes() and uartSetMode() in esp32-hal-uart.c return at once on a NULL
// uart): write() reports one byte written and nothing reaches the pin, flush() returns without
// waiting, nothing is ever available, and the RS-485 mode is not taken. This stand-in does the
// same, so a test cannot see a frame the board would never have sent.
//
// begin(0) is no exception to that. The core takes a zero rate for "detect it": it installs the
// driver at 9600, then waits up to timeout_ms (20 s by default) for 30 edges on RX, and when they
// do not come it sets `_uart` back to NULL (HardwareSerial::begin() and uartBaudrateDetect()
// of the core) - the object is left without a driver, even one begun before. Nothing talks
// to a master that has not asked anything, so here the detection always fails; the wait is not
// modelled, this header has no clock (SimRs485Bus, which has one, spends it).

// On the device it is HardwareSerial.h that declares the global `Serial` sink, and ModbusRS485.h
// relies on that: it includes HardwareSerial.h and nothing else, then prints to Serial. The
// stand-in has to keep that arrangement, or the header would not build here for a reason that has
// nothing to do with what is being tested.
#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

// The 8N1 frame format constant the firmware passes to begin(). Value is irrelevant here; it is
// carried through and recorded so a test can assert the port was configured at all.
#ifndef SERIAL_8N1
#define SERIAL_8N1 0x800001cu
#endif

enum
{
    UART_MODE_UART = 0,
    UART_MODE_RS485_HALF_DUPLEX = 1
};

class HardwareSerial
{
public:
    explicit HardwareSerial(int uartNumber = 0) : uartNumber(uartNumber) {}

    void begin(unsigned long baud, uint32_t config = SERIAL_8N1, int8_t rxPin = -1, int8_t txPin = -1)
    {
        this->baud = baud;
        this->config = config;
        this->rxPin = rxPin;
        this->txPin = txPin;
        beginCount++;
        driverInstalled = (baud != 0);
    }

    void setPins(int8_t rx, int8_t tx, int8_t cts, int8_t rts)
    {
        rxPin = rx;
        txPin = tx;
        ctsPin = cts;
        rtsPin = rts;
    }

    bool setMode(int mode)
    {
        if (!driverInstalled)
        {
            return false;
        }
        this->mode = mode;
        return true;
    }

    /** @brief What the device answers: is there a UART driver behind this object. */
    operator bool() const { return driverInstalled; }

    size_t write(uint8_t value)
    {
        if (driverInstalled)
        {
            tx.push_back(value);
        }
        return 1;
    }

    void flush()
    {
        if (!driverInstalled)
        {
            return;
        }
        flushCount++;
        if (onFlush)
        {
            onFlush(tx);
        }
        tx.clear();
    }

    int available()
    {
        if (!driverInstalled)
        {
            return 0;
        }
        if (onPoll)
        {
            onPoll();
        }
        return static_cast<int>(rx.size());
    }

    int read()
    {
        if (!driverInstalled)
        {
            return -1;
        }
        if (onPoll)
        {
            onPoll();
        }
        if (rx.empty())
        {
            return -1;
        }
        const int value = rx.front();
        rx.pop_front();
        return value;
    }

    // --- Host-only ---------------------------------------------------------------------------

    /** @brief Called with the bytes of a completed request, once the firmware has flushed them. */
    std::function<void(const std::vector<uint8_t>&)> onFlush;

    /** @brief Called before the receive queue is looked at, so a test can release bytes in time. */
    std::function<void()> onPoll;

    void push(uint8_t value) { rx.push_back(value); }

    int uartNumber = 0;
    unsigned long baud = 0;
    uint32_t config = 0;
    int8_t rxPin = -1;
    int8_t txPin = -1;
    int8_t ctsPin = -1;
    int8_t rtsPin = -1;
    int mode = UART_MODE_UART;
    int beginCount = 0;
    bool driverInstalled = false;
    int flushCount = 0;
    std::vector<uint8_t> tx;
    std::deque<uint8_t> rx;
};
