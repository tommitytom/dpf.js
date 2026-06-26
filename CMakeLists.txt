# dpf.js — generic DPF + LVGL + txiki/QuickJS + rpcpp framework.
#
# Consumed by a plugin via `add_subdirectory(<this dir>)` (resolved with
# `require.resolve('dpf.js/package.json')`), then `target_link_libraries(<tgt>
# PUBLIC dpfjs::core)`. Also builds standalone (-DDPFJS_BUILD_EXAMPLE=ON) to
# verify self-containment.

cmake_minimum_required(VERSION 3.19...3.31)  # 3.19 for string(JSON ...)

# --- Version: single-sourced from package.json, stamped into a generated
# header. A drift between package.json and any hardcoded version is impossible
# because there is only one source.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/package.json" _DPFJS_PKG_JSON)
string(JSON _DPFJS_VERSION GET "${_DPFJS_PKG_JSON}" version)

project(dpfjs VERSION "${_DPFJS_VERSION}" LANGUAGES C CXX)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf_js_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/dpf_js_version.h"
    @ONLY)

# tell LVGL where to find its config file. CACHE without FORCE: a consumer that
# sets LV_BUILD_CONF_PATH before add_subdirectory(dpf.js) wins (e.g. RetroPlug
# points it at its own src/lv_conf.h); standalone builds fall back to ours.
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "")

# --- DPF (DISTRHO Plugin Framework): defines dpf_add_plugin() globally, so the
# consumer can call it after add_subdirectory(dpf.js).
add_subdirectory(deps/dpf)

# Use LVGL from lv_binding_js (single source of truth)
add_subdirectory(deps/lv_binding_js/deps/lvgl)

# --- txiki.js (QuickJS + libuv + libwebsockets + ffi runtime) ---
set(BUILD_WITH_MIMALLOC OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/lv_binding_js/deps/txiki EXCLUDE_FROM_ALL)

# --- rpcpp: header-only JSON-RPC 2.0 server, used by JsRpcBridge ---
# The bridge marshals C++ <-> JSValue in-process via rpcpp's QuickJS codec, so
# rpcpp needs quickjs.h. txiki already defines the `qjs` target above (the
# add_subdirectory(...txiki) on line ~37), so rpcpp's
# `if(RPCPP_BUILD_QUICKJS AND NOT TARGET qjs)` guard skips the CPM fetch and
# links that existing engine instead of pulling its own.
set(RPCPP_BUILD_QUICKJS ON CACHE BOOL "" FORCE)
add_subdirectory(deps/rpcpp)

# --- lvgl-js-native: the native C++ component bridge (lv_binding_js wrappers +
# the dpf.js LVGL+JS engine + the generic embedded txiki host).
set(LVGL_JS_NATIVE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src/render/native)
add_library(lvgl-js-native STATIC
    ${LVGL_JS_NATIVE_DIR}/core/basic/comp.cpp
    ${LVGL_JS_NATIVE_DIR}/core/style/style.cpp
    ${LVGL_JS_NATIVE_DIR}/core/style/font/font.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/event.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/click/click.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/normal/normal.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/select/select.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/value_change/value_change.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/key/key.cpp
    ${LVGL_JS_NATIVE_DIR}/core/refresh/refresh.cpp
    ${LVGL_JS_NATIVE_DIR}/core/utils/utils.cpp
    ${LVGL_JS_NATIVE_DIR}/core/theme/theme.cpp
    ${LVGL_JS_NATIVE_DIR}/core/dimensions/dimensions.cpp
    ${LVGL_JS_NATIVE_DIR}/core/animate/animate.cpp
    ${LVGL_JS_NATIVE_DIR}/core/group/group.cpp
    ${LVGL_JS_NATIVE_DIR}/core/img/img.cpp
    ${LVGL_JS_NATIVE_DIR}/core/img/png/png.cpp
    ${LVGL_JS_NATIVE_DIR}/core/img/png/lodepng.cpp
    ${LVGL_JS_NATIVE_DIR}/core/img/gif/gif.cpp
    ${LVGL_JS_NATIVE_DIR}/components/component.cpp
    ${LVGL_JS_NATIVE_DIR}/components/view/view.cpp
    ${LVGL_JS_NATIVE_DIR}/components/view/view_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/text/text.cpp
    ${LVGL_JS_NATIVE_DIR}/components/text/text_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/button/button.cpp
    ${LVGL_JS_NATIVE_DIR}/components/button/button_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/image/image.cpp
    ${LVGL_JS_NATIVE_DIR}/components/image/image_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/canvas/canvas.cpp
    ${LVGL_JS_NATIVE_DIR}/components/canvas/canvas_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/slider/slider.cpp
    ${LVGL_JS_NATIVE_DIR}/components/slider/slider_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/switch/switch.cpp
    ${LVGL_JS_NATIVE_DIR}/components/switch/switch_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/arc/arc.cpp
    ${LVGL_JS_NATIVE_DIR}/components/arc/arc_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/checkbox/checkbox.cpp
    ${LVGL_JS_NATIVE_DIR}/components/checkbox/checkbox_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/textarea/textarea.cpp
    ${LVGL_JS_NATIVE_DIR}/components/textarea/textarea_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/keyboard/keyboard.cpp
    ${LVGL_JS_NATIVE_DIR}/components/keyboard/keyboard_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/dropdownlist/dropdownlist.cpp
    ${LVGL_JS_NATIVE_DIR}/components/dropdownlist/dropdownlist_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/progressbar/progressbar.cpp
    ${LVGL_JS_NATIVE_DIR}/components/progressbar/progressbar_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/roller/roller.cpp
    ${LVGL_JS_NATIVE_DIR}/components/roller/roller_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/line/line.cpp
    ${LVGL_JS_NATIVE_DIR}/components/line/line_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/calendar/calendar.cpp
    ${LVGL_JS_NATIVE_DIR}/components/calendar/calendar_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/chart/chart.cpp
    ${LVGL_JS_NATIVE_DIR}/components/chart/chart_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/tabview/tabview.cpp
    ${LVGL_JS_NATIVE_DIR}/components/tabview/tabview_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/gif/gif.cpp
    ${LVGL_JS_NATIVE_DIR}/components/gif/gif_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/mask/mask.cpp
    ${LVGL_JS_NATIVE_DIR}/components/mask/mask_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/window/window.cpp
    ${LVGL_JS_NATIVE_DIR}/bootstrap/render_bootstrap.cpp
    # the dpf.js LVGL+JS engine wrapper (replaces lv_binding_js's engine.cpp) +
    # the generic embedded txiki host (shared with consumers' CLIs).
    src/dpfjs/LvglJsEngine.cpp
    src/dpfjs/host/TjsHostRuntime.cpp
)

