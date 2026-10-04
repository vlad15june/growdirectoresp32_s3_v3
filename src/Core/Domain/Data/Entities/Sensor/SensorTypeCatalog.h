#pragma once

#include "SensorType.h"
#include <memory>
#include <vector>

/**
 * @brief The sensor type catalog as one shared, immutable list.
 *
 * The catalog is the largest single thing this module holds - 1.4 to 2 KB per type, and an account
 * with forty of them is around 55 KB against a heap that has tens of KB free at boot. A reader that
 * is handed the catalog by value doubles it for as long as it reads, which is a transient the size
 * of the account's catalog on a board that cannot spare one.
 *
 * So the catalog is handed out as a handle to a list nobody writes to. Readers share one list; a
 * write replaces the whole list with a new one rather than editing the old, so a reader holding a
 * handle keeps reading the list it took, whole, until it lets go. That is the snapshot
 * semantics a reader needs - it sees the old catalog or the new one, never a
 * half-applied mixture - without a copy per reader.
 *
 * The handle is also what crosses a task boundary: the control loop takes it from the repository
 * and the acquisition task scans from it, because the repositories belong to the loop.
 */
using SensorTypeCatalog = std::shared_ptr<const std::vector<SensorType>>;
