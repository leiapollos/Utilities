//
// Created by André Leite on 12/06/2026.
//
// The demo's asset list, single source: one row per asset. The enums
// and desc tables derive from these rows, and `sob cook` discovers
// sources by scanning assets/src — adding an asset is the file plus
// one row. Row tokens are the file stems, so stems must be C
// identifiers.
//

#pragma once

#define DEMO_ASSET_COOKED_DIR "projects/demo/assets/cooked/"

#define DEMO_MODEL_LIST(X) \
    X(Duck) \
    X(Avocado) \
    X(Lantern) \
    X(Buggy)

#define DEMO_SOUND_LIST(X) \
    X(Jump) \
    X(Land) \
    X(Click) \
    X(Ambience)

// Both enums index the engine slots the desc tables register, in row
// order by construction.
#define DEMO_MODEL_ID_ENTRY_(name) DemoModel_##name,
enum DemoModel {
    DEMO_MODEL_LIST(DEMO_MODEL_ID_ENTRY_)
    DemoModel_Count,
};
#undef DEMO_MODEL_ID_ENTRY_

#define DEMO_SOUND_ID_ENTRY_(name) DemoSound_##name,
enum DemoSound {
    DEMO_SOUND_LIST(DEMO_SOUND_ID_ENTRY_)
    DemoSound_Count,
};
#undef DEMO_SOUND_ID_ENTRY_
