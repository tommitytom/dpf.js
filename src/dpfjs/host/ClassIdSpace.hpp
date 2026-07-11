#pragma once

// quickjs.h only — no txiki, no LVGL, no DSP. This keeps the helper usable from
// the isolated `test_host` target (which compiles TjsHostRuntime.cpp with only
// dpf.js/src on the include path and links just `tjs`) and keeps the host runtime
// LVGL-agnostic.
#include <quickjs.h>

namespace dpfjs {

// quickjs-ng's JS_NewClass1 rejects any class id >= (1 << 16), so no class can
// ever be registered at or above this. It is the exact, complete upper bound for
// a registered-class scan — not an eyeballed guess.
// (quickjs.c: `if (class_id >= (1 << 16)) return -1;`)
inline constexpr JSClassID kQuickJsClassIdCeiling = static_cast<JSClassID>(1u) << 16;

// Advance `rt`'s per-runtime class-id allocator strictly past every class id
// currently registered on `rt`, so the next fresh JS_NewClassID(rt, &zero) yields
// an id no live class holds — its JS_SetClassProto then cannot clobber another
// class's prototype.
//
// The hazard this closes: JS_NewClassID caches each id in a process-global
// `static JSClassID`, but allocates from a PER-RUNTIME counter. A runtime that
// REUSES a static an earlier runtime already set does NOT advance its own counter,
// yet JS_NewClass still grows this runtime's class_count for that reused id — so
// the counter is left trailing class_count. A later fresh allocation on this
// runtime is then handed an id a live class already owns. (This is why a DAW that
// scans the plugin — constructing a throwaway txiki runtime that seeds the statics
// before any editor registers lv_binding_js — then blanks the editor: the reused
// runtime's fresh lv_binding_js ids collide with live txiki classes and strip, for
// instance, TextDecoder.prototype.decode.)
//
// Call this immediately before any batch of fresh, static-cached class
// registrations (and once after, if that batch itself reuses cached ids on a
// non-first runtime — the reuse grows class_count without moving the counter).
//
// Correctness properties (all grounded in quickjs-ng's public behaviour):
//   * self-sizing — the reserve is the MEASURED high-water via JS_IsRegisteredClass,
//     not a constant, so it tracks txiki/lv_binding_js growth with no maintenance;
//   * hole-proof — it advances past the MAXIMUM registered id, never merely the
//     first unregistered one, so gaps below the high-water can't make it stop short
//     and later collide;
//   * per-runtime — JS_IsRegisteredClass / JS_NewClassID read and advance only
//     `rt`'s own state, so runtimes don't interfere regardless of scan/editor order;
//   * terminating — JS_NewClassID increments a uint32 monotonically, so the loop
//     ends in at most (highWater - counter + 1) steps and burns at most one spare id.
inline void syncClassIdAllocator(JSRuntime* rt) {
    // JS_IsRegisteredClass short-circuits on `id < class_count`, so despite the
    // 64K ceiling this touches only O(class_count) real slots (a few hundred).
    // Do NOT "optimize" this into a stop-at-first-unregistered scan — that
    // reintroduces the hole-collision hazard the max scan exists to prevent.
    JSClassID highWater = 0;
    for (JSClassID id = 1; id < kQuickJsClassIdCeiling; ++id) {
        if (JS_IsRegisteredClass(rt, id))
            highWater = id;
    }

    JSClassID drawn;
    do {
        JSClassID scratch = 0;
        drawn = JS_NewClassID(rt, &scratch);
    } while (drawn <= highWater);
}

} // namespace dpfjs
