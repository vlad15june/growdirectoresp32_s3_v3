#ifndef MALFORMED_JSON_NUMBER_COUNTER_H
#define MALFORMED_JSON_NUMBER_COUNTER_H

#include <atomic>
#include <cstdint>

/**
 * @brief Numeric JSON fields whose text was not a number, counted so that reading one is never
 *        silent.
 *
 * A number that cannot be read leaves the reader two honest answers: refuse the whole document, or
 * carry on with the field's default. The readers here take the second - one damaged field costs
 * that field and not the stored registry, the rule list or the boot that reads them - and the
 * price of that choice is a value nobody asked for standing in a record that looks intact. This
 * number is the trace it leaves: a module scaling a channel by a default multiplier because its
 * stored one read as "-nan" says so here and nowhere else.
 *
 * Monotonic and process-wide - read it as a delta. Atomic because a document is parsed on
 * whichever task is holding it.
 */
class MalformedJsonNumberCounter
{
public:
    /** Report one field whose text could not be read as a number. */
    static void record();

    /** Fields counted since boot. */
    static uint32_t count();

private:
    static std::atomic<uint32_t> _count;
};

#endif // MALFORMED_JSON_NUMBER_COUNTER_H
