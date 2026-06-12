//
// Created by André Leite on 12/06/2026.
//
// The developer-facing engine surface: the project contract. A product
// (game or app) is a directory under projects/ whose TU root includes the
// engine unity, its own files, and defines eng_project_() returning its
// table. The engine boots window/text/ui/debug-overlay/hot-reload with
// zero project code; the 3D world, fixed-tick sim + replay, and cooked
// assets are capabilities the project opts into. Hooks may be null when
// the matching capability is off.
//

#pragma once

#define ENG_CAP_WORLD3D (1u << 0u)
#define ENG_CAP_SIM (1u << 1u)
#define ENG_CAP_AUDIO (1u << 2u)

struct EngState;
struct EngRendererFrame;
struct UI_Context;

struct EngContext {
    EngHost* host;
    HOT_StateStore* store;
    EngState* engine;
    void* project;          // the project's state slot
    const EngInput* input;  // valid for the duration of eng_frame
};

struct EngModelDesc {
    const char* path;  // cooked .umdl, repo-relative
    const char* label; // artifact key label
};

struct EngSoundDesc {
    const char* path;  // cooked .uaud, repo-relative
    const char* label;
};

// The project contract. State is a store slot the engine requires on the
// project's behalf; sim hooks treat actions and save payloads as opaque
// blobs sized by the table (the engine owns the clock, the replay
// machinery, and the files; the project owns what a tick means).
struct EngProject {
    const char* name;
    U64 stateId;
    U32 stateVersion;
    U64 stateSize;
    U64 stateAlignment;
    U32 capabilities;

    const EngModelDesc* models; // ENG_CAP_WORLD3D
    U32 modelCount;
    const EngSoundDesc* sounds; // ENG_CAP_AUDIO
    U32 soundCount;

    U32 actionSize;  // ENG_CAP_SIM, <= ENG_SIM_MAX_ACTION_SIZE
    U32 saveSize;    // ENG_CAP_SIM, <= ENG_SIM_MAX_SAVE_SIZE
    U32 saveVersion; // bump when the save payload layout changes

    void (*state_init)(EngContext* ctx, void* state);
    void (*pre_frame)(EngContext* ctx);                    // before the sim drain
    B32 (*sim_sample)(EngContext* ctx, void* outActions);  // returns: tick this frame?
    void (*sim_tick)(EngContext* ctx, const void* actions);
    void (*sim_capture)(EngContext* ctx, void* outSave);
    void (*sim_apply)(EngContext* ctx, const void* save);
    U64 (*sim_checksum)(EngContext* ctx);
    void (*frame)(EngContext* ctx, EngRendererFrame* frame); // scene submission
    void (*panels)(EngContext* ctx, UI_Context* ui);         // before engine panels
    void (*panels_post)(EngContext* ctx, UI_Context* ui);    // after all panels
    void (*debug_stats)(EngContext* ctx, UI_Context* ui);    // extra stats lines
};

// Defined by the project TU after the engine sources; the engine calls
// through this everywhere it needs project policy.
static const EngProject* eng_project_(void);
