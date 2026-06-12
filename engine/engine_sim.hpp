//
// Created by André Leite on 12/06/2026.
//
// The fixed-tick clock and the deterministic replay machinery, generic
// over the project's action and save blobs (sized by the project table,
// checksummed by its hook). The engine owns tick draining, recording,
// playback, and the save/replay files; the project owns what the bytes
// mean. Formats are forward-discard: either version field mismatching
// means the file is dead data, never an upgrade project.
//

#pragma once

#define ENG_SIM_TICK_HZ 60u
#define ENG_SIM_TICK_DT (1.0f / (F32)ENG_SIM_TICK_HZ)
#define ENG_SIM_MAX_FRAME_DT 0.25f
#define ENG_SIM_MAX_ACTION_SIZE 16u
#define ENG_SIM_MAX_SAVE_SIZE 128u

#define ENG_REPLAY_MAX_TICKS 18000u // 5 minutes at 60 Hz
#define ENG_REPLAY_CHECK_INTERVAL 60u
#define ENG_REPLAY_MAX_CHECKS (ENG_REPLAY_MAX_TICKS / ENG_REPLAY_CHECK_INTERVAL)

#define ENG_SAVE_MAGIC 0x56415355u  // "USAV"
#define ENG_SAVE_FORMAT_VERSION 4u  // v4: U19 generic blob format
#define ENG_SAVE_PATH "saves/player.usav"
#define ENG_REPLAY_MAGIC 0x50455255u // "UREP"
#define ENG_REPLAY_FORMAT_VERSION 4u
#define ENG_REPLAY_PATH "saves/session.urep"

enum EngReplayMode {
    EngReplayMode_Idle = 0u,
    EngReplayMode_Recording = 1u,
    EngReplayMode_Playing = 2u,
};

// Input replay: action blobs per tick from a captured start blob, with a
// project checksum every ENG_REPLAY_CHECK_INTERVAL ticks. Playback always
// reads back from disk so every replay validates the whole pipe; the
// first checksum mismatch names the exact diverging tick. Relative-tick
// indexed, so recording and playback are frame-rate independent.
struct EngReplay {
    U32 mode;
    U32 cursor;    // ticks recorded / consumed, relative to start
    U32 tickCount; // loaded replay length (playback only)
    U32 checkCount;
    U64 divergedAtTick; // absolute sim tick of first mismatch; 0 = clean
    U8 initial[ENG_SIM_MAX_SAVE_SIZE];
    U8 actions[ENG_REPLAY_MAX_TICKS * ENG_SIM_MAX_ACTION_SIZE];
    U64 checksums[ENG_REPLAY_MAX_CHECKS];
};

struct EngSaveFileHeader {
    U32 magic;
    U32 formatVersion; // engine file format
    U32 saveVersion;   // project payload layout
    U32 saveSize;
};

struct EngReplayFileHeader {
    U32 magic;
    U32 formatVersion;
    U32 saveVersion;
    U32 saveSize;
    U32 actionSize;
    U32 tickCount;
    U32 checkInterval;
    U32 pad0;
};
