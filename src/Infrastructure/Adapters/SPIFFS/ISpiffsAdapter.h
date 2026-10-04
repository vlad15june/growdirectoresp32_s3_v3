#pragma once
#include <memory>
#include <string>
#include "ISpiffsFileWriter.h"
#include "ISpiffsFileOps.h"
#include "Core/Domain/Enums/FileSystemStatusEnum.h"

/**
 * @brief File system of the module.
 *
 * Extends ISpiffsFileOps (GDV1-3827) so that a component holding an adapter can also move files
 * around — that is what makes a safe replacement of a stored file possible.
 */
class ISpiffsAdapter : public ISpiffsFileOps {
public:
    virtual ~ISpiffsAdapter() = default;

    virtual void initSPIFFS() = 0;
    virtual std::string readSPIFFSstr(const char* filePath) = 0;
    virtual void removeFile(const char* filePath) = 0;
    virtual bool rewriteToFile(const char* filePath, const char* body) = 0;
    virtual bool appendToFile(const char* filePath, const char* body) = 0;

    /**
     * @brief Rewrite a file chunk by chunk instead of from one buffer (GDV1-3827)
     *
     * The returned writer streams into `<filePath>.tmp` and replaces `filePath` on commit(), so the
     * caller needs heap for one chunk at a time and a failed write cannot destroy the stored file.
     * Unlike appendToFile() the file is opened once and no newline is added.
     *
     * @param filePath Target file
     * @return Writer ready to take chunks, or nullptr if the file could not be opened
     */
    virtual std::unique_ptr<ISpiffsFileWriter> openForRewrite(const char* filePath) = 0;
    virtual unsigned int getFileSize(const char* filePath) = 0;

    /**
     * @brief Look a file up the way the file system indexes it, without opening it
     *
     * Unlike fileExists() and getFileSize(), which open the file to answer and so answer "not
     * there" or 0 when the open itself fails, this asks the file system's own index: what it
     * answers about a file that is there does not depend on whether the file could be opened.
     *
     * @param size Set to the file's size when the file is there
     * @param missing Set to true when the file system says the file is not there; false when the
     *        lookup itself failed
     * @return true when the file is there
     */
    virtual bool statFile(const char* filePath, size_t& size, bool& missing) = 0;
    virtual bool directoryExists(const char* path) = 0;
    virtual bool createDirectory(const char* path) = 0;
    virtual FileSystemStatusEnum getFileSystemStatus() = 0;
};