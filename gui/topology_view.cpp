#include "topology_view.hpp"

#include <QCursor>
#include <QEnterEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>

namespace {
constexpr qreal radius = 27;
const QColor blue("#7b9fff");
const QColor green("#66dfbc");
const QColor red("#f5808f");
}

TopologyView::TopologyView(QWidget* parent) : QGraphicsView(parent) {
    setObjectName("topologyView");
    setScene(new QGraphicsScene(this));
    setSceneRect(-480, -280, 960, 560);
    setRenderHint(QPainter::Antialiasing);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setBackgroundBrush(QColor("#11151d"));
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    centerOn(0, 0);
}

QPointF TopologyView::nodePosition(int id) const {
    return nodes_.at(id)->pos();
}

QString TopologyView::nodeList() const {
    QString text;
    for (const auto& [id, node] : nodes_) {
        if (!text.isEmpty()) text += ' ';
        text += QString::number(id);
    }
    return text;
}

QString TopologyView::edgeList() const {
    QString text;
    for (auto [u, v] : connections_)
        text += QString::number(u) + ' ' + QString::number(v) + '\n';
    return text;
}

bool TopologyView::positionFree(const QPointF& point, int except) const {
    for (const auto& [id, node] : nodes_)
        if (id != except && QLineF(point, node->pos()).length() < 2 * radius + 8)
            return false;
    return true;
}

void TopologyView::createNode(const QPointF& point) {
    int id = nextId_++;
    auto* node = scene()->addEllipse(-radius, -radius, 2 * radius, 2 * radius,
                                    QPen(QColor("#61718b"), 2), QBrush(QColor("#253044")));
    node->setPos(point);
    node->setZValue(3);
    node->setAcceptedMouseButtons(Qt::NoButton);
    auto* label = new QGraphicsSimpleTextItem(QString::number(id), node);
    QFont font = this->font();
    font.setPixelSize(17);
    font.setBold(true);
    label->setFont(font);
    label->setBrush(QColor("#f2f5fc"));
    label->setPos(-label->boundingRect().width() / 2, -label->boundingRect().height() / 2);
    label->setAcceptedMouseButtons(Qt::NoButton);
    nodes_.emplace(id, node);
    emit nodeAdded(id);
    emit topologyChanged();
    viewport()->update();
}

int TopologyView::nodeAt(const QPointF& point) const {
    for (const auto& [id, node] : nodes_)
        if (QLineF(point, node->pos()).length() <= radius) return id;
    return -1;
}

int TopologyView::edgeAt(const QPointF& point) const {
    for (int i = edgeCount() - 1; i >= 0; --i)
        if (edges_[i].hitbox->path().contains(point)) return i;
    return -1;
}

void TopologyView::updateEdge(Edge& edge) {
    QLineF segment(nodes_.at(edge.u)->pos(), nodes_.at(edge.v)->pos());
    edge.line->setLine(segment);
    QPainterPath path;
    path.moveTo(segment.p1());
    path.lineTo(segment.p2());
    QPainterPathStroker stroker;
    stroker.setWidth(18);
    stroker.setCapStyle(Qt::RoundCap);
    edge.hitbox->setPath(stroker.createStroke(path));
}

void TopologyView::createEdge(int u, int v) {
    if (u == v) return;
    if (u > v) std::swap(u, v);
    if (!connections_.insert({u, v}).second) return;
    auto* hitbox = scene()->addPath(QPainterPath(),
        QPen(QColor(245, 128, 143, 120), 1), QBrush(QColor(245, 128, 143, 38)));
    hitbox->setZValue(0);
    hitbox->setVisible(mode_ == Mode::RemoveEdge);
    hitbox->setAcceptedMouseButtons(Qt::NoButton);
    auto* line = scene()->addLine(QLineF(), QPen(QColor("#657992"), 2.5));
    line->setZValue(1);
    line->setAcceptedMouseButtons(Qt::NoButton);
    edges_.push_back({u, v, line, hitbox});
    updateEdge(edges_.back());
    emit edgeAdded(u, v);
    emit topologyChanged();
}

void TopologyView::removeEdge(int index) {
    Edge edge = edges_[index];
    connections_.erase({edge.u, edge.v});
    delete edge.line;
    delete edge.hitbox;
    edges_.erase(edges_.begin() + index);
}

void TopologyView::removeNode(int id) {
    for (int i = edgeCount() - 1; i >= 0; --i)
        if (edges_[i].u == id || edges_[i].v == id) removeEdge(i);
    delete nodes_.at(id);
    nodes_.erase(id);
    hovered_ = -1;
    // K removes a router and its links; separate D messages are unnecessary.
    emit nodeRemoved(id);
    emit topologyChanged();
    viewport()->update();
}

void TopologyView::updateNodeColors() {
    for (auto [id, node] : nodes_) {
        bool selected = id == source_ || id == dragged_;
        bool target = id == target_;
        bool deleting = mode_ == Mode::RemoveNode && id == hovered_;
        QColor border = deleting ? red : selected ? blue : target ? green : QColor("#61718b");
        QColor fill = deleting ? QColor("#58313e") : selected ? QColor("#344e82") :
                      target ? QColor("#235448") : QColor("#253044");
        node->setPen(QPen(border, selected || target || deleting ? 3 : 2));
        node->setBrush(fill);
    }
}

