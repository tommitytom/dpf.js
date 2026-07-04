#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "host/TjsHostRuntime.hpp"
#include "RpcEnvelope.h"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "qjs/read.hpp"
#include "transports/QuickJSTransport.h"

extern "C" {
    #include <quickjs.h>
}

namespace dpfjs {

// Generic QuickJS <-> rpcpp bridge for a concrete Service. Owns the rpc
// server + transport, builds a Symbol.for(<namespace>) JS object with the single
// sync `__rpcSend` entry (dispatched through the shared txiki host) plus a
// `__log` stderr shim, and drains async/notification frames to a settable
// emit sink ("rpc-message"). The consumer registers the service's methods on
// server() and may attach further native props to jsNamespace().
//
// Bound to a bare TjsHostRuntime (not LvglJsEngine) so the bridge can live on
// the plugin-lifetime host with no LVGL/display dependency — the display side
// supplies the emit sink when the editor attaches (setEmitSink), and clears it
// on detach. While detached the async drain simply has nowhere to deliver
// (nothing pumps it either), which is fine.
//
// A template (not a type-erased base) because TypedRpcServer reflects the
// concrete Service's method set at compile time — `processMessage` and
// `writeNotification<T>` are concrete-Service members. Keeping the server
// concrete here means the domain bridge calls writeNotification directly (no
// indirection) and __rpcSend has no vcall.
template <class Service>
class JsRpcBridge {
public:
    using RpcTransport = rpcpp::QuickJSTransport;
    using RpcServer    = rpcpp::TypedRpcServer<Service, rpcpp::QuickJSCodec>;

    // Delivers async/notification frames to JS. Set by the display side on
    // editor attach (routes to LvglJsEngine::emit), cleared on detach.
    using EmitFn = std::function<void(const char* channel, int argc, JSValueConst* argv)>;

    JsRpcBridge(TjsHostRuntime& host, Service& service, const char* namespaceSymbol)
        : host_(host) {
        // The QuickJS bridge marshals directly against a live context, so unlike
        // a byte codec it cannot be built before the host has one. Construct the
        // context first and bail if absent (real consumers init the host first).
        JSContext* ctx = host_.context();
        if (!ctx) return;

        // transport must outlive server (member order below). The transport's
        // delivery callback fires on the JS thread (from pumpAsync -> drain) and
        // forwards each materialized async/notification response object through
        // the current emit sink (nowhere while the editor is detached).
        transport_ = std::make_unique<RpcTransport>(ctx,
            [this](JSContext*, JSValue v) { if (emit_) emit_("rpc-message", 1, &v); });
        server_    = std::make_unique<RpcServer>(service, *transport_,
                                                 rpcpp::QuickJSCodec{ctx});

        JSValue global = JS_GetGlobalObject(ctx);
        JSValue sym    = JS_NewSymbol(ctx, namespaceSymbol, /*is_global*/ true);
        JSAtom atom    = JS_ValueToAtom(ctx, sym);
        JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);

        JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);

        // __rpcSend: the host's generic trampoline; we supply the dispatch
        // callable, capturing this (one bridge per engine; the host frees the
        // binding at shutdown, after the bridge). Echo server-side JSON-RPC
        // error envelopes to stderr — the JS client drops error replies with a
        // null id (every notification reply), so without this a typed-handler
        // exception in C++ would read as "nothing happened" on the UI side.
        host_.bindRpcSend(ns,
            [this](JSContext* sctx, JSValueConst req) -> JSValue {
                auto out = server_->processMessage(req);
                if (!out) return JS_NULL;             // notification / no reply
                // Materialize the response on the JS thread (sync path).
                JSValue resp = out->materialize(sctx);
                // Echo server-side error envelopes to stderr — the JS client
                // drops error replies (null id), so without this a typed-handler
                // exception in C++ reads as "nothing happened" on the UI side.
                // read() borrows resp; a success reply has no `error` field, so
                // the read fails and we skip logging.
                if (auto err = rpcpp::qjs::read<rpcpp::RpcError>(sctx, resp);
                    err && err->error.code) {
                    std::fprintf(stderr, "[rpc] error %d: %s\n",
                                 err->error.code, err->error.message.c_str());
                }
                return resp;                          // owned; handed to JS
            });
        JS_SetPropertyStr(ctx, ns, "__log",
                          JS_NewCFunction(ctx, js_log, "__log", 2));

        ns_ = JS_DupValue(ctx, ns);

        JS_FreeAtom(ctx, atom);
        JS_FreeValue(ctx, sym);
        JS_FreeValue(ctx, global);
    }

    ~JsRpcBridge() {
        if (JSContext* ctx = host_.context(); ctx && !JS_IsUndefined(ns_)) {
            JS_FreeValue(ctx, ns_);
            ns_ = JS_UNDEFINED;
        }
    }

    JsRpcBridge(const JsRpcBridge&)            = delete;
    JsRpcBridge& operator=(const JsRpcBridge&) = delete;

    RpcServer&    server()      { return *server_; }
    RpcTransport& transport()   { return *transport_; }
    JSValue       jsNamespace() const { return ns_; }  // borrowed; consumer adds props

    // Route async/notification frames to this sink (editor attach), or pass {}
    // to stop delivering (editor detach).
    void setEmitSink(EmitFn fn) { emit_ = std::move(fn); }

    // Drain the rpc transport's outgoing queue (async resolver responses +
    // notification frames). Each deferred thunk is materialized on the JS thread
    // into a response object and routed to engine.emit("rpc-message", object) by
    // the transport's delivery callback. Call from the JS event loop.
    void pumpAsync() {
        if (!transport_) return;
        transport_->drain();
    }

private:
    // Console-stderr shim: `__log(level, msg)`. tjs has no native console, so
    // the JS-side polyfill (ui/runtime/console.ts) routes through this.
    static JSValue js_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        const char* level = (argc >= 1) ? JS_ToCString(ctx, argv[0]) : nullptr;
        const char* msg   = (argc >= 2) ? JS_ToCString(ctx, argv[1]) : nullptr;
        std::fprintf(stderr, "[js:%s] %s\n", level ? level : "log", msg ? msg : "");
        if (level) JS_FreeCString(ctx, level);
        if (msg)   JS_FreeCString(ctx, msg);
        return JS_UNDEFINED;
    }

    TjsHostRuntime& host_;
    EmitFn          emit_;
    JSValue         ns_ = JS_UNDEFINED;
    std::unique_ptr<RpcTransport> transport_; // before server_
    std::unique_ptr<RpcServer>    server_;
};

} // namespace dpfjs
