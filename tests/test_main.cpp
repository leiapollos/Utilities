//
// Created by André Leite on 11/06/2026.
//
// CPU seam tests (U11): executable convention documentation for the seams
// that bit. Tool TU in the cooker pattern — no window, no GPU. Build and
// run with `./sob test [mode]`; the exit code is the failure count.
//

#include "nstl/base/base_include.hpp"

// Live OS core only — the graphics backend (Cocoa/Win32) stays out of
// tool TUs; tests run against the live tree, not the meta/ snapshot.
#include "nstl/os/core/os_core.hpp"
#if defined(PLATFORM_OS_WINDOWS)
#include "nstl/os/core/windows/os_core_windows.hpp"
#elif defined(PLATFORM_OS_MACOS)
#include "nstl/os/core/macos/os_core_macos.hpp"
#endif

#include "nstl/os/core/os_core.cpp"
#if defined(PLATFORM_OS_WINDOWS)
#include "nstl/os/core/windows/os_core_windows.cpp"
#elif defined(PLATFORM_OS_MACOS)
#include "nstl/os/core/macos/os_core_macos.cpp"
#endif
#include "nstl/base/base_include.cpp"

#include "nstl/prof/prof_include.hpp"
#include "nstl/prof/prof_include.cpp"

// Backend-shared gfx validation code; os_graphics.hpp is declarations only,
// the platform window backend stays out of tool TUs.
#include "nstl/os/graphics/os_graphics.hpp"
#include "nstl/gfx/gfx.hpp"
#include "nstl/gfx/gfx_common.cpp"

// Host-side text (FreeType + kbts) so the glyph-cache statics are in-TU.
#include "nstl/text/text_include.hpp"
#include "nstl/text/text_include.cpp"

// Module-side draw2d + ui so the key statics are in-TU.
#include "nstl/draw2d/draw2d_include.hpp"
#include "nstl/draw2d/draw2d_include.cpp"
#include "nstl/ui/ui_include.hpp"
#include "nstl/ui/ui_include.cpp"

#include "engine/shaders/shader_records.generated.hpp"
#include "engine/engine_sim.hpp"
#include "engine/engine_world_kernels.hpp"
#include "projects/demo/demo_game_kernels.hpp"
#include "projects/demo/demo_scene_kernels.hpp"
#include "nstl/audio/audio_mixer.hpp"

#include <stdio.h>

static U32 g_suiteChecks;
static U32 g_suiteFails;
static U32 g_totalChecks;
static U32 g_totalFails;

static void test_check_(B32 ok, const char* expr, const char* file, int line) {
    g_suiteChecks += 1u;
    g_totalChecks += 1u;
    if (!ok) {
        g_suiteFails += 1u;
        g_totalFails += 1u;
        fprintf(stderr, "  FAIL %s:%d  %s\n", file, line, expr);
    }
}

static F32 test_abs_(F32 value) {
    return (value < 0.0f) ? -value : value;
}

#define TEST_CHECK(cond) test_check_((cond) ? 1 : 0, #cond, __FILE__, __LINE__)
#define TEST_CHECK_NEAR(a, b, eps) test_check_((test_abs_((a) - (b)) <= (eps)) ? 1 : 0, \
                                               #a " ~= " #b, __FILE__, __LINE__)

static Vec3F32 test_vec3_(F32 x, F32 y, F32 z) {
    Vec3F32 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

// The point-transform convention, byte-for-byte what world.slang's
// world_mul_point computes: row-vector v·M against Mat4x4F32 storage
// (basis in storage rows 0..2, translation in storage row 3).
static Vec4F32 test_mul_point_(const Mat4x4F32* m, Vec3F32 p) {
    Vec4F32 r;
    r.x = p.x * m->v[0][0] + p.y * m->v[1][0] + p.z * m->v[2][0] + m->v[3][0];
    r.y = p.x * m->v[0][1] + p.y * m->v[1][1] + p.z * m->v[2][1] + m->v[3][1];
    r.z = p.x * m->v[0][2] + p.y * m->v[1][2] + p.z * m->v[2][2] + m->v[3][2];
    r.w = p.x * m->v[0][3] + p.y * m->v[1][3] + p.z * m->v[2][3] + m->v[3][3];
    return r;
}

#include "test_math.cpp"
#include "test_frustum_cull.cpp"
#include "test_winding.cpp"
#include "test_addressing.cpp"
#include "test_gfx_upload.cpp"
#include "test_ordering.cpp"
#include "test_text_ui.cpp"
#include "test_base.cpp"
#include "test_game.cpp"
#include "test_collision.cpp"
#include "test_audio.cpp"

typedef void TestSuiteProc(void);

struct TestSuite {
    const char* name;
    TestSuiteProc* proc;
};

void entry_point(void) {
    static const TestSuite suites[] = {
        {"math", test_math_},
        {"frustum_cull", test_frustum_cull_},
        {"winding", test_winding_},
        {"addressing", test_addressing_},
        {"gfx_upload", test_gfx_upload_},
        {"ordering", test_ordering_},
        {"text_ui", test_text_ui_},
        {"base", test_base_},
        {"game", test_game_},
        {"collision", test_collision_},
        {"audio", test_audio_},
    };

    for (U32 at = 0u; at < (U32)(sizeof(suites) / sizeof(suites[0])); ++at) {
        g_suiteChecks = 0u;
        g_suiteFails = 0u;
        suites[at].proc();
        printf("%-14s %3u checks  %u failures\n", suites[at].name, g_suiteChecks, g_suiteFails);
    }
    printf("tests: %u checks, %u failures\n", g_totalChecks, g_totalFails);
    if (g_totalFails != 0u) {
        // The framework main always returns 0; failures exit here so
        // `./sob test` propagates the count.
        exit((int)g_totalFails);
    }
}
