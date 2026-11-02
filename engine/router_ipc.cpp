#include "router.hpp"
#include <unistd.h>

extern map<int, vector<int>> routes;
extern map<int, int> neighbors_sockets;
extern map<int, uint64_t> neighbor_versions;
extern int router_id;
extern int master_socket;
extern bool routes_dirty;

const int RECV_BUF_SIZE = 128;
map<int, string> buffers;
static bool watched_dirty = false;

static bool send_message(int fd, const string& text) {
    size_t sent = 0;
    while (sent < text.size()) {
        ssize_t n = send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

void disconnect_neighbor(int neighbor) {
    auto found = neighbors_sockets.find(neighbor);
    if (found == neighbors_sockets.end()) return;
    buffers.erase(found->second);
    close(found->second);
    neighbors_sockets.erase(found);
    neighbor_versions.erase(neighbor);
    watched_dirty = routes_dirty = true;
}

void inform(map<int, int>& neighbors, vector<int>& path) {
    for (auto it = neighbors.begin(); it != neighbors.end();) {
        auto [neighbor, fd] = *it++;
        // The peer may already have closed its end while D is still in flight. in this case send_path fails
        if (!send_path(fd, path)) disconnect_neighbor(neighbor);
    }
}

bool send_path(int fd, const vector<int>& path) {
    string text;
    for (int node : path) text += to_string(node) + ' ';
    text += '\0';
    return send_message(fd, text);
}

bool send_link_change(int fd, char command, int u, int v, uint64_t version) {
    string text(1, command);
    text += ' ' + to_string(min(u, v)) + ' ' + to_string(max(u, v));
    text += ' ' + to_string(version);
    text += '\0';
    return send_message(fd, text);
}

static string read_control_header() {
    string header;
    while (true) {
        char c;
        ssize_t n = recv(master_socket, &c, 1, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n == 0) _exit(0);
        if (n < 0) throw runtime_error("Failed to receive control header");
        if (c == '\0') return header;
        header += c;
    }
}

vector<int> receive_path(vector<pollfd>& watched) {
    while (true) {
        while (routes_dirty) {
            routes_dirty = false;
            revalidate_routes();
        }
        if (watched_dirty) {
            watched.clear();
            for (auto [neighbor, fd] : neighbors_sockets) watched.push_back({fd, POLLIN, 0});
            watched.push_back({master_socket, POLLIN, 0});
            watched_dirty = false;
        }

        // Consume complete frames already buffered before waiting again.
        bool handled_link_change = false;
        for (auto& p : watched) {
            if (p.fd < 0 || p.fd == master_socket) continue;
            string& buffer = buffers[p.fd];
            size_t end = buffer.find('\0');
            if (end == string::npos) continue;
            string frame = buffer.substr(0, end);
            buffer.erase(0, end + 1);
            istringstream input(frame);
            if (!frame.empty() && (frame[0] == 'D' || frame[0] == 'U')) {
                char command;
                int u, v;
                uint64_t version;
                if (input >> command >> u >> v >> version)
                    handle_link_change(command, u, v, version);
                // A D can close a watched FD; refresh outside this range-for.
                handled_link_change = true;
                break;
            }
            vector<int> path;
            int id;
            while (input >> id) path.push_back(id);
            return path;
        }
        if (handled_link_change) continue;

        if (poll(watched.data(), watched.size(), -1) < 0) {
            if (errno == EINTR) continue;
            throw system_error(errno, generic_category(), "poll");
        }
        for (auto& p : watched) {
            if (!(p.revents & (POLLIN | POLLHUP | POLLERR))) continue;
            if (p.fd == master_socket) {
                char command;
                ssize_t n = recv(master_socket, &command, 1, MSG_DONTWAIT);
                if (n < 0) continue;
                if (n == 0) _exit(0);
                switch (command) {
                    case 'S':
                        dump_table();
                        break;
                    case 'E': {
                        istringstream input(read_control_header());
                        int neighbor;
                        uint64_t version = 0;
                        if (!(input >> neighbor)) throw runtime_error("Missing neighbor ID");
                        input >> version;
                        int fd = receive_edge_fd();
                        if (neighbors_sockets.count(neighbor) && neighbor_versions[neighbor] >= version) {
                            close(fd);
                            break;
                        }
                        // Replace an old incarnation, even if U arrived before the local D.
                        disconnect_neighbor(neighbor);
                        neighbors_sockets[neighbor] = fd;
                        neighbor_versions[neighbor] = version;
                        buffers.erase(fd);
                        watched_dirty = routes_dirty = true;
                        handle_link_change('U', router_id, neighbor, version);
                        if (!neighbors_sockets.count(neighbor) || neighbors_sockets.at(neighbor) != fd) break;
                        // Synchronize failure/restoration history before exchanging routes.
                        if (!send_link_states(fd)) {
                            disconnect_neighbor(neighbor);
                            break;
                        }
                        for (const auto& [target, path] : routes) {
                            if (!validate_path(path)) continue;
                            vector<int> announcement = path;
                            announcement.insert(announcement.begin(), router_id);
                            if (!send_path(fd, announcement)) {
                                disconnect_neighbor(neighbor);
                                break;
                            }
                        }
                        break;
                    }
                    case 'D': {
                        istringstream input(read_control_header());
                        int neighbor;
                        uint64_t version;
                        if (!(input >> neighbor >> version)) throw runtime_error("Missing deletion data");
                        handle_link_change('D', router_id, neighbor, version);
                        break;
                    }
                }
            } else {
                char chunk[RECV_BUF_SIZE];
                ssize_t n = recv(p.fd, chunk, sizeof(chunk), MSG_DONTWAIT);
                if (n < 0) continue;
                if (n == 0) {
                    int neighbor = -1;
                    for (auto [id, fd] : neighbors_sockets)
                        if (fd == p.fd) { neighbor = id; break; }
                    if (neighbor >= 0) disconnect_neighbor(neighbor);
                    p.fd = -1;
                    continue;
                }
                buffers[p.fd].append(chunk, n);
            }
        }
    }
}

int receive_edge_fd() {
    char dummy;
    iovec data{&dummy, 1};
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};
    msghdr message{};
    message.msg_iov = &data;
    message.msg_iovlen = 1;
    message.msg_control = control;
    ssize_t n;
    do {
        message.msg_controllen = sizeof(control);
        n = recvmsg(master_socket, &message, 0);
    } while (n < 0 && errno == EINTR);
    if (n == 0) _exit(0);
    if (n != 1 || (message.msg_flags & MSG_CTRUNC))
        throw runtime_error("Failed to receive edge descriptor");
    cmsghdr* header = CMSG_FIRSTHDR(&message);
    if (!header || header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len != CMSG_LEN(sizeof(int)))
        throw runtime_error("Missing edge descriptor");
    int fd;
    memcpy(&fd, CMSG_DATA(header), sizeof(fd));
    return fd;
}

void dump_table() {
    string text;
    for (const auto& [target, path] : routes) {
        for (int node : path) text += to_string(node) + ' ';
        text += '\n';
    }
    text += '\0';
    if (!send_message(master_socket, text)) _exit(0);
}
