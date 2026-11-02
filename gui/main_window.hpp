#pragma once

#include <QMainWindow>

class QLabel;
class QPushButton;
class QPlainTextEdit;
class TopologyView;
class EngineSession;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void updateControls();
    TopologyView* topology_;
    EngineSession* engine_;
    QPlainTextEdit* tables_;
    QLabel* state_;
    QLabel* counts_;
    QPushButton* add_;
    QPushButton* removeNode_;
    QPushButton* addEdge_;
    QPushButton* remove_;
    QPushButton* start_;
    QPushButton* stop_;
};