target_include_directories(lvgl-js-native PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src                            # for "dpfjs/..." includes
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src/render  # for "native/..." includes
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src/engine  # for "hal/hal.hpp" etc
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src         # top-level src includes
)
target_link_libraries(lvgl-js-native PUBLIC tjs lvgl::lvgl)
set_target_properties(lvgl-js-native PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    # C++20: the shared host (src/dpfjs/host/TjsHostRuntime) uses std::optional /
    # std::string_view, and the lv_binding_js component wrappers use designated
    # initializers. Clang/GCC accept the latter under 17 as an extension, but
    # MSVC requires 20 — so pin 20 for cross-compiler (Windows) parity.
    CXX_STANDARD 20
)

# Windows: <windows.h> (pulled in via libuv → txiki's private.h) #defines GDI
# names like Arc / Rectangle, which collide with lv_binding_js component classes
# (class Arc, …) and desync the MSVC parser. NOGDI drops wingdi.h;
# WIN32_LEAN_AND_MEAN trims winsock1 etc. PUBLIC so the targets that include
# these component headers (the plugin UI) inherit them. Scoped to this target —
# NOT global — because WIN32_LEAN_AND_MEAN also drops winioctl.h, which wamr's
# win_file.c needs. Matches the proven lvgl-template-plugin build.
if(WIN32)
    # PRIVATE, not PUBLIC: NOGDI drops wingdi.h, but DPF's UI code (the file-browser
    # common dialog, DistrhoUIMain) needs GDI types (LOGFONT in commdlg.h). The Arc
    # collision is only in lv_binding_js's own component sources compiled here, so
    # scope NOGDI to this target and don't leak it to consumers.
    target_compile_definitions(lvgl-js-native PRIVATE NOGDI WIN32_LEAN_AND_MEAN)
endif()

# Ensure all static libs that end up in shared plugin binaries have PIC.
set_target_properties(lvgl PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(tjs PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(qjs PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(uv_a PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(vmlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(websockets PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(mbedtls PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(mbedx509 PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(mbedcrypto PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(ada PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(reflectcpp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- dpfjs::core: the single INTERFACE target a consumer links. Carries the
# framework's PUBLIC include dirs + link libraries; the generated version header
# lives in the binary dir.
add_library(dpfjs-core INTERFACE)
add_library(dpfjs::core ALIAS dpfjs-core)
target_include_directories(dpfjs-core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src/render
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/deps/txiki/deps/miniz
    ${CMAKE_CURRENT_BINARY_DIR}            # dpf_js_version.h
)
target_link_libraries(dpfjs-core INTERFACE
    lvgl-js-native lvgl::lvgl tjs rpcpp miniz
)

# --- Optional compile-only seam probe: proves src/dpfjs builds against a
# trivial consumer with zero product headers.
option(DPFJS_BUILD_EXAMPLE "Compile the dpf.js seam probe (no linking)" OFF)
if(DPFJS_BUILD_EXAMPLE)
    add_library(dpfjs-minimal-probe OBJECT examples/minimal/MinimalProbe.cpp)
    target_include_directories(dpfjs-minimal-probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/minimal)
    target_link_libraries(dpfjs-minimal-probe PRIVATE lvgl-js-native rpcpp)
    set_target_properties(dpfjs-minimal-probe PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON)
endif()

# --- Tests: runtime tests for the txiki host + __rpcSend trampoline. These run
# against TjsHostRuntime alone (links only `tjs`, no LVGL), so they stay light.
option(DPFJS_BUILD_TESTS "Build dpf.js tests" OFF)
if(DPFJS_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.9.1)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    add_subdirectory(tests)
endif()
