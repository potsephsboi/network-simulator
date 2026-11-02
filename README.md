# Network Simulator

A C++/Qt 6 GUI implementation of distributed path vector algorithm on user defined network topology.
## Build and run (Fedora)

```sh
sudo dnf install gcc-c++ make cmake qt6-qtbase-devel
make run
```

The executable is `build/network_simulator`. The build uses `gui/` and `engine/`; 

## Controls

- **add / remove node / edge:** build the network's topology.
- **simulation:** based on current topology calculate with distributed path vector algorithm, the routing table in each router. The topology can be edited "in real time" as the simulation runs. The tables will be recalculated.


## Architecture


The code is separated in 2 main parts, the gui and the simulation engine
`gui/` contains the two-screen Qt Widgets application. When simulation is started it does `fork()`/`execv()`, and creates a Unix socket pair for GUI–engine communication.

The engine code is separeted into a main process and into router processes which are created through the main process. the main process (setup.cpp) communicates with the gui and with the routers 

Each router runs as a separate process to simulate the distributed nature of the algorithm. Routers exchange routing information through sockets representing the links between them.

Edge and node failure detection is also handled without recalculating everything from the start. When a router link failure occurs, aka an edge is deleted, the main process informs the routers which the edge used to connect, and they recalculate the available paths, updating their routing tables. While this is done, the invalidation of the edge is "flooded" through the network, thus all routers calculate possible differences to their routing tables.



## Future work
The goal is to simulate BGP using a simplified model with one router per autonomous system. This includes adding routing policies that influence how routes are selected and advertised between autonomous systems.