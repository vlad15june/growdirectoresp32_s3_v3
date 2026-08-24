#ifndef I_MODBUS_ADDRESS_ALLOCATOR_H
#define I_MODBUS_ADDRESS_ALLOCATOR_H

#include <map>
#include <string>
#include <vector>

using ModbusAddressOccupancy = std::map<int, std::vector<std::string>>;

/**
 * @brief Owner of the ModBus addresses of one module: four ports are two UARTs, so an address is a
 *        resource of the module and not of the pin (GDV1-3831).
 */
class IModbusAddressAllocator
{
public:
    virtual ~IModbusAddressAllocator() = default;

    /**
     * @brief Addresses of the ModBus range already held by the entities of this module
     * @param excludeSensorUid Uid whose own row is left out, so a sensor being reconfigured does not
     *        count as its own competitor; an empty string counts every entity
     * @return Address to the uids holding it
     */
    virtual ModbusAddressOccupancy occupiedAddresses(const std::string &excludeSensorUid) const = 0;

    /**
     * @brief First address of the ModBus range nobody holds
     * @param occupied Occupancy built by occupiedAddresses()
     * @param preferred Address tried before the ascending scan; ignored when outside the range
     * @return Free address, or -1 when the whole range is held
     */
    virtual int nextFreeAddress(const ModbusAddressOccupancy &occupied, int preferred) const = 0;

    /**
     * @brief Whether another entity of the module holds this address
     * @param occupied Occupancy built by occupiedAddresses()
     * @param address Address to test
     */
    virtual bool isAddressTaken(const ModbusAddressOccupancy &occupied, int address) const = 0;

    /**
     * @brief Uid holding the address, for the log of a collision
     * @param occupied Occupancy built by occupiedAddresses()
     * @param address Address to look up
     * @return First uid holding the address, or an empty string when nobody does
     */
    virtual std::string addressHolder(const ModbusAddressOccupancy &occupied, int address) const = 0;
};

#endif // I_MODBUS_ADDRESS_ALLOCATOR_H
