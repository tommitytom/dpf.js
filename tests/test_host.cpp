#include <cstring>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <quickjs.h>

#include "dpfjs/host/TjsHostRuntime.hpp"

namespace {

// Evaluate a JS expression as global code and return whether it is strictly
// true. Exceptions count as false. Used to assert on values produced inside the
// JS engine (evalString only reports success/failure, not the value).
bool eval_true(JSContext* ctx, const char* code) {
    JSValue r = JS_Eval(ctx, code, std::strlen(code), "<test>", JS_EVAL_TYPE_GLOBAL);
    const bool ok = !JS_IsException(r) && JS_ToBool(ctx, r) == 1;
    JS_FreeValue(ctx, r);
    return ok;
}

// Create a JS namespace object, bind a __rpcSend dispatching to `fn`, and expose
// it as globalThis[<name>]. Mirrors what JsRpcBridge does, minus rpcpp.
void install_namespace(TjsHostRuntime& host, const char* globalName,
                       TjsHostRuntime::RpcSendFn fn) {
    JSContext* ctx = host.context();
    JSValue ns = JS_NewObject(ctx);
    host.bindRpcSend(ns, std::move(fn));
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, globalName, JS_DupValue(ctx, ns));
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, ns);
}

// A fake dispatch: returns null if the request has a truthy `notify`, otherwise
// { doubled: request.n * factor }. Pure quickjs — no rpcpp.
TjsHostRuntime::RpcSendFn doubler(int factor) {
    return [factor](JSContext* ctx, JSValueConst req) -> JSValue {
        JSValue notify = JS_GetPropertyStr(ctx, req, "notify");
        const bool isNotify = JS_ToBool(ctx, notify) == 1;
        JS_FreeValue(ctx, notify);
        if (isNotify) return JS_NULL;

        JSValue n = JS_GetPropertyStr(ctx, req, "n");
        std::int32_t v = 0;
        JS_ToInt32(ctx, &v, n);
        JS_FreeValue(ctx, n);

        JSValue resp = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, resp, "doubled", JS_NewInt32(ctx, v * factor));
        return resp;  // owned; handed back to JS
    };
}

}  // namespace

TEST_CASE("host: init, context, eval success and failure", "[host]") {
    TjsHostRuntime host;
    REQUIRE(host.init());
    REQUIRE(host.context() != nullptr);
    REQUIRE(host.init());  // idempotent

    REQUIRE(host.evalString("globalThis.__ok = 1 + 1;") == 0);
    REQUIRE(eval_true(host.context(), "globalThis.__ok === 2"));

    // A syntax error is reported as -1 (and dumped to stderr).
    REQUIRE(host.evalString("this is not valid javascript !!!") == -1);
}

TEST_CASE("host: pump drains a queued promise job", "[host]") {
    TjsHostRuntime host;
    REQUIRE(host.init());

    REQUIRE(host.evalString(
        "globalThis.x = 0; Promise.resolve().then(() => { globalThis.x = 42; });") == 0);
    host.pump();  // tjs__execute_jobs runs the .then continuation
    REQUIRE(eval_true(host.context(), "globalThis.x === 42"));
}

TEST_CASE("host: __rpcSend round-trips a request object to a response object",
          "[host][rpc]") {
    TjsHostRuntime host;
    REQUIRE(host.init());
    install_namespace(host, "NS", doubler(2));
    JSContext* ctx = host.context();

    SECTION("request object in, response object out") {
        REQUIRE(eval_true(ctx, "globalThis.NS.__rpcSend({ n: 21 }).doubled === 42"));
    }
    SECTION("null response (notification) surfaces as JS null") {
        REQUIRE(eval_true(ctx, "globalThis.NS.__rpcSend({ notify: true }) === null"));
    }
    SECTION("calling with no args throws a TypeError") {
        REQUIRE(eval_true(ctx,
            "(() => { try { globalThis.NS.__rpcSend(); return false; }"
            "         catch (e) { return e instanceof TypeError; } })()"));
    }
}

TEST_CASE("host: two namespaces with distinct dispatch fns coexist", "[host][rpc]") {
    // Stresses the per-binding funcData pointer-boxing — each __rpcSend must
    // resolve to its own captured RpcSendFn.
    TjsHostRuntime host;
    REQUIRE(host.init());
    install_namespace(host, "A", doubler(2));
    install_namespace(host, "B", doubler(3));
    JSContext* ctx = host.context();

    REQUIRE(eval_true(ctx, "globalThis.A.__rpcSend({ n: 10 }).doubled === 20"));
    REQUIRE(eval_true(ctx, "globalThis.B.__rpcSend({ n: 10 }).doubled === 30"));
}
