#include "setup.hpp"


extern map<int, vector<int>> GRAPH;
extern map<int, int> node_socket;
extern int gui_socket;
extern map<int, pid_t> children;
extern uint64_t link_version;
extern volatile sig_atomic_t stop_requested;

void add_node(int nid) {
    if (nid < 0 || GRAPH.count(nid)) return;
    int master_socket[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, master_socket) < 0) {
        stop_requested = 1;
        return;
    }
    
    node_socket[nid] = master_socket[0];
    pid_t p = fork();
    if (p == 0) {
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        if (stop_requested) _exit(0);
        // close unwanted fds
        close(gui_socket);
        for (auto [id, fd] : node_socket) close(fd);
        

        // The control FD is independent of neighbors, including for isolated nodes.
        vector<string> args = {"./router", to_string(nid), to_string(master_socket[1])};

        vector<char*> router_argv;
        for (string& arg : args) router_argv.push_back(arg.data());
        router_argv.push_back(nullptr);
        execv("./router", router_argv.data());
        _exit(127);
    }
    close(master_socket[1]);
    if (p < 0) {
        close(master_socket[0]);
        node_socket.erase(nid);
        stop_requested = 1;
        return;
    }

    GRAPH.insert({nid, {}});
    children[nid] = p;
}

void add_edge(int u, int v) {
    if (u == v || !GRAPH.count(u) || !GRAPH.count(v)) return;
    if (find(GRAPH[u].begin(), GRAPH[u].end(), v) != GRAPH[u].end()) return;
    int ends[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, ends) < 0) {
        stop_requested = 1;
        return;
    }

    uint64_t version = ++link_version;
    bool sent = send_edge_fd(node_socket.at(u), v, ends[0], version)
             && send_edge_fd(node_socket.at(v), u, ends[1], version);

    close(ends[0]);
    close(ends[1]);

    if (!sent) {
        stop_requested = 1;
        return;
    }

    GRAPH[u].push_back(v);
    GRAPH[v].push_back(u);


    
}

void remove_edge(int u, int v) {
    if (u == v || !GRAPH.count(u) || !GRAPH.count(v)) return;
    auto position = find(GRAPH[u].begin(), GRAPH[u].end(), v);
    if (position == GRAPH[u].end()) return;

    uint64_t version = ++link_version;
    bool sent = send_edge_removed(node_socket.at(u), v, version)
             && send_edge_removed(node_socket.at(v), u, version);
    if (!sent) {
        stop_requested = 1;
        return;
    }
    GRAPH[u].erase(position);
    auto& reverse = GRAPH[v];
    reverse.erase(remove(reverse.begin(), reverse.end(), u), reverse.end());
}

void remove_node(int nid) {
    if (!GRAPH.count(nid)) return;
    // Copy because remove_edge changes the adjacency vectors.
    vector<int> neighbors = GRAPH[nid];
    for (int neighbor : neighbors) {
        remove_edge(nid, neighbor);
        if (stop_requested) return;
    }

    pid_t child = children.at(nid);
    kill(child, SIGTERM);
    while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
    children.erase(nid);
    close(node_socket.at(nid));
    node_socket.erase(nid);
    GRAPH.erase(nid);
}
