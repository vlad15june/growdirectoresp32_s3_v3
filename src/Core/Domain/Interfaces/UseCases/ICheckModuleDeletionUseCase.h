#ifndef I_CHECK_MODULE_DELETION_USE_CASE_H
#define I_CHECK_MODULE_DELETION_USE_CASE_H

#include "Core/Domain/Enums/ModuleDeletionStatusEnum.h"

class ICheckModuleDeletionUseCase {
public:
    virtual ~ICheckModuleDeletionUseCase() = default;

    /**
     * @brief Decide whether the module is deleted on the server
     *
     * Queries the API layer for the raw deletion status and applies the deletion
     * policy: a clean 200 with isDeleted:true, or a sustained 403 across the whole
     * retry budget, means DELETED; a 200 with isDeleted:false means NOT_DELETED;
     * any transport failure / ambiguous response means UNKNOWN (no reset).
     * @return ModuleDeletionStatusEnum verdict (DELETED / NOT_DELETED / UNKNOWN)
     */
    virtual ModuleDeletionStatusEnum evaluate() = 0;
};

#endif // I_CHECK_MODULE_DELETION_USE_CASE_H
