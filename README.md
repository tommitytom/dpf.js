# dpf.js

The generic **DPF + LVGL + txiki/QuickJS + rpcpp** framework slice extracted
from RetroPlug2 — the reusable "embed a JS engine in a DPF plugin, drive an
LVGL UI from React/TypeScript, and call native over an in-process RPC bridge"
layer, with zero product-specific code.

It is consumed as an npm **source** package (not a prebuilt ABI): a consumer
resolves the package directory and pulls it into its own CMake build via
`add_subdirectory`, so the C++ is compiled in the consumer's toolchain and the
JS bundle is byte-compiled per-binary.

```cmake
execute_process(COMMAND node -e "console.log(require.resolve('dpf.js/package.json'))"
                OUTPUT_VARIABLE DPFJS_PKG OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(DPFJS_PATH "${DPFJS_PKG}" DIRECTORY)
add_subdirectory("${DPFJS_PATH}" dpfjs)
# ... then: target_link_libraries(my_plugin PUBLIC dpfjs::core)
```

## What's here

- `src/dpfjs/` — the generic C++: `LvglJsEngine` (LVGL+JS engine), the embedded
  txiki host (`host/TjsHostRuntime`), the `JsRpcBridge<Service>` template, and
  the DPF-free `PluginDescriptor` / `Env` helpers.
- `runtime/lvgljs/` — the generic TypeScript front door to the native bridge
  (parameter API, input handling, React hook glue).
- `deps/` — the framework's own submodules: DPF, DPF-Widgets, lv_binding_js
  (LVGL + txiki/QuickJS), rpcpp (reflect-cpp), msgpack-c, efsw.
- `examples/minimal/` — a compile-only seam probe that instantiates the
  framework against a trivial service with no consumer coupling.

## Architecture

The architecture is inverted from a normal Node stack: **C++ hosts the JS
engine, and TypeScript is the guest.** A consumer binary embeds txiki/QuickJS,
exposes a native rpcpp service, and runs a TS bundle that calls native through
an in-process `__rpcSend` FFI shim. See the upstream RetroPlug `dpfjs.md` for
the full framework walkthrough (architecture, parameter sync, JS API, hot
reload, validation) until those docs migrate here.

## Building standalone

```sh
cmake -B build -DDPFJS_BUILD_EXAMPLE=ON
cmake --build build -j$(nproc)   # compiles lvgl-js-native + tjs + the seam probe
```

The version is single-sourced from `package.json` and stamped into
`dpf_js_version.h` at configure time; a mismatch is a build error.
