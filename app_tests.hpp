//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "app_state.hpp"

void app_tests_initialize(APP_Context* ctx, AppTestsState* tests);
void app_tests_reload(APP_Context* ctx, AppTestsState* tests);
void app_tests_tick(APP_Context* ctx, AppTestsState* tests, F32 deltaSeconds);
void app_tests_shutdown(APP_Context* ctx, AppTestsState* tests);
