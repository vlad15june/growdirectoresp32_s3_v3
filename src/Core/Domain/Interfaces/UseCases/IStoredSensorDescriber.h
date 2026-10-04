#pragma once

#include "Core/Domain/Data/Models/ModuleStructure.h"

/**
 * @brief The one answer to "how is a stored sensor described in the module structure".
 *
 * There is exactly one right answer per stored row and it depends on one thing: whether the line
 * is answering for that row. A sensor the person has accepted is described as itself; a sensor
 * nobody has accepted yet is described as itself too, unless the module has asked the line about
 * it and nothing is coming back - then it is an empty port, which is what a boot scan reports
 * for it.
 *
 * The question is about the present, and a structure is built at any time: a sensor that was
 * silent while the module restarted and answers an hour later is described by its type again, and
 * one that answered then and has gone quiet since becomes an empty port without waiting for the
 * next restart.
 *
 * The interface exists because that answer is needed in two places that must not disagree: the
 * structure a boot builds, and the structure a republish rebuilds. The knowledge lives with the
 * discovery, which is what asks the line and what watches the sweep asking it; the republish only
 * needs to be told.
 */
class IStoredSensorDescriber
{
public:
    virtual ~IStoredSensorDescriber() = default;

    /**
     * @brief Write the records of every stored sensor into a node. Control loop only.
     * @param node Node of the module structure the records are written into
     */
    virtual void describeStoredSensors(Node& node) = 0;

    /**
     * @brief Say what became of the structure that was last described. Control loop only.
     *
     * The describer is the one place that knows what the module has told the server about each
     * row, and it can only know it once the structure has arrived: a description built while the
     * uplink was down was never anybody's answer. Whoever sends a structure this describer wrote
     * owes it this call - once, with the outcome of that structure.
     *
     * @param delivered Whether the server took the structure
     */
    virtual void onDescribedStructureSettled(bool delivered) = 0;
};
