#ifndef PENDING_FIRMWARE_UPDATE_STORE_H
#define PENDING_FIRMWARE_UPDATE_STORE_H

#include "Core/Domain/Interfaces/IPendingFirmwareUpdateStore.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsAdapter.h"
#include <string>

/**
 * @brief The kept UpdateFirmware command as one JSON file, the way the module stores its
 *        WiFi networks and its catalogs.
 *
 * The file is written while the module runs and read on the next boot, in setupModule() after
 * initSPIFFS() and before the startup update check, so the file system is already mounted.
 */
class PendingFirmwareUpdateStore : public IPendingFirmwareUpdateStore
{
private:
    ISpiffsAdapter *_spiffsAdapter;
    IJsonConverter *_jsonConverter;

    static constexpr const char *PENDING_UPDATE_FILE = "/pendingFirmwareUpdate.json";

public:
    PendingFirmwareUpdateStore(ISpiffsAdapter *spiffsAdapter = nullptr,
                               IJsonConverter *jsonConverter = nullptr)
        : _spiffsAdapter(spiffsAdapter), _jsonConverter(jsonConverter)
    {
    }

    virtual ~PendingFirmwareUpdateStore() = default;

    bool save(const ModuleCommandDto &command) override
    {
        if (_spiffsAdapter == nullptr || _jsonConverter == nullptr)
        {
            return false;
        }

        std::string json = _jsonConverter->moduleCommandToJson(command);
        return _spiffsAdapter->rewriteToFile(PENDING_UPDATE_FILE, json.c_str());
    }

    bool load(ModuleCommandDto &command) override
    {
        if (_spiffsAdapter == nullptr || _jsonConverter == nullptr)
        {
            return false;
        }

        std::string content = _spiffsAdapter->readSPIFFSstr(PENDING_UPDATE_FILE);
        if (content.empty())
        {
            return false;
        }

        return _jsonConverter->moduleCommandFromJson(content, command);
    }

    void clear() override
    {
        if (_spiffsAdapter != nullptr)
        {
            _spiffsAdapter->removeFile(PENDING_UPDATE_FILE);
        }
    }
};

#endif // PENDING_FIRMWARE_UPDATE_STORE_H