void TopologyView::updateCursor() {
    viewport()->setCursor(mode_ != Mode::Move ? Qt::CrossCursor :
                          dragged_ >= 0 ? Qt::ClosedHandCursor :
                          hovered_ >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void TopologyView::cancelDrag() {
    delete preview_;
    preview_ = nullptr;
    source_ = target_ = dragged_ = -1;
    updateNodeColors();
    updateCursor();
}

void TopologyView::setMode(Mode mode) {
    if (mode_ == mode) return;
    cancelDrag();
    mode_ = mode;
    QPoint cursor = viewport()->mapFromGlobal(QCursor::pos());
    pointerInside_ = viewport()->underMouse() && viewport()->rect().contains(cursor);
    pointer_ = mapToScene(cursor);
    hovered_ = pointerInside_ ? nodeAt(pointer_) : -1;
    for (const auto& edge : edges_) edge.hitbox->setVisible(mode_ == Mode::RemoveEdge);
    updateNodeColors();
    updateCursor();
    viewport()->update();
    emit modeChanged(mode_);
}

bool TopologyView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::Enter) {
        pointerInside_ = true;
        pointer_ = mapToScene(static_cast<QEnterEvent*>(event)->position().toPoint());
        hovered_ = nodeAt(pointer_);
        updateNodeColors();
        updateCursor();
        viewport()->update();
    } else if (event->type() == QEvent::Leave) {
        pointerInside_ = false;
        hovered_ = -1;
        updateNodeColors();
        updateCursor();
        viewport()->update();
    }
    return QGraphicsView::viewportEvent(event);
}

void TopologyView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    QPointF point = mapToScene(event->position().toPoint());
    if (mode_ == Mode::AddNode) {
        if (positionFree(point)) createNode(point);
    } else if (mode_ == Mode::RemoveNode) {
        int id = nodeAt(point);
        if (id >= 0) removeNode(id);
    } else if (mode_ == Mode::RemoveEdge) {
        int index = nodeAt(point) < 0 ? edgeAt(point) : -1;
        if (index >= 0) {
            Edge edge = edges_[index];
            removeEdge(index);
            emit edgeRemoved(edge.u, edge.v);
            emit topologyChanged();
        }
    } else if (mode_ == Mode::AddEdge) {
        source_ = nodeAt(point);
        if (source_ >= 0) {
            preview_ = scene()->addLine(QLineF(nodes_.at(source_)->pos(), point), QPen(blue, 2.5));
            preview_->setZValue(2);
            preview_->setAcceptedMouseButtons(Qt::NoButton);
            updateNodeColors();
        }
    } else {
        dragged_ = nodeAt(point);
        if (dragged_ >= 0) dragOffset_ = point - nodes_.at(dragged_)->pos();
        updateNodeColors();
        updateCursor();
    }
    event->accept();
}

void TopologyView::mouseMoveEvent(QMouseEvent* event) {
    pointer_ = mapToScene(event->position().toPoint());
    pointerInside_ = viewport()->rect().contains(event->position().toPoint());
    hovered_ = pointerInside_ ? nodeAt(pointer_) : -1;
    if (mode_ == Mode::AddEdge && source_ >= 0 && preview_) {
        target_ = hovered_ != source_ ? hovered_ : -1;
        preview_->setLine(QLineF(nodes_.at(source_)->pos(),
                                target_ >= 0 ? nodes_.at(target_)->pos() : pointer_));
        preview_->setPen(QPen(target_ >= 0 ? green : blue, 2.5));
    } else if (mode_ == Mode::Move && dragged_ >= 0) {
        QPointF position = pointer_ - dragOffset_;
        QRectF bounds = mapToScene(viewport()->rect()).boundingRect().adjusted(radius, radius, -radius, -radius);
        position.setX(std::clamp(position.x(), bounds.left(), bounds.right()));
        position.setY(std::clamp(position.y(), bounds.top(), bounds.bottom()));
        if (positionFree(position, dragged_)) {
            nodes_.at(dragged_)->setPos(position);
            for (auto& edge : edges_)
                if (edge.u == dragged_ || edge.v == dragged_) updateEdge(edge);
        }
    }
    updateNodeColors();
    updateCursor();
    viewport()->update();
    event->accept();
}

void TopologyView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    if (mode_ == Mode::AddEdge && source_ >= 0) {
        int target = viewport()->rect().contains(event->position().toPoint())
                     ? nodeAt(mapToScene(event->position().toPoint())) : -1;
        if (target >= 0 && target != source_) createEdge(source_, target);
    }
    cancelDrag();
    event->accept();
}

void TopologyView::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor("#11151d"));
    painter->setPen(QPen(QColor("#26303e"), 1));
    constexpr int step = 28;
    int left = static_cast<int>(std::floor(rect.left() / step)) * step;
    int top = static_cast<int>(std::floor(rect.top() / step)) * step;
    for (int x = left; x < rect.right(); x += step)
        for (int y = top; y < rect.bottom(); y += step)
            painter->drawPoint(QPointF(x, y));
}

void TopologyView::drawForeground(QPainter* painter, const QRectF&) {
    if (mode_ == Mode::AddNode && pointerInside_) {
        painter->save();
        bool free = positionFree(pointer_);
        painter->setPen(QPen(free ? blue : red, 2));
        painter->setBrush(free ? QColor(52, 78, 130, 180) : QColor(88, 49, 62, 180));
        painter->drawEllipse(pointer_, radius, radius);
        QFont font = this->font();
        font.setPixelSize(17);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor("#f2f5fc"));
        painter->drawText(QRectF(pointer_.x() - radius, pointer_.y() - radius, 2 * radius, 2 * radius),
                          Qt::AlignCenter, QString::number(nextId_));
        painter->restore();
    } else if (nodes_.empty()) {
        painter->save();
        painter->resetTransform();
        painter->setPen(QColor("#8895ab"));
        QFont font = this->font();
        font.setPixelSize(16);
        painter->setFont(font);
        painter->drawText(viewport()->rect(), Qt::AlignCenter,
                          "Choose Add node, then click to place a router.\nChoose Add edge to connect routers.");
        painter->restore();
    }
}
