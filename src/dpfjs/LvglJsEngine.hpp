#pragma once

// txiki's private.h pulls in libwebsockets.h, which under __cplusplus includes
// <cstddef>/<cstdarg> at namespace scope before opening its own extern "C".
// Including private.h inside the extern "C" block below would trap those stdlib
// includes in C linkage — and libc++'s <cstddef> defines std::byte operator
// templates, which are illegal under extern "C" ("templates must have C++
// linkage"). Pre-include them here so libwebsockets.h's copies hit the include
// guards and become no-ops. (Surfaced on macOS/libc++ by the newer upstream
// libwebsockets, which added the <cstddef> include.)
#include <cstddef>
#include <cstdarg>

extern "C" {
    #include "tjs.h"
    #include "private.h"
    #include "lvgl.h"
}

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "host/TjsHostRuntime.hpp"
#include "native/components/window/window.hpp"

class LvglJsEngine;

// Extends lv_binding_js display data with the TJS runtime pointer.
// Stored on each LVGL display via lv_display_set_user_data().
struct DpfJsDisplayData : LvBindingJsDisplayData {
    TJSRuntime* runtime = nullptr;
    LvglJsEngine* engine = nullptr;

    static DpfJsDisplayData* get() {
        lv_display_t* disp = lv_display_get_default();
        return disp ? static_cast<DpfJsDisplayData*>(lv_display_get_user_data(disp)) : nullptr;
    }
};

class LvglJsEngine {
public:
    using ParamWriteCallback = std::function<void(uint32_t index, float value)>;

    // Owns its own txiki host (LV2 / standalone-without-DSP fallback).
    LvglJsEngine();
    // References an external, already-initialized host owned elsewhere (the
    // plugin-lifetime runtime the DSP owns). The engine adds only the LVGL
    // display layer on top and never shuts the host down.
    explicit LvglJsEngine(TjsHostRuntime& externalHost);
    ~LvglJsEngine();

    // Switch to an external host BEFORE init() (equivalent to the external-host
    // constructor, but usable on a default-constructed value member once the
    // host pointer becomes known). No effect after init().
    void useExternalHost(TjsHostRuntime& externalHost);

    // Init the host (only if owned) + the ONE-TIME-per-host context setup: the
    // lvgljs namespace, native component constructors, the libuv job pump, the
    // cookie jar. Idempotent per host — a second editor session on the same
    // persistent host skips the context setup (detected via the lvgljs
    // namespace already existing). Does NOT bind a display or mount React; call
    // attachDisplay() for that after the bundle is evaluated.
    bool init();
    void tick();
    void shutdown();

    // Per-editor-session display lifecycle on a persistent runtime. The context,
    // lvgljs namespace, and evaluated bundle survive across these; only the LVGL
    // display binding + the mounted React tree are torn down and rebuilt. Lets
    // the runtime outlive the editor window (open -> close -> reopen) without a
    // re-eval (which QuickJS module caching would no-op). `attachDisplay` binds
    // the current default display, builds the window root, and mounts React (via
    // the bundle's __rp_mountUI hook); `detachDisplay` unmounts (__rp_unmountUI)
    // and drops the window root + display user_data. A pump must run between a
    // detach and the next attach to flush LVGL's async deletes.
    void attachDisplay();
    void detachDisplay();

    // True when THIS init() freshly installed the host context (vs. reusing a
    // persistent host a prior editor session already set up). The caller uses it
    // to evaluate the UI bundle exactly once per host — a reused host already
    // has the bundle (and its __rp_mountUI hook) registered.
    bool contextFresh() const { return contextFresh_; }
    int evalModule(const char* filename);
    int evalModuleBuffer(const char* code, size_t len, const char* name);
    int evalModuleBytecode(const uint8_t* bytecode, size_t len);
    int evalString(const char* code);
    JSContext* getContext() const;

    // Returns a *new* reference to the lvgljs Symbol-keyed JS namespace object.
    // Caller must JS_FreeValue. Used by external code (e.g. PluginJsBridge) to
    // attach further native methods without the engine knowing about them.
    JSValue lvglJsNamespace() const;

    // The embedded txiki host (owned or external). PluginJsBridge uses it to
    // bind the generic __rpcSend trampoline onto its own "plugin" namespace.
    TjsHostRuntime& host() { return *host_; }

    // Fire all handlers registered via lvgljs.on(channel, fn).
    // argv values are not consumed; engine handles refcounting internally.
    void emit(const char* channel, int argc, JSValueConst* argv);

    // Parameter machinery: every plugin needs a way for JS to write parameter
    // values to the host and to receive host-side parameter changes.

    // Set the callback invoked when JS calls lvgljs.setParameter(idx, val).
    void setParamWriteCallback(ParamWriteCallback cb);

    // Register a parameter name->index mapping so JS can refer to parameters
    // by name. Throws no error on conflict — last write wins.
    void registerParameter(uint32_t index, const std::string& name);

    // DSP -> JS: emit a "parameter" event with (index, value) to all handlers.
    void pushParameter(uint32_t index, float value);

private:
    static JSValue js_lvgljs_on(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_lvgljs_off(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_lvgljs_setParameter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_lvgljs_getParameterIndex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_lvgljs_getParameterCount(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // Call a zero-arg global function by name (the bundle's __rp_mountUI /
    // __rp_unmountUI lifecycle hooks). No-op if absent or not a function.
    void callAppLifecycle(const char* globalName);

    // One-time-per-host context setup (lvgljs namespace + native components +
    // job pump + cookie jar). Returns false if it detects the namespace is
    // already present (a prior session did it) so the caller can skip.
    bool setupHostContextOnce();

    TjsHostRuntime  ownedHost_;             // used only when no external host
    TjsHostRuntime* host_ = &ownedHost_;    // active host (owned or external)
    bool ownsHost_ = true;
    bool contextFresh_ = false;             // this init() installed the context
    JSContext* ctx = nullptr;  // cached host_->context()
    DpfJsDisplayData displayData;
    bool initialized = false;
    JSValue lvgljsObj;  // initialized to JS_UNDEFINED in ctor (not a constexpr in C++)
    std::map<std::string, std::vector<JSValue>> handlers;
    ParamWriteCallback paramWrite;
    std::map<std::string, uint32_t> paramNameToIndex;
    std::map<uint32_t, std::string> paramIndexToName;
};
