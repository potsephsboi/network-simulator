#include "setup.hpp"

using namespace std;

// argv: setup "node-id node-id ..." <gui-socket-fd> "u v\nu v\n..."
map<int, vector<int>> GRAPH;
map<int, int> node_socket;
map<int, pid_t> children; // router ID -> PID
uint64_t link_version = 0;

int gui_socket;

volatile sig_atomic_t stop_requested = 0;

void request_stop(int) {
    stop_requested = 1;
}

void stop_routers(const map<int, pid_t>& children) {
    for (auto [id, child] : children) kill(child, SIGTERM);
    for (auto [id, child] : children)
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
    for (auto [id, fd] : node_socket) close(fd);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "usage: ./setup \"node-ids\" gui-socket-fd \"edge-list\"\n";
        return 1;
    }
    gui_socket = stoi(argv[2]);
    build_graph(GRAPH, argv[1], argv[3]);

    struct sigaction action{};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);

    map<int, map<int, int>> socket_map;
    for (const auto& [nid, neighbors] : GRAPH) {
        for (int neigh : neighbors) {
            if (neigh > nid) {
                int ends[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == -1) return 1;
                socket_map[nid][neigh] = ends[0];
                socket_map[neigh][nid] = ends[1];
            }
        }
    }

    for (const auto& [nid, neighbors] : GRAPH) {
        if (stop_requested) break;
        int master_socket[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, master_socket) == -1) {
            stop_requested = 1;
            break;
        }
        node_socket[nid] = master_socket[0]; // parent uses master_socket[0]
        pid_t p = fork();
        if (p == 0) {
            // Do not inherit the master's graceful-stop handler across the fork/exec gap.
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            if (stop_requested) _exit(0);
            // Keep only this router's control socket and neighbor sockets.
            close(gui_socket);
            for (auto [id, fd] : node_socket) close(fd);
            for (const auto& [other, sockets] : socket_map) {
                if (other == nid) continue;
                for (auto [neighbor, fd] : sockets) close(fd);
            }

            // The control FD is independent of neighbors, including for isolated nodes.
            vector<string> args = {"./router", to_string(nid), to_string(master_socket[1])};
            for (int neigh : neighbors) {
                args.push_back(to_string(neigh));
                args.push_back(to_string(socket_map[nid][neigh]));
            }
            vector<char*> router_argv;
            for (string& arg : args) router_argv.push_back(arg.data());
            router_argv.push_back(nullptr);
            execv("./router", router_argv.data());
            _exit(127);
        }
        close(master_socket[1]);
        if (p < 0) {
            stop_requested = 1;
            break;
        }
        children[nid] = p;
    }

    for (const auto& [id, sockets] : socket_map)
        for (auto [neighbor, fd] : sockets) close(fd);

    string previous;
    bool first = true;
    while (!stop_requested) {
        pollfd control{gui_socket, POLLIN, 0};
        int ready = poll(&control, 1, first ? 0 : 500);
        if (ready < 0) continue;
        if (control.revents & (POLLHUP | POLLERR | POLLNVAL)) break;
        if (control.revents & POLLIN) {
            char command;
            if (recv(gui_socket, &command, 1, 0) <= 0) break;
            switch (command) {
                case 'N': { // new Node
                    int nid = consume_int();
                    if (!stop_requested) add_node(nid);
                    break;
                }
                case 'E': { // new Edge; consume endpoints in wire order
                    int u = consume_int();
                    int v = consume_int();
                    if (!stop_requested) add_edge(u, v);
                    break;
                }
                case 'K': { // Kill router
                    int nid = consume_int();
                    if (!stop_requested) remove_node(nid);
                    break;
                }
                case 'D': { // Delete edge
                    int u = consume_int();
                    int v = consume_int();
                    if (!stop_requested) remove_edge(u, v);
                    break;
                }
                default: break;
            }
        }
        if (stop_requested) break;

        string text = collect_tables();
        if (stop_requested) break;
        if (first || text != previous) {
            string message = text;
            message += '\0';
            if (!send_text(gui_socket, message)) break;
            previous = text;
        }
        first = false;
    }

    stop_routers(children);
    close(gui_socket);
}

void build_graph(map<int, vector<int>>& g, const string& node_ids, const string& edges) {
    g.clear();
    istringstream ids(node_ids);
    int id;
    while (ids >> id)
        if (id >= 0) g.try_emplace(id);
    istringstream lines(edges);
    string line;
    while (getline(lines, line)) {
        istringstream edge(line);
        int u, v;
        if (!(edge >> u >> v) || !g.count(u) || !g.count(v) || u == v)
            continue;
        if (find(g[u].begin(), g[u].end(), v) != g[u].end()) continue;
        g[u].push_back(v);
        g[v].push_back(u);
    }
}


