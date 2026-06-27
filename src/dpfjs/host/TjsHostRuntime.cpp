#include "TjsHostRuntime.hpp"

#include <cstring>

TjsHostRuntime::~TjsHostRuntime() {
    shutdown();
}

bool TjsHostRuntime::init() {
    if (ctx_)
        return true;

    // Idempotent global init: txiki's TJS_Initialize sets process-wide state
    // (signal handlers, the module loader), so it must run exactly once even
    // when several hosts are created in one process (multiple plugin windows,
    // or the UI test runner booting a second runtime).
    static bool tjsInitialized = false;
    if (!tjsInitialized) {
        static char arg0[] = "retroplug";
        static char* argv[] = { arg0, nullptr };
        TJS_Initialize(1, argv);
        tjsInitialized = true;
    }

    qrt_ = TJS_NewRuntime();
    if (!qrt_)
        return false;
    ctx_ = TJS_GetJSContext(qrt_);

    // Browser-compat global aliases. Upstream txiki.js removed its
    // src/js/polyfills/global.js (window/global/self -> globalThis) since it is
    // a server-side runtime. dpf.js hosts browser-oriented JS (React, plus any
    // code that hooks the synthetic 'load' event via `window.addEventListener`),
    // so install the aliases on every host right after the context exists,
    // before any module evaluates. Note: with QuickJS's async ES modules a
    // missing `window` here surfaces only as a rejected module promise, not a
    // synchronous error, so a regression would fail silently.
    static const char kBrowserGlobals[] =
        "globalThis.window = globalThis.global = globalThis.self = globalThis;";
    JSValue r = JS_Eval(ctx_, kBrowserGlobals, sizeof(kBrowserGlobals) - 1,
                        "<dpfjs-globals>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r))
        tjs_dump_error(ctx_);
    JS_FreeValue(ctx_, r);
    return true;
}

void TjsHostRuntime::shutdown() {
    if (!qrt_)
        return;
    // Free the runtime (and every JS function, incl. the __rpcSend trampolines)
    // before the captured RpcSendFns it points at.
    TJS_FreeRuntime(qrt_);
    qrt_ = nullptr;
    ctx_ = nullptr;
    rpcBindings_.clear();
}

void TjsHostRuntime::pump() {
    if (!qrt_)
        return;
    uv_run(TJS_GetLoop(qrt_), UV_RUN_NOWAIT);
    tjs__execute_jobs(ctx_);
}

int TjsHostRuntime::evalModule(const char* filename) {
    if (!ctx_)
        return -1;
    JSValue result = TJS_EvalModule(ctx_, filename, true);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx_);
        JS_FreeValue(ctx_, result);
        return -1;
    }
    JS_FreeValue(ctx_, result);
    return 0;
}

int TjsHostRuntime::evalModuleBuffer(const char* code, std::size_t len, const char* name) {
    if (!ctx_)
        return -1;
    // Same path as TJS_EvalModule, but fed from a buffer rather than a file.
    // Sets up import.meta.url and fires the synthetic 'load' event.
    JSValue result = TJS_EvalModuleContent(ctx_, name, true, false, code, len);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx_);
        JS_FreeValue(ctx_, result);
        return -1;
    }
    JS_FreeValue(ctx_, result);
    return 0;
}

int TjsHostRuntime::evalModuleBytecode(const std::uint8_t* bytecode, std::size_t len) {
    if (!ctx_)
        return -1;
    // tjs__eval_bytecode does JS_ReadObject(JS_READ_OBJ_BYTECODE) ->
    // JS_ResolveModule -> js_module_set_import_meta(false, false) ->
    // JS_EvalFunction. It does NOT fire the 'load' event that
    // TJS_EvalModuleContent fires when is_main=true, so do it here for parity
    // with evalModuleBuffer.
    if (tjs__eval_bytecode(ctx_, bytecode, len, true) != 0)
        return -1;

    // Use globalThis rather than `window`: upstream txiki dropped the
    // window/global/self browser-compat aliases, and the host runtime (CLI)
    // does not install them. dispatchEvent lives on globalThis either way.
    static const char kLoadEvent[] = "globalThis.dispatchEvent(new Event('load'));";
    JSValue result = JS_Eval(ctx_, kLoadEvent, sizeof(kLoadEvent) - 1,
                             "<global>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx_);
        JS_FreeValue(ctx_, result);
        return -1;
    }
    JS_FreeValue(ctx_, result);
    return 0;
}

int TjsHostRuntime::evalString(const char* code) {
    if (!ctx_)
        return -1;
    JSValue result = JS_Eval(ctx_, code, std::strlen(code), "<eval>",
                             JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_STRICT);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx_);
        JS_FreeValue(ctx_, result);
        return -1;
    }
    JS_FreeValue(ctx_, result);
    return 0;
}

// __rpcSend(request) -> response | null. Forwards the request JSValue to the
// captured RpcSendFn and returns its response JSValue (JS_NULL for a
// notification / no reply). The RpcSendFn* is carried in funcData[0] (an
// ArrayBuffer holding the pointer), so the trampoline needs no global to find
// its dispatch target. The host moves only opaque JSValues — the marshalling
// to/from C++ types happens inside the dispatch callable (the bridge's codec).
JSValue TjsHostRuntime::rpcSendThunk(JSContext* ctx, JSValueConst, int argc,
                                     JSValueConst* argv, int, JSValue* funcData) {
    std::size_t holderLen = 0;
    std::uint8_t* holder = JS_GetArrayBuffer(ctx, &holderLen, funcData[0]);
    if (!holder || holderLen != sizeof(RpcSendFn*))
        return JS_ThrowInternalError(ctx, "__rpcSend: missing dispatch binding");
    RpcSendFn* fn = nullptr;
    std::memcpy(&fn, holder, sizeof(fn));
    if (!fn || !*fn)
        return JS_ThrowInternalError(ctx, "__rpcSend: dispatch unavailable");
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "__rpcSend: expected (request)");

    return (*fn)(ctx, argv[0]);
}

void TjsHostRuntime::bindRpcSend(JSValue ns, RpcSendFn fn) {
    if (!ctx_)
        return;
    auto held = std::make_unique<RpcSendFn>(std::move(fn));
    RpcSendFn* raw = held.get();
    rpcBindings_.push_back(std::move(held));

    // Carry the RpcSendFn* through the function's data as an ArrayBuffer holding
    // the pointer bytes (QuickJS func-data are JSValues, not raw pointers).
    JSValue holder = JS_NewArrayBufferCopy(ctx_,
        reinterpret_cast<const std::uint8_t*>(&raw), sizeof(raw));
    // QuickJS-ng's JS_NewCFunctionData(ctx, func, length, magic, data_len, data)
    // has no name arg; the JS-visible name comes from JS_SetPropertyStr below.
    JSValue f = JS_NewCFunctionData(ctx_, rpcSendThunk, 1, 0, 1, &holder);
    JS_FreeValue(ctx_, holder);
    JS_SetPropertyStr(ctx_, ns, "__rpcSend", f);
}
