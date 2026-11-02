#include "engine_session.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

void SocketReader::run() {
    QByteArray pending;
    char chunk[4096];
    while (true) {
        ssize_t n = recv(fd_, chunk, sizeof(chunk), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        pending.append(chunk, static_cast<int>(n));
        qsizetype end;
        while ((end = pending.indexOf('\0')) >= 0) {
            emit textReceived(QString::fromUtf8(pending.constData(), end));
            pending.remove(0, end + 1);
        }
    }
}

EngineSession::~EngineSession() { stop(); }

bool EngineSession::start(const QString& nodes, const QString& edges) {
    if (isRunning()) return false;
    QString directory = QCoreApplication::applicationDirPath() + "/engine";
    if (!QFileInfo(directory + "/setup").isExecutable() ||
        !QFileInfo(directory + "/router").isExecutable()) {
        emit problem("Engine executables are missing. Run make from the project directory.");
        return false;
    }

    int endpoints[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, endpoints) < 0) {
        emit problem(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    // Prepare everything before fork; the child only adjusts FDs/cwd and execs.
    std::string cwd = directory.toStdString();
    std::string executable = "./setup";
    std::string node_ids = nodes.toStdString();
    std::string socket = std::to_string(endpoints[1]);
    std::string graph = edges.toStdString();
    const char* directory_path = cwd.c_str();
    char* args[] = {executable.data(), node_ids.data(), socket.data(), graph.data(), nullptr};

    pid_t child = fork();
    if (child == 0) {
        setpgid(0, 0);
        close(endpoints[0]);
        fcntl(endpoints[1], F_SETFD, 0);
        if (chdir(directory_path) < 0) _exit(127);
        execv("./setup", args);
        _exit(127);
    }
    close(endpoints[1]);
    if (child < 0) {
        close(endpoints[0]);
        emit problem("Could not start the simulation process.");
        return false;
    }
    setpgid(child, child);
    pid_ = child;
    fd_ = endpoints[0];
    unsigned generation = ++generation_;
    reader_ = new SocketReader(fd_, this);
    connect(reader_, &SocketReader::textReceived, this,
        [this, generation](const QString& text) {
            if (generation == generation_) emit tablesReceived(text);
        }, Qt::QueuedConnection);
    connect(reader_, &QThread::finished, this, [this, generation] {
        if (generation != generation_ || !isRunning()) return;
        stop();
        emit problem("The engine connection closed.");
    }, Qt::QueuedConnection);
    emit runningChanged(true);
    reader_->start();
    return true;
}

void EngineSession::sendCommand(char command, int node, int neighbor) {
    if (!isRunning()) return;
    // One command byte, space-separated IDs, then a newline ending the message.
    QByteArray message(1, command);
    message += ' ';
    message += QByteArray::number(node);
    if (neighbor >= 0) {
        message += ' ';
        message += QByteArray::number(neighbor);
    }
    message += '\n';
    qsizetype sent = 0;
    while (sent < message.size()) {
        ssize_t n = send(fd_, message.constData() + sent, message.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            emit problem("Could not send the topology change to the engine.");
            return;
        }
        sent += n;
    }
}

void EngineSession::stop() {
    if (!isRunning()) return;
    ++generation_;
    pid_t child = pid_;
    pid_ = -1;

    // setup handles SIGTERM by stopping and reaping all its routers.
    kill(child, SIGTERM);
    shutdown(fd_, SHUT_RDWR);
    reader_->wait();
    delete reader_;
    reader_ = nullptr;
    close(fd_);
    fd_ = -1;

    QElapsedTimer timer;
    timer.start();
    pid_t result = 0;
    while (timer.elapsed() < 1000) {
        result = waitpid(child, nullptr, WNOHANG);
        if (result == child || (result < 0 && errno == ECHILD)) break;
        usleep(10000);
    }
    if (result == 0 || (result < 0 && errno != ECHILD)) {
        kill(-child, SIGKILL);
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
    }
    emit runningChanged(false);
}
