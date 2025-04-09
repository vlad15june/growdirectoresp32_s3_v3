#ifndef EEPROM_ADAPTER_H
#define EEPROM_ADAPTER_H

#include <Arduino.h>
#include <EEPROM.h>

// Interface for EEPROM operations
class IEepromAdapter {
public:
    // Pure virtual destructor to make the class abstract
    virtual ~IEepromAdapter() = 0;
    
    // Initialize the EEPROM
    virtual bool begin() = 0;
    
    // Basic read/write operations
    virtual uint8_t readByte(int address) = 0;
    virtual bool writeByte(int address, uint8_t value) = 0;
    
    // Read/write for different data types
    virtual bool writeInt(int address, int value) = 0;
    virtual int readInt(int address) = 0;
    
    virtual bool writeFloat(int address, float value) = 0;
    virtual float readFloat(int address) = 0;
    
    // String operations
    virtual bool writeString(int address, const char* string) = 0;
    virtual bool readString(int address, char* buffer, int bufferSize) = 0;
    
    // Byte array operations
    virtual bool writeBytes(int address, const byte* data, int length) = 0;
    virtual bool readBytes(int address, byte* buffer, int length) = 0;
    
    // Commit changes to EEPROM
    virtual bool commit() = 0;
};

// Default implementation
inline IEepromAdapter::~IEepromAdapter() {}

class EepromAdapter : public IEepromAdapter {
private:
    int _eepromSize;
    int _minAddr;
    int _maxAddr;
    
    // Check if address is valid
    bool isAddressValid(int address) const {
        return (address >= _minAddr && address <= _maxAddr);
    }
    
    // Check if address range is valid
    bool isAddressRangeValid(int startAddr, int length) const {
        return (isAddressValid(startAddr) && isAddressValid(startAddr + length - 1));
    }

public:
    // Constructor with default values
    EepromAdapter(int eepromSize = 1024, int minAddr = 0) 
        : _eepromSize(eepromSize),
          _minAddr(minAddr),
          _maxAddr(minAddr + eepromSize - 1) {
    }
    
    // Initialize EEPROM
    bool begin() override {
        return EEPROM.begin(_eepromSize);
    }
    
    // Basic read/write operations
    uint8_t readByte(int address) override {
        if (!isAddressValid(address)) {
            return 0;
        }
        return EEPROM.read(address);
    }
    
    bool writeByte(int address, uint8_t value) override {
        if (!isAddressValid(address)) {
            return false;
        }
        EEPROM.write(address, value);
        return true;
    }
    
    // Integer operations (4 bytes)
    bool writeInt(int address, int value) override {
        if (!isAddressRangeValid(address, sizeof(int))) {
            return false;
        }
        
        byte* byteArray = (byte*)&value;
        for (unsigned int i = 0; i < sizeof(int); i++) {
            EEPROM.write(address + i, byteArray[i]);
        }
        
        return true;
    }
    
    int readInt(int address) override {
        if (!isAddressRangeValid(address, sizeof(int))) {
            return 0;
        }
        
        int value = 0;
        byte* byteArray = (byte*)&value;
        for (unsigned int i = 0; i < sizeof(int); i++) {
            byteArray[i] = EEPROM.read(address + i);
        }
        
        return value;
    }
    
    // Float operations (4 bytes)
    bool writeFloat(int address, float value) override {
        if (!isAddressRangeValid(address, sizeof(float))) {
            return false;
        }
        
        byte* byteArray = (byte*)&value;
        for (unsigned int i = 0; i < sizeof(float); i++) {
            EEPROM.write(address + i, byteArray[i]);
        }
        
        return true;
    }
    
    float readFloat(int address) override {
        if (!isAddressRangeValid(address, sizeof(float))) {
            return 0.0f;
        }
        
        float value = 0.0f;
        byte* byteArray = (byte*)&value;
        for (unsigned int i = 0; i < sizeof(float); i++) {
            byteArray[i] = EEPROM.read(address + i);
        }
        
        return value;
    }
    
    // String operations
    bool writeString(int address, const char* string) override {
        int numBytes = strlen(string) + 1; // Include null terminator
        if (!isAddressRangeValid(address, numBytes)) {
            return false;
        }
        
        for (int i = 0; i < numBytes; i++) {
            EEPROM.write(address + i, string[i]);
        }
        
        return true;
    }
    
    bool readString(int address, char* buffer, int bufferSize) override {
        if (!isAddressValid(address) || bufferSize <= 0) {
            return false;
        }
        
        // If buffer can only hold terminator
        if (bufferSize == 1) {
            buffer[0] = 0;
            return true;
        }
        
        int bytesRead = 0;
        byte ch = EEPROM.read(address + bytesRead);
        buffer[bytesRead] = ch;
        bytesRead++;
        
        // Continue reading until null terminator, buffer full, or address limit reached
        while (ch != 0 && bytesRead < bufferSize && isAddressValid(address + bytesRead)) {
            ch = EEPROM.read(address + bytesRead);
            buffer[bytesRead] = ch;
            bytesRead++;
        }
        
        // Ensure string termination
        if (ch != 0 && bytesRead > 0) {
            buffer[bytesRead - 1] = 0;
        }
        
        return true;
    }
    
    // Byte array operations
    bool writeBytes(int address, const byte* data, int length) override {
        if (!isAddressRangeValid(address, length)) {
            return false;
        }
        
        for (int i = 0; i < length; i++) {
            EEPROM.write(address + i, data[i]);
        }
        
        return true;
    }
    
    bool readBytes(int address, byte* buffer, int length) override {
        if (!isAddressRangeValid(address, length)) {
            return false;
        }
        
        for (int i = 0; i < length; i++) {
            buffer[i] = EEPROM.read(address + i);
        }
        
        return true;
    }
    
    // Commit changes to EEPROM
    bool commit() override {
        return EEPROM.commit();
    }
    
    // Set minimum and maximum addresses
    void setAddressRange(int minAddr, int maxAddr) {
        if (minAddr >= 0 && maxAddr > minAddr) {
            _minAddr = minAddr;
            _maxAddr = maxAddr;
            _eepromSize = maxAddr - minAddr + 1;
        }
    }
    
    // Get EEPROM size
    int getSize() const {
        return _eepromSize;
    }
    
    // Get minimum address
    int getMinAddress() const {
        return _minAddr;
    }
    
    // Get maximum address
    int getMaxAddress() const {
        return _maxAddr;
    }
};

#endif // EEPROM_ADAPTER_H