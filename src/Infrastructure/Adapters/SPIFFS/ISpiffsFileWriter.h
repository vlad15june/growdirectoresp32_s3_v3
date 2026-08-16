#pragma once
#include <cstddef>

/**
 * @brief Streaming writer for one file: chunks reach the file system as they are produced.
 *
 * The caller never has to hold the whole payload as a single heap block. GDV1-3827: serializing the
 * sensor type catalog into one std::string asked for a ~28 KB contiguous block for 14 types, which a
 * fragmented heap could not give, and the resulting std::bad_alloc ended up as an empty catalog.
 *
 * Bytes go to a temporary companion file (`<path>.tmp`). The target is replaced only by commit(),
 * so an interrupted or failed write leaves the previous file exactly as it was; a writer destroyed
 * without commit() removes its temporary file.
 */
class ISpiffsFileWriter {
public:
    virtual ~ISpiffsFileWriter() = default;

    /**
     * @brief Append one chunk to the temporary file
     * @param chunk Null-terminated text to write
     * @return true if the whole chunk was written, false otherwise (the writer stays failed)
     */
    virtual bool write(const char* chunk) = 0;

    /**
     * @brief Close the temporary file and replace the target file with it
     * @return true if the target now holds everything that was written, false otherwise
     */
    virtual bool commit() = 0;

    /**
     * @brief Drop everything written so far and leave the target file untouched
     */
    virtual void abort() = 0;

    /**
     * @brief Number of bytes handed to the file system so far
     */
    virtual size_t bytesWritten() const = 0;
};
