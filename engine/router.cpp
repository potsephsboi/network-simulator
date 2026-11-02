#include "router.hpp"

using namespace std;

int router_id;
int master_socket;
map<int, int> neighbors_sockets;
map<int, uint64_t> neighbor_versions; // generation of each physical socket
map<int, vector<int>> routes;
bool routes_dirty = false;


// custom hash for edges
using Edge = pair<int, int>;
struct EdgeHash {
    size_t operator()(const Edge& edge) const {
        uint64_t key = (uint64_t(uint32_t(edge.first)) << 32) | uint32_t(edge.second);
        return hash<uint64_t>{}(key);
    }
};
struct LinkState {
    uint64_t version;
    bool up;
};

unordered_set<Edge, EdgeHash> invalid_edges;
unordered_map<Edge, LinkState, EdgeHash> link_states;

// metric for choosing next available path to a target
// shortest path first, IDs break ties deterministically
struct PathLess {
    bool operator()(const vector<int>& a, const vector<int>& b) const {
        if (a.size() != b.size()) return a.size() < b.size();
        return a < b;
    }
};

map<int, set<vector<int>, PathLess>> fallback;

static Edge edge_key(int u, int v) {
    return {min(u, v), max(u, v)};
}

bool validate_path(const vector<int>& path) {
    if (path.empty()) return true; // local route
    if (!neighbors_sockets.count(path.front())) return false;
    unordered_set<int> seen{router_id};
    int previous = router_id;
    for (int node : path) {
        if (node < 0 || !seen.insert(node).second) return false;
        // Include router_id -> first hop, which is absent from the stored path.
        if (invalid_edges.count(edge_key(previous, node))) return false;
        previous = node;
    }
    return true;
}

static void cache_path(const vector<int>& path) {
    if (path.empty() || !validate_path(path)) return;
    int target = path.back();
    fallback[target].insert(path);
}

static bool better_path(const vector<int>& a, const vector<int>& b) {
    return a.size() < b.size() || (a.size() == b.size() && a < b);
}

static void select_best(int target) {
    if (target == router_id) return;
    auto existing = routes.find(target);
    bool had_route = existing != routes.end();
    vector<int> old_path = had_route ? existing->second : vector<int>{};
    if (had_route && !validate_path(old_path)) routes.erase(existing);

    auto& paths = fallback[target];
    while (!paths.empty()) {
        auto best = paths.begin();
        bool same_as_selected = routes.count(target) && *best == routes.at(target);
        if (validate_path(*best) && !same_as_selected) break;
        paths.erase(best);
    }
    if (!paths.empty() && (!routes.count(target) || better_path(*paths.begin(), routes.at(target)))) {
        vector<int> best = *paths.begin();
        paths.erase(paths.begin());
        if (routes.count(target)) cache_path(routes.at(target));
        routes[target] = std::move(best);
    }
    auto selected = routes.find(target);
    if (selected != routes.end() && (!had_route || selected->second != old_path)) {
        vector<int> announcement = selected->second;
        announcement.insert(announcement.begin(), router_id);
        inform(neighbors_sockets, announcement);
    }
    // If no fallback exists, the D flood invalidates downstream copies too.
}

void consider_path(const vector<int>& path) {
    if (path.empty() || !validate_path(path)) return;
    int target = path.back();
    if (routes.count(target) && routes.at(target) == path) return;
    cache_path(path);
    select_best(target);
}

void revalidate_routes() {
    set<int> targets;
    for (const auto& [target, path] : routes) targets.insert(target);
    for (const auto& [target, paths] : fallback) targets.insert(target);
    for (int target : targets) select_best(target);
}

static void close_older_socket(const Edge& edge, uint64_t down_version) {
    int neighbor = router_id == edge.first ? edge.second :
                   router_id == edge.second ? edge.first : -1;
    if (neighbors_sockets.count(neighbor) && neighbor_versions[neighbor] < down_version)
        disconnect_neighbor(neighbor);
}

void handle_link_change(char command, int u, int v, uint64_t version) {

    Edge edge = edge_key(u, v);
    // A delayed local D must close an old socket, but never a restored one.
    if (command == 'D') close_older_socket(edge, version);

    auto previous = link_states.find(edge);
    if (previous != link_states.end() && previous->second.version >= version) {
        if (!previous->second.up) close_older_socket(edge, previous->second.version);
        return;
    }
    bool up = command == 'U';
    link_states[edge] = {version, up}; // mark seen before flooding
    if (up) invalid_edges.erase(edge);
    else invalid_edges.insert(edge);
    routes_dirty = true;

    for (auto it = neighbors_sockets.begin(); it != neighbors_sockets.end();) {
        auto [neighbor, fd] = *it++;
        if (!send_link_change(fd, command, edge.first, edge.second, version))
            disconnect_neighbor(neighbor);
    }
}

bool send_link_states(int fd) {
    for (const auto& [edge, state] : link_states)
        if (!send_link_change(fd, state.up ? 'U' : 'D', edge.first, edge.second, state.version))
            return false;
    return true;
}

int main(int argc, char* argv[]) {
    
    
    router_id = stoi(argv[1]);
    master_socket = stoi(argv[2]);
    for (int i = 3; i < argc; i += 2) {
        int neighbor = stoi(argv[i]);
        neighbors_sockets[neighbor] = stoi(argv[i + 1]);
        neighbor_versions[neighbor] = 0;
    }
    routes[router_id] = {};
    vector<int> self{router_id};
    inform(neighbors_sockets, self);

    vector<pollfd> watched;
    for (auto [neighbor, fd] : neighbors_sockets) watched.push_back({fd, POLLIN, 0});
    watched.push_back({master_socket, POLLIN, 0});
    while (true) consider_path(receive_path(watched));
}
