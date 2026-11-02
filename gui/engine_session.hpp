#pragma once

#include <QObject>
#include <QThread>
#include <sys/types.h>

class SocketReader : public QThread {
    Q_OBJECT
public:
    explicit SocketReader(int fd, QObject* parent = nullptr) : QThread(parent), fd_(fd) {}
signals:
    void textReceived(const QString& text);
protected:
    void run() override;
private:
    int fd_;
};

class EngineSession : public QObject {
    Q_OBJECT
public:
    explicit EngineSession(QObject* parent = nullptr) : QObject(parent) {}
    ~EngineSession() override;
    bool start(const QString& nodes, const QString& edges);
    void sendCommand(char command, int node, int neighbor = -1);
    void stop();
    bool isRunning() const { return pid_ > 0; }
signals:
    void tablesReceived(const QString& text);
    void runningChanged(bool running);
    void problem(const QString& message);
private:
    pid_t pid_ = -1;
    int fd_ = -1;
    unsigned generation_ = 0;
    SocketReader* reader_ = nullptr;
};
