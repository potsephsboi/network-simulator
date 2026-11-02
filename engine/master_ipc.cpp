#include "setup.hpp"

#include <sys/socket.h>
#include <sys/uio.h>
#include <cstring> 



extern map<int, int> node_socket;
extern int gui_socket;
extern volatile sig_atomic_t stop_requested;

bool send_text(int fd, const string& text) {
    size_t sent = 0;
    while (sent < text.size() && !stop_requested) {
        ssize_t n = send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += n;
    }
    return sent == text.size();
}

// used to share a socket to the endpoint processes of the new edge
bool send_edge_fd(int master_fd, int neighbor, int edge_fd, uint64_t version) {
    string payload = "E";
    payload += to_string(neighbor);
    payload += ' ';
    payload += to_string(version);
    payload += '\0';

    if (!send_text(master_fd, payload)) return false;

    // The header was already sent. Only this byte carries the descriptor.
    char dummy = 'F';
    iovec data{&dummy, 1};

    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};

    msghdr message{};
    message.msg_iov = &data;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);

    cmsghdr* header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(header), &edge_fd, sizeof(edge_fd));

    ssize_t n;
    do {
        n = sendmsg(master_fd, &message, MSG_NOSIGNAL);
    } while (n < 0 && errno == EINTR && !stop_requested);
    return n == 1;
}

bool send_edge_removed(int master_fd, int neighbor, uint64_t version) {
    string payload = "D" + to_string(neighbor) + ' ' + to_string(version);
    payload += '\0';
    return send_text(master_fd, payload);
}

int consume_int() {
    string number;
    char c;
    while (!stop_requested) {
        ssize_t n = recv(gui_socket, &c, 1, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            stop_requested = 1;
            return -1;
        }
        if (isspace(static_cast<unsigned char>(c))) {
            if (!number.empty()) return stoi(number);
        } else {
            number += c;
        }
    }
    return -1;
}


string collect_tables() {
    ostringstream output;
    for (auto [id, fd] : node_socket) {
        if (stop_requested || !send_text(fd, "S")) {
            stop_requested = 1;
            break;
        }

        string reply;
        char c;
        while (!stop_requested) {
            ssize_t n = recv(fd, &c, 1, 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                stop_requested = 1;
                break;
            }
            if (c == '\0') break;
            reply += c;
        }

        output << "ROUTER " << id << " TABLE:\n"
               << "TARGET    |    PATH\n"
               << "----------+------------------------\n";

        istringstream table(reply);
        string row;
        while (getline(table, row)) {
            istringstream nodes(row);
            int target = id, node;
            string path;
            while (nodes >> node) {
                target = node;
                if (!path.empty()) path += " -> ";
                path += to_string(node);
            }
            output << left << setw(10) << target << "|    "
                   << (path.empty() ? "(local)" : path) << '\n';
        }
        output << '\n';
    }
    return output.str();
}
