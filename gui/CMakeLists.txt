find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

add_executable(network_simulator
    main.cpp
    main_window.cpp main_window.hpp
    topology_view.cpp topology_view.hpp
    engine_session.cpp engine_session.hpp)
target_link_libraries(network_simulator PRIVATE Qt6::Widgets)
set_target_properties(network_simulator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_dependencies(network_simulator setup router)
