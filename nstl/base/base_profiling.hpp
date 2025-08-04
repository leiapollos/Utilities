//
// Created by André Leite on 04/08/2025.
//

#pragma once

struct TimedBlockData {
    U64 totalTime;
    const char* name;
    const char* debugName;
};

#define MAX_NUMBER_OF_TIMED_BLOCKS 4096

struct TimedBlock {
    TimedBlock(U32 id_, const char* name_, const char* debugName_);
    ~TimedBlock();
    const char* name;
    const char* debugName;
    U32 id;
    U64 start;
};

#define TIMED_BLOCK_NAME_2(A, B) A '|' B
#define TIMED_BLOCK_NAME() TIMED_BLOCK_NAME_2(__FILE__, __LINE__)

#define TIMED_BLOCK_2(GUID, Name, DebugName) TimedBlock NAME_CONCAT(TimedBlock_,__LINE__)(GUID, Name, MACRO_STR(DebugName))
#define TIMED_BLOCK(Name) TIMED_BLOCK_2(__COUNTER__, Name, TIMED_BLOCK_NAME())

#define TIMED_FUNCTION_2(GUID, DebugName) TimedBlock NAME_CONCAT(TimedBlock_,__LINE__)(GUID, __FUNCTION__, MACRO_STR(DebugName))
#define TIMED_FUNCTION(...) TIMED_FUNCTION_2(__COUNTER__, TIMED_BLOCK_NAME())