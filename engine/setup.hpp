#pragma once
#include <sys/socket.h>
#include <bits/stdc++.h>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>

using namespace std;

void build_graph(map<int, vector<int>>& g, const string& node_ids, const string& edges);

bool send_text(int, const string&);
string collect_tables();

bool send_edge_fd(int, int, int, uint64_t version);
bool send_edge_removed(int master_fd, int neighbor, uint64_t version);
int consume_int();

void add_node(int);
void add_edge(int, int);
void remove_edge(int, int);
void remove_node(int);
