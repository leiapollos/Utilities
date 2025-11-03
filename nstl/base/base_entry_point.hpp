//
// Created by André Leite on 26/07/2025.
//

#pragma once

void base_entry_point(int argc, char** argv);

void thread_entry_point(void (*func)(void* params), void* args);
