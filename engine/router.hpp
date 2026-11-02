#pragma once

#include <bits/stdc++.h>
#include <iostream>
#include <poll.h>
#include <cerrno>
#include <sys/socket.h>
#include <sys/uio.h>

using namespace std;

void inform(map<int,int>&,  vector<int>& path);
vector<int> receive_path(vector<pollfd>& watched);
void dump_table();
bool send_path(int fd, const vector<int>& path);
int receive_edge_fd();
bool send_link_change(int fd, char command, int u, int v, uint64_t version);
void disconnect_neighbor(int neighbor);
bool validate_path(const vector<int>& path);
void consider_path(const vector<int>& path);
void revalidate_routes();
void handle_link_change(char command, int u, int v, uint64_t version);
bool send_link_states(int fd);
