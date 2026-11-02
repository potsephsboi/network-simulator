add_executable(setup setup.cpp
    graph_events.cpp
    master_ipc.cpp)
add_executable(router router.cpp router_ipc.cpp)
set_target_properties(setup router PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/engine")
