#pragma once

#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Infrastructure/Adapters/Http/IHttpBodySink.h"
#include "Infrastructure/Adapters/Json/JsonArrayStreamSplitter.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Builds a rule set out of a rule list that arrives in pieces.
 *
 * The array is cut into its objects as the body goes past and each object is handed to the
 * converter on its own, so what is alive at any moment is one rule's text and one rule - never the
 * answer, and never a document sized for the whole of it. The length of an account's rule set
 * therefore decides neither what reading it costs nor whether it can be read at all: a document
 * holding the whole array is a wall rather than a limit, and a rule set one rule past such a wall
 * yields no rules at all rather than the rules that fit.
 *
 * A rule the module does not take - one the converter cannot read, one larger than the caller's
 * limit on a rule, one past the number of rules the caller keeps - is left out, counted and passed
 * over; the rules around it stand. Failing the whole answer over one element would stop every other rule of the
 * account from being updated for as long as the bad one is published.
 *
 * What such an answer is *not* is a statement about which rules exist, and readWhole() is how the
 * caller knows: a rule set is compared against the stored one only when the module read all of it.
 * The merge retires the stored rules an answer does not name, and a rule left out here is missing
 * from the answer for a reason that has nothing to do with the server having deleted it - so
 * without that distinction one unreadable element switches a device off.
 *
 * What is not degradation is a body that yields nothing: an answer that carried objects and no
 * readable rule is Failed, never Empty. Empty travels on as "this module has no rules", and a
 * snapshot is authoritative - the merge marks everything the snapshot does not name DELETING - so
 * a module would delete its rules because it could not read them. For the same reason a body that
 * stops in the middle fails whole: the rules before the cut are the beginning of a snapshot.
 *
 * The same reader serves the socket and the rule file: one reading of one format, with only the
 * parsing of a single rule left to the converter.
 */
class RuleStreamReader : public BufferedJsonArraySplitter, public IHttpBodySink
{
public:
    /**
     * @brief The largest rule this module takes off the wire, in bytes of JSON as it arrives.
     *
     * A declared limit on one rule, the way MAX_RULES_PER_MODULE is a declared limit on their
     * number. Measured against the shapes a rule can take - a schedule of forty windows is ~2.4 KB,
     * a sensor rule with twelve conditions ~2.4 KB - so this is above the working range and still
     * small enough that the documents derived from it are served by the pool.
     *
     * It measures the form the rule arrives in, which is not the form it is stored in: the stored
     * form carries members the server does not send and comes out larger - about a sixth larger
     * for a schedule, plus a fixed couple of hundred bytes. That is why this limit is applied to
     * input from outside and not to the module's own rule file: a file read under a limit measured
     * on the other form refuses rules the module itself wrote, and a rule file that cannot be read
     * is a registry that comes up empty. The stored form of the largest rule this limit admits
     * still fits MAX_OBJECT_BYTES with room, which is what keeps the file readable at all sizes.
     *
     * A rule past it is refused, counted, and left to the server to hear about; it does not fail
     * the answer around it.
     */
    static const size_t MAX_RULE_BYTES = 4096;

    /**
     * @brief The run of bytes past which the body is not an array of rules at all.
     *
     * Twice the largest rule: an object that goes on beyond that has no end in sight, and reaching
     * this fails the body rather than truncating an object into a shorter one. It also bounds the
     * buffer one object is cut into.
     */
    static const size_t MAX_OBJECT_BYTES = 2 * MAX_RULE_BYTES;

    /**
     * @brief Room taken for the rule being cut out, reserved once.
     *
     * A rule of the shape the backend sends is 600 to 700 bytes of JSON; the buffer is reused for
     * every rule of the array, so this is the whole reader's appetite for a list of any length.
     */
    static const size_t RULE_INITIAL_CAPACITY = 1024;

