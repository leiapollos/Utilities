//
// Created by André Leite on 26/07/2025.
//

#pragma once

static void base_entry_point(int argc, char **argv);

static void thread_entry_point(void (*func)(void* params), void* args);
