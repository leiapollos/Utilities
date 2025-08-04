//
// Created by André Leite on 04/08/2025.
//

TimedBlockData s_timedBlocks[MAX_NUMBER_OF_TIMED_BLOCKS];

TimedBlock::TimedBlock(U32 id_, const char* name_, const char* debugName_) {
    id = id_;
    name = name_;
    debugName = debugName_;
    start = OS_get_time_microseconds();
}

TimedBlock::~TimedBlock() {
    U64 end = OS_get_time_microseconds();
    U64 diff = end - start;
    s_timedBlocks[id].totalTime += diff;
    s_timedBlocks[id].name = name;
    s_timedBlocks[id].debugName = debugName;
}