    /**
     * @param maxRules how many rules to build out of this body, or zero for no limit
     * @param maxRuleBytes the largest object to build a rule out of, or zero for no limit
     *
     * Both are the module's policy for input from outside, and both are given by the caller
     * rather than assumed here, because the two readers in this firmware read two different
     * things. An answer from the server is read with the module's limits: a long answer then
     * costs what the module's own rule set costs, and a rule too large to run is refused at the
     * door. The module's own rule file is read with neither, because it holds what the module
     * itself wrote - and it wrote it in a larger form than the one the limits were measured on,
     * so a file read under them would refuse the module's own rules and leave the registry empty.
     */
    RuleStreamReader(IJsonConverter& converter, std::vector<Rule>& rules, size_t maxRules = 0,
                     size_t maxRuleBytes = 0)
        : BufferedJsonArraySplitter(MAX_OBJECT_BYTES, RULE_INITIAL_CAPACITY),
          _converter(converter),
          _rules(rules),
          _maxRules(maxRules),
          _maxRuleBytes(maxRuleBytes)
    {
    }

    // --- IHttpBodySink ---------------------------------------------------------------------

    bool write(const char* data, size_t length) override
    {
        keepPrefix(data, length);
        feed(data, length);
        return !failed();
    }

    /** @brief What the body turned out to be - and no rules at all unless it was readable. */
    JsonListParse finish() override
    {
        JsonListParse verdict = JsonArrayStreamSplitter::finish();

        // Objects came out of the body and not one of them was a rule. That is an answer this
        // module cannot read, and it must not reach the merge as an empty snapshot.
        if (verdict == JsonListParse::Ok && _readCount == 0)
        {
            verdict = JsonListParse::Failed;
        }

        if (verdict == JsonListParse::Failed)
        {
            _rules.clear();
        }
        return verdict;
    }

    /** @brief Rules read out of the body. */
    size_t readCount() const { return _readCount; }

    /** @brief Objects that were rules this module could not read, and were left out. */
    size_t skippedCount() const { return _skippedCount; }

    /** @brief Rules the module did not take: past its capacity, or larger than it accepts. */
    size_t refusedCount() const { return _refusedCount; }

    /**
     * @brief True when every rule the body carried was built into the list.
     *
     * What the caller acts on: a rule set is only a statement about which rules exist if the
     * module read all of it. One left out - unreadable, oversized, or past the number this module
     * keeps - makes the answer a set of rules to apply and not a set to compare the stored ones
     * against.
     */
    bool readWhole() const { return _skippedCount == 0 && _refusedCount == 0; }

    /** @brief The first bytes of the body, kept for the log that reports an unusable answer. */
    const char* prefix() const { return _prefix; }

protected:
    bool onObject(const std::string& objectJson) override
    {
        // Refused before it is built, not after: a rule past the module's capacity or larger than
        // it accepts costs nothing but the bytes it was cut into, which the next object reuses.
        if (_maxRules != 0 && _readCount >= _maxRules)
        {
            _refusedCount++;
            return true;
        }

        if (_maxRuleBytes != 0 && objectJson.size() > _maxRuleBytes)
        {
            _refusedCount++;
            return true;
        }

        Rule rule;
        if (!_converter.ruleFromJson(objectJson, rule))
        {
            _skippedCount++;
            return true;
        }

        // Moved in: the strings of a rule are its uids, and copying them to hand the rule over
        // would put a second rule in memory for the length of the call.
        _rules.push_back(std::move(rule));
        _readCount++;
        return true;
    }

private:
    /**
     * @brief Keep the head of the body for the log, up to a fixed length.
     *
     * The whole-body path logged the answer it had in a string anyway. There is no such string
     * here, and reading one back for the log would be the buffer this class exists to avoid - so a
     * bounded head is copied as it goes past, into storage that is part of this object.
     */
    void keepPrefix(const char* data, size_t length)
    {
        size_t room = PREFIX_BYTES - _prefixLength;
        if (room == 0 || data == nullptr)
        {
            return;
        }
        if (length < room)
        {
            room = length;
        }
        for (size_t i = 0; i < room; i++)
        {
            _prefix[_prefixLength + i] = data[i];
        }
        _prefixLength += room;
        _prefix[_prefixLength] = '\0';
    }

    static const size_t PREFIX_BYTES = 200;

    IJsonConverter& _converter;
    std::vector<Rule>& _rules;
    size_t _maxRules = 0;
    size_t _maxRuleBytes = 0;
    size_t _readCount = 0;
    size_t _skippedCount = 0;
    size_t _refusedCount = 0;
    char _prefix[PREFIX_BYTES + 1] = {0};
    size_t _prefixLength = 0;
};
