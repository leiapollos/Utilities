//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "app_state.hpp"

U64 app_tests_permanent_size(void);
void app_tests_initialize(AppMemory* memory, AppCoreState* core, AppTestsState* tests);
void app_tests_reload(AppMemory* memory, AppCoreState* core, AppTestsState* tests);
void app_tests_tick(AppMemory* memory, AppCoreState* core, AppTestsState* tests, F32 deltaSeconds);
void app_tests_shutdown(AppMemory* memory, AppCoreState* core, AppTestsState* tests);
