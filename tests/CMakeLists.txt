include(Catch)

# test_host exercises the txiki host runtime + the __rpcSend trampoline in
# isolation. It compiles TjsHostRuntime.cpp directly and links only `tjs`
# (no LVGL, no lvgl-js-native, no rpcpp), so the binary stays small and fast.
add_executable(test_host
    test_host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/dpfjs/host/TjsHostRuntime.cpp)
target_include_directories(test_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(test_host PRIVATE tjs Catch2::Catch2WithMain)
set_target_properties(test_host PROPERTIES
    # The host is C++17 (std::optional / std::string_view, no rpcpp).
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
catch_discover_tests(test_host)
