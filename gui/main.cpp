#include "main_window.hpp"

#include <QApplication>
#include <QFont>
#include <QPalette>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setApplicationName("Network Simulator");
    QFont font = app.font();
    font.setPixelSize(14);
    app.setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#181d27"));
    palette.setColor(QPalette::WindowText, QColor("#e5eaf4"));
    palette.setColor(QPalette::Base, QColor("#11151d"));
    palette.setColor(QPalette::Text, QColor("#e5eaf4"));
    palette.setColor(QPalette::Button, QColor("#252e3e"));
    palette.setColor(QPalette::ButtonText, QColor("#e5eaf4"));
    palette.setColor(QPalette::Highlight, QColor("#496bb5"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(palette);
    app.setStyleSheet(R"(
        QMainWindow { background: #181d27; }
        QLabel#title { color: #f1f4fb; font-size: 24px; font-weight: 600; }
        QLabel#muted { color: #97a4ba; font-size: 13px; }
        QLabel#stateBadge { background: #252d3b; color: #aebbd0; border-radius: 14px; padding: 7px 14px; }
        QLabel#stateBadge[running="true"] { background: #213d36; color: #7ee4be; }
        QTabBar::tab { color: #94a4bf; background: transparent; padding: 12px 20px; border-bottom: 2px solid #2a3445; }
        QTabBar::tab:selected { color: #edf2ff; border-bottom: 2px solid #7b9fff; }
        QTabBar::tab:hover { color: #edf2ff; background: #202735; }
        QGraphicsView, QPlainTextEdit { border: 1px solid #303a4d; border-radius: 8px; background: #11151d; }
        QPlainTextEdit { color: #dbe5f5; padding: 16px; selection-background-color: #344e82; }
        QPushButton { color: #dce5f5; background: #252e3e; border: 1px solid #3b485e; border-radius: 6px; padding: 11px 17px; }
        QPushButton:hover { background: #303d52; border-color: #677e9e; }
        QPushButton:pressed { background: #1d2635; }
        QPushButton:checked { color: #cbdcff; background: #2c426b; border-color: #7b9fff; }
        QPushButton:disabled { color: #657186; background: #202632; border-color: #2c3443; }
        QPushButton#startButton:enabled { color: #101b32; background: #8aaaff; border-color: #8aaaff; font-weight: 600; }
        QPushButton#startButton:hover { background: #a4bcff; }
        QPushButton#removeNodeButton:checked, QPushButton#removeEdgeButton:checked { color: #ffb8c1; background: #492d38; border-color: #bd697b; }
        QPushButton#stopButton:enabled { color: #f4adba; border-color: #805365; }
        QScrollBar:vertical { width: 11px; background: #151b25; }
        QScrollBar:horizontal { height: 11px; background: #151b25; }
        QScrollBar::handle { background: #44516a; border-radius: 4px; min-height: 25px; min-width: 25px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    )");
    MainWindow window;
    window.show();
    return app.exec();
}
