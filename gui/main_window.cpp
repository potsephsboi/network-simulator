#include "main_window.hpp"
#include "engine_session.hpp"
#include "topology_view.hpp"

#include <QCloseEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QVBoxLayout>
#include <array>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Network Simulator");
    resize(1120, 760);
    setMinimumSize(980, 540);
    engine_ = new EngineSession(this);
    auto* central = new QWidget;
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);

    auto* heading = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    titles->setSpacing(5);
    auto* title = new QLabel("Network Simulator");
    title->setObjectName("title");
    titles->addWidget(title);
    heading->addLayout(titles);
    heading->addStretch();
    state_ = new QLabel("Stopped");
    state_->setObjectName("stateBadge");
    state_->setAlignment(Qt::AlignCenter);
    state_->setMinimumWidth(100);
    heading->addWidget(state_);
    layout->addLayout(heading);

    auto* navigation = new QHBoxLayout;
    auto* tabs = new QTabBar;
    tabs->setObjectName("screenTabs");
    tabs->setExpanding(false);
    tabs->addTab("Topology");
    tabs->addTab("Routing tables");
    navigation->addWidget(tabs);
    navigation->addStretch();
    counts_ = new QLabel;
    counts_->setObjectName("muted");
    navigation->addWidget(counts_);
    layout->addLayout(navigation);

    auto* screens = new QStackedWidget;
    layout->addWidget(screens, 1);
    auto* topologyPage = new QWidget;
    auto* topologyLayout = new QVBoxLayout(topologyPage);
    topologyLayout->setContentsMargins(0, 0, 0, 0);
    topologyLayout->setSpacing(14);
    topology_ = new TopologyView;
    topologyLayout->addWidget(topology_, 1);
    auto* controls = new QHBoxLayout;
    controls->setSpacing(10);
    auto* controlsLabel = new QLabel("Controls:");
    controlsLabel->setObjectName("muted");
    add_ = new QPushButton("Add node");
    add_->setObjectName("addNodeButton");
    add_->setCheckable(true);
    removeNode_ = new QPushButton("Remove node");
    removeNode_->setObjectName("removeNodeButton");
    removeNode_->setCheckable(true);
    addEdge_ = new QPushButton("Add edge");
    addEdge_->setObjectName("addEdgeButton");
    addEdge_->setCheckable(true);
    remove_ = new QPushButton("Remove edge");
    remove_->setObjectName("removeEdgeButton");
    remove_->setCheckable(true);
    start_ = new QPushButton("Start simulation");
    start_->setObjectName("startButton");
    stop_ = new QPushButton("Stop simulation");
    stop_->setObjectName("stopButton");
    controls->addWidget(controlsLabel);
    controls->addWidget(add_);
    controls->addWidget(removeNode_);
    controls->addWidget(addEdge_);
    controls->addWidget(remove_);
    controls->addStretch();
    controls->addWidget(start_);
    controls->addWidget(stop_);
    topologyLayout->addLayout(controls);
    screens->addWidget(topologyPage);

    auto* tablePage = new QWidget;
    auto* tableLayout = new QVBoxLayout(tablePage);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(12);
    auto* tableHint = new QLabel("Live snapshots from the routers. Tables refresh while the simulation runs.");
    tableHint->setObjectName("muted");
    tableHint->setWordWrap(true);
    tableLayout->addWidget(tableHint);
    tables_ = new QPlainTextEdit;
    tables_->setObjectName("routingTables");
    tables_->setReadOnly(true);
    tables_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(11);
    tables_->setFont(mono);
    tables_->setPlaceholderText("No routing tables yet.\nBuild a topology and start the simulation.");
    tableLayout->addWidget(tables_, 1);
    screens->addWidget(tablePage);

    connect(tabs, &QTabBar::currentChanged, screens, &QStackedWidget::setCurrentIndex);
    using Mode = TopologyView::Mode;
    const std::array<std::pair<QPushButton*, Mode>, 4> tools = {{
        {add_, Mode::AddNode}, {removeNode_, Mode::RemoveNode},
        {addEdge_, Mode::AddEdge}, {remove_, Mode::RemoveEdge}
    }};
    for (auto [button, mode] : tools) {
        connect(button, &QPushButton::toggled, this, [this, mode](bool checked) {
            if (checked) topology_->setMode(mode);
            else if (topology_->mode() == mode) topology_->setMode(Mode::Move);
        });
    }
    connect(topology_, &TopologyView::modeChanged, this, [tools](Mode selected) {
        for (auto [button, mode] : tools) {
            const QSignalBlocker blocked(button);
            button->setChecked(selected == mode);
        }
    });
    connect(topology_, &TopologyView::nodeAdded, this, [this](int id) {
        engine_->sendCommand('N', id);
    });
    connect(topology_, &TopologyView::nodeRemoved, this, [this](int id) {
        engine_->sendCommand('K', id);
    });
    connect(topology_, &TopologyView::edgeAdded, this, [this](int u, int v) {
        engine_->sendCommand('E', u, v);
    });
    connect(topology_, &TopologyView::edgeRemoved, this, [this](int u, int v) {
        engine_->sendCommand('D', u, v);
    });
    connect(topology_, &TopologyView::topologyChanged, this, [this] {
        if (!engine_->isRunning()) {
            tables_->clear();
            tables_->setPlaceholderText("Topology changed.\nStart the simulation to calculate new routing tables.");
        }
        updateControls();
    });
    connect(start_, &QPushButton::clicked, this, [this] {
        tables_->clear();
        tables_->setPlaceholderText("Waiting for routing tables...");
        topology_->setMode(Mode::Move);
        engine_->start(topology_->nodeList(), topology_->edgeList());
    });
    connect(stop_, &QPushButton::clicked, engine_, &EngineSession::stop);
    connect(engine_, &EngineSession::runningChanged, this, [this] {
        updateControls();
    });
    connect(engine_, &EngineSession::tablesReceived, this, [this](const QString& text) {
        int scroll = tables_->verticalScrollBar()->value();
        tables_->setPlainText(text);
        tables_->verticalScrollBar()->setValue(scroll);
    });
    connect(engine_, &EngineSession::problem, this, [this](const QString& message) {
        QMessageBox::warning(this, "Simulation", message);
    });
    updateControls();
}

void MainWindow::updateControls() {
    bool running = engine_->isRunning();
    start_->setEnabled(!running && topology_->nodeCount() > 0);
    stop_->setEnabled(running);
    state_->setText(running ? "Running" : "Stopped");
    state_->setProperty("running", running);
    state_->style()->unpolish(state_);
    state_->style()->polish(state_);
    counts_->setText(QString("%1 routers  ·  %2 links").arg(topology_->nodeCount()).arg(topology_->edgeCount()));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    engine_->stop();
    event->accept();
}
