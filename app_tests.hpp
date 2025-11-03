//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "app_state.hpp"

U64 app_tests_permanent_size(void);
void app_tests_initialize(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests);
void app_tests_reload(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests);
void app_tests_tick(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests, F32 deltaSeconds);
void app_tests_shutdown(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests);

