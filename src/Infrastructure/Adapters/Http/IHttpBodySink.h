#pragma once

#include <cstddef>

/**
 * @brief Where a response body goes while it arrives, instead of into one buffer.
 *
 * The alternative this exists next to is getString(), which answers only once the whole body is in
 * one heap block. For a list endpoint that block is the size of the list, and the module has to
 * have that much free at one moment on top of everything the list is about to become. A sink is
 * handed the body in the pieces the transport reads it in, so what it costs is a property of the
 * sink and not of the answer's length.
 */
class IHttpBodySink
{
public:
    virtual ~IHttpBodySink() = default;

    /**
     * @brief Take the next piece of the body.
     *
     * @param data   the piece, not null-terminated
     * @param length its length in bytes
     * @return false to end the transfer early - the body is already known to be unusable, and
     *         reading the rest of it would buy nothing
     */
    virtual bool write(const char* data, size_t length) = 0;
};
