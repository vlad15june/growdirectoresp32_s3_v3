#ifndef MODBUS_ADDRESS_ALLOCATOR_H
#define MODBUS_ADDRESS_ALLOCATOR_H

#include "Core/Domain/Interfaces/UseCases/IModbusAddressAllocator.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "constants.h"

/**
 * @brief Occupancy of the ModBus range of one module, read from the sensor repository
 */
class ModbusAddressAllocator : public IModbusAddressAllocator
{
private:
    ISensorRepository *sensorRepository;

public:
    /**
     * @param sensorRepository Source of the entities of the module; without it every address of the
     *        range reads as free
     */
    explicit ModbusAddressAllocator(ISensorRepository *sensorRepository = nullptr)
        : sensorRepository(sensorRepository) {}

    /**
     * @brief Addresses of the ModBus range already held by the entities of this module
     * @param excludeSensorUid Uid whose own row is left out; an empty string counts every entity
     * @return Address to the uids holding it
     */
    ModbusAddressOccupancy occupiedAddresses(const std::string &excludeSensorUid) const override
    {
        ModbusAddressOccupancy occupied;
        if (!sensorRepository)
        {
            return occupied;
        }

        for (const auto &sensorEntity : sensorRepository->getAll())
        {
            if (sensorEntity.protocol != "ModBus")
            {
                continue;
            }

            if (!excludeSensorUid.empty() && sensorEntity.uid == excludeSensorUid)
            {
                continue;
            }

            int address = sensorEntity.modbusDeviceAddress;
            if (address < MODBUS_ADDRESS_MIN || address > MODBUS_ADDRESS_MAX)
            {
                continue;
            }

            occupied[address].push_back(sensorEntity.uid);
        }

        return occupied;
    }

    /**
     * @brief First address of the ModBus range nobody holds
     * @param occupied Occupancy built by occupiedAddresses()
     * @param preferred Address tried before the ascending scan; ignored when outside the range
     * @return Free address, or -1 when the whole range is held
     */
    int nextFreeAddress(const ModbusAddressOccupancy &occupied, int preferred) const override
    {
        if (preferred >= MODBUS_ADDRESS_MIN && preferred <= MODBUS_ADDRESS_MAX && !isAddressTaken(occupied, preferred))
        {
            return preferred;
        }

        for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
        {
            if (!isAddressTaken(occupied, address))
            {
                return address;
            }
        }

        return -1;
    }

    /**
     * @brief Whether another entity of the module holds this address
     * @param occupied Occupancy built by occupiedAddresses()
     * @param address Address to test
     */
    bool isAddressTaken(const ModbusAddressOccupancy &occupied, int address) const override
    {
        auto holders = occupied.find(address);
        return holders != occupied.end() && !holders->second.empty();
    }

    /**
     * @brief Uid holding the address, for the log of a collision
     * @param occupied Occupancy built by occupiedAddresses()
     * @param address Address to look up
     * @return First uid holding the address, or an empty string when nobody does
     */
    std::string addressHolder(const ModbusAddressOccupancy &occupied, int address) const override
    {
        auto holders = occupied.find(address);
        if (holders == occupied.end() || holders->second.empty())
        {
            return "";
        }

        return holders->second.front();
    }
};

#endif // MODBUS_ADDRESS_ALLOCATOR_H
