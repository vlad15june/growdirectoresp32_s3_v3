#include <unity.h>
#include "Core/Domain/UseCases/ModuleMode/CheckModuleDeletionUseCase.h"
#include "Core/Domain/Data/Models/ModuleDeletionQueryResult.h"
#include "Core/Domain/Enums/ModuleDeletionStatusEnum.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"

static MockHybridAPIService g_mockHybrid;
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static CheckModuleDeletionUseCase* g_useCase;

static void check_module_deletion_setup(void) {
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockDebugSystem.featureEnabled = true;
    g_mockHybrid.queryModuleDeletionResult = ModuleDeletionQueryResult();
    g_useCase = new CheckModuleDeletionUseCase(&g_mockHybrid, &g_mockLogger, &g_mockDebugSystem);
}

static void check_module_deletion_teardown(void) {
    delete g_useCase;
    g_useCase = nullptr;
}

void test_evaluate_sustained_403_returns_deleted(void) {
    check_module_deletion_setup();

    g_mockHybrid.queryModuleDeletionResult.received200 = false;
    g_mockHybrid.queryModuleDeletionResult.allTries403 = true;
    g_mockHybrid.queryModuleDeletionResult.finalHttpCode = 403;

    TEST_ASSERT_TRUE(g_useCase->evaluate() == ModuleDeletionStatusEnum::DELETED);

    check_module_deletion_teardown();
}

void test_evaluate_200_is_deleted_true_returns_deleted(void) {
    check_module_deletion_setup();

    g_mockHybrid.queryModuleDeletionResult.received200 = true;
    g_mockHybrid.queryModuleDeletionResult.isDeletedFlag = 1;
    g_mockHybrid.queryModuleDeletionResult.finalHttpCode = 200;

    TEST_ASSERT_TRUE(g_useCase->evaluate() == ModuleDeletionStatusEnum::DELETED);

    check_module_deletion_teardown();
}

void test_evaluate_200_is_deleted_false_returns_not_deleted(void) {
    check_module_deletion_setup();

    g_mockHybrid.queryModuleDeletionResult.received200 = true;
    g_mockHybrid.queryModuleDeletionResult.isDeletedFlag = 0;
    g_mockHybrid.queryModuleDeletionResult.finalHttpCode = 200;

    TEST_ASSERT_TRUE(g_useCase->evaluate() == ModuleDeletionStatusEnum::NOT_DELETED);

    check_module_deletion_teardown();
}

void test_evaluate_partial_403_returns_unknown(void) {
    check_module_deletion_setup();

    g_mockHybrid.queryModuleDeletionResult.received200 = false;
    g_mockHybrid.queryModuleDeletionResult.allTries403 = false;
    g_mockHybrid.queryModuleDeletionResult.finalHttpCode = 500;

    TEST_ASSERT_TRUE(g_useCase->evaluate() == ModuleDeletionStatusEnum::UNKNOWN);

    check_module_deletion_teardown();
}

void test_evaluate_query_failed_returns_unknown(void) {
    check_module_deletion_setup();

    g_mockHybrid.queryModuleDeletionResult.received200 = false;
    g_mockHybrid.queryModuleDeletionResult.queryFailed = true;
    g_mockHybrid.queryModuleDeletionResult.finalHttpCode = -1;

    TEST_ASSERT_TRUE(g_useCase->evaluate() == ModuleDeletionStatusEnum::UNKNOWN);

    check_module_deletion_teardown();
}

void test_evaluate_200_unparseable_body_returns_unknown(void) {
    check_module_deletion_setup();

    g_mockHybrid.queryModuleDeletionResult.received200 = true;
    g_mockHybrid.queryModuleDeletionResult.isDeletedFlag = -1;
    g_mockHybrid.queryModuleDeletionResult.finalHttpCode = 200;

    TEST_ASSERT_TRUE(g_useCase->evaluate() == ModuleDeletionStatusEnum::UNKNOWN);

    check_module_deletion_teardown();
}

void test_evaluate_null_api_returns_unknown(void) {
    CheckModuleDeletionUseCase useCase(nullptr, &g_mockLogger, &g_mockDebugSystem);
    TEST_ASSERT_TRUE(useCase.evaluate() == ModuleDeletionStatusEnum::UNKNOWN);
}

extern "C" void run_check_module_deletion_use_case_tests(void) {
    RUN_TEST(test_evaluate_sustained_403_returns_deleted);
    RUN_TEST(test_evaluate_200_is_deleted_true_returns_deleted);
    RUN_TEST(test_evaluate_200_is_deleted_false_returns_not_deleted);
    RUN_TEST(test_evaluate_partial_403_returns_unknown);
    RUN_TEST(test_evaluate_query_failed_returns_unknown);
    RUN_TEST(test_evaluate_200_unparseable_body_returns_unknown);
    RUN_TEST(test_evaluate_null_api_returns_unknown);
}
