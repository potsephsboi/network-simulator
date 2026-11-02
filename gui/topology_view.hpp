#pragma once

#include <QGraphicsView>
#include <map>
#include <set>
#include <vector>

class QGraphicsEllipseItem;
class QGraphicsLineItem;
class QGraphicsPathItem;

class TopologyView : public QGraphicsView {
    Q_OBJECT
public:
    enum class Mode { Move, AddNode, RemoveNode, AddEdge, RemoveEdge };
    Q_ENUM(Mode)

    explicit TopologyView(QWidget* parent = nullptr);
    int nodeCount() const { return static_cast<int>(nodes_.size()); }
    int edgeCount() const { return static_cast<int>(edges_.size()); }
    QString nodeList() const;
    QString edgeList() const;
    QPointF nodePosition(int id) const;
    Mode mode() const { return mode_; }
    void setMode(Mode mode);

signals:
    void topologyChanged();
    void modeChanged(TopologyView::Mode mode);
    void nodeAdded(int id);
    void nodeRemoved(int id);
    void edgeAdded(int u, int v);
    void edgeRemoved(int u, int v);

protected:
    bool viewportEvent(QEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    struct Edge {
        int u, v;
        QGraphicsLineItem* line;
        QGraphicsPathItem* hitbox;
    };
    int nodeAt(const QPointF& point) const;
    int edgeAt(const QPointF& point) const;
    bool positionFree(const QPointF& point, int except = -1) const;
    void createNode(const QPointF& point);
    void removeNode(int id);
    void removeEdge(int index);
    void updateEdge(Edge& edge);
    void updateNodeColors();
    void updateCursor();
    void cancelDrag();
    void createEdge(int u, int v);

    std::map<int, QGraphicsEllipseItem*> nodes_;
    std::vector<Edge> edges_;
    std::set<std::pair<int, int>> connections_;
    QGraphicsLineItem* preview_ = nullptr;
    int source_ = -1;
    int target_ = -1;
    int dragged_ = -1;
    int hovered_ = -1;
    int nextId_ = 0;
    QPointF dragOffset_;
    QPointF pointer_;
    bool pointerInside_ = false;
    Mode mode_ = Mode::Move;
};
