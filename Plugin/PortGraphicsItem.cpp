#include "PortGraphicsItem.h"
#include "NetworkEditor.h"
#include "SourceGraphicsItem.h"
#include "ConnectionGraphicsItem.h"
#include "utilpq.h"

#include <vtkSMParaViewPipelineControllerWithRendering.h>
#include <vtkSMViewProxy.h>

#include <pqPipelineFilter.h>
#include <pqPipelineSource.h>
#include <pqActiveObjects.h>
#include <pqOutputPort.h>
#include <pqPropertiesPanel.h>
#include <pqView.h>

#include <QPen>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsView>
#include <QToolTip>
#include <QMainWindow>
#include <QApplication>

#include <functional>
#include <iostream>

namespace ParaViewNetworkEditor {

PortGraphicsItem::PortGraphicsItem(SourceGraphicsItem *parent, const QPointF &pos, bool up, QColor color)
    : EditorGraphicsItem(parent), source_(parent), size_(9.0f), lineWidth_(1.0f) {
  setRect(-(0.5f * size_ + lineWidth_), -(0.5f * size_ + lineWidth_), size_ + 2.0 * lineWidth_,
          size_ + 2.0 * lineWidth_);
  setPos(pos);
  setFlags(ItemSendsScenePositionChanges);

  connectionIndicator_ = new PortConnectionIndicator(this, up, color);
  connectionIndicator_->setVisible(false);
}

void PortGraphicsItem::addConnection(ConnectionGraphicsItem *connection) {
  connections_.push_back(connection);
  connectionIndicator_->setVisible(true);
  updateConnectionPositions();
  update();  // we need to repaint the connection
}

void PortGraphicsItem::removeConnection(ConnectionGraphicsItem *connection) {
  auto it = std::find(connections_.begin(), connections_.end(), connection);
  if (it != connections_.end())
    connections_.erase(it);
  connectionIndicator_->setVisible(!connections_.empty());
  // update();  // don't repaint in case source was removed before connection
}

QVariant PortGraphicsItem::itemChange(GraphicsItemChange change, const QVariant &value) {
  if (change == QGraphicsItem::ItemScenePositionHasChanged) {
    updateConnectionPositions();
  }
  return EditorGraphicsItem::itemChange(change, value);
}

std::vector<ConnectionGraphicsItem *> &PortGraphicsItem::getConnections() {
  return connections_;
}

SourceGraphicsItem *PortGraphicsItem::getSourceGraphicsItem() const { return source_; }

PortGraphicsItem::~PortGraphicsItem() = default;

InputPortGraphicsItem::InputPortGraphicsItem(SourceGraphicsItem *parent, const QPointF &pos, int port_id)
    : PortGraphicsItem(parent, pos, true, utilpq::default_color),
      portID_(port_id) {}

std::pair<pqPipelineFilter *, int> InputPortGraphicsItem::getPort() const {
  pqPipelineSource *source = this->getSourceGraphicsItem()->getSource();
  pqPipelineFilter *filter = nullptr;
  if (source) {
    filter = qobject_cast<pqPipelineFilter *>(source);
  }
  return {filter, portID_};
}

void InputPortGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent *e) {
  if (e->buttons() == Qt::LeftButton /*&& inport_->isConnected()*/) {
    getNetworkEditor()->releaseConnection(this);
  }
  e->accept();
}

void InputPortGraphicsItem::updateConnectionPositions() {
  for (auto &elem : connections_) {
    elem->updateShape();
  }
}

void InputPortGraphicsItem::showToolTip(QGraphicsSceneHelpEvent *e) {
  // showPortInfo(e, inport_);
}

void InputPortGraphicsItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
  p->save();
  p->setRenderHint(QPainter::Antialiasing, true);
  p->setRenderHint(QPainter::SmoothPixmapTransform, true);

  QColor borderColor(40, 40, 40);

  QColor color = utilpq::default_color;
  if (!this->getConnections().empty()) {
    QColor in_color = this->getConnections().front()->getColor();
    bool consistent_color = true;
    for (auto connection : this->getConnections()) {
      if (in_color != connection->getColor()) {
        consistent_color = false;
        break;
      }
    }
    if (consistent_color) {
      color = in_color;
    } else {
      color = utilpq::default_color;
    }
  }

  QRectF portRect(QPointF(-size_, size_) / 2.0f, QPointF(size_, -size_) / 2.0f);
  p->setBrush(color);
  p->setPen(QPen(borderColor, lineWidth_));

  pqPipelineSource *source = this->getSourceGraphicsItem()->getSource();
  pqPipelineFilter *filter = nullptr;
  if (source) {
    filter = qobject_cast<pqPipelineFilter *>(source);
  }
  if (filter && utilpq::optional_input(filter, portID_)) {
    // Use a different shape for optional ports (rounded at the bottom)
    QPainterPath path;
    auto start = (portRect.topRight() + portRect.bottomRight()) * 0.5;
    path.moveTo(start);
    path.lineTo(portRect.bottomRight());
    path.lineTo(portRect.bottomLeft());
    path.lineTo((portRect.topLeft() + portRect.bottomLeft()) * 0.5);
    path.arcTo(portRect, 180, -180);
    p->drawPath(path);

    // Draw a dot in the middle
    const qreal radius = 1.5;
    QRectF dotRect(QPointF(-radius, radius), QPointF(radius, -radius));
    p->setPen(borderColor.lighter(180));
    p->setBrush(borderColor.lighter(180));
    p->drawEllipse(dotRect);

  } else {
    p->drawRect(portRect);
  }

  p->restore();
}

OutputPortGraphicsItem::OutputPortGraphicsItem(SourceGraphicsItem *parent, const QPointF &pos, int port_id)
    : PortGraphicsItem(parent, pos, false, utilpq::default_color), portID_(port_id) {}

std::pair<pqPipelineSource *, int> OutputPortGraphicsItem::getPort() const {
  pqPipelineSource *source = this->getSourceGraphicsItem()->getSource();
  return {source, portID_};
}

void OutputPortGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent *e) {
  if (e->buttons() == Qt::LeftButton) {
    getNetworkEditor()->initiateConnection(this);
  }
  e->accept();

  if (pqPipelineSource *source = this->source_->getSource()) {
    pqOutputPort *port = source->getOutputPort(portID_);
    pqActiveObjects::instance().setActivePort(port);
  }
}

void OutputPortGraphicsItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) {
  auto controller = vtkSmartPointer<vtkSMParaViewPipelineControllerWithRendering>::New();
  pqView *activeView = pqActiveObjects::instance().activeView();
  vtkSMViewProxy *viewProxy = activeView ? activeView->getViewProxy() : nullptr;
  if (!viewProxy)
    return;
  if (pqPipelineSource *source = this->source_->getSource()) {
    pqOutputPort *port = source->getOutputPort(portID_);
    bool visible = controller->GetVisibility(port->getSourceProxy(), port->getPortNumber(), viewProxy);
    controller->SetVisibility(port->getSourceProxy(), port->getPortNumber(), viewProxy, !visible);
    activeView->render();
  }
}

void OutputPortGraphicsItem::updateConnectionPositions() {
  for (auto &elem : connections_) {
    elem->updateShape();
  }
}

void OutputPortGraphicsItem::showToolTip(QGraphicsSceneHelpEvent *e) {
  showToolTipHelper(e, "Hello <b>World</b>!");
}

void OutputPortGraphicsItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
  p->save();
  p->setRenderHint(QPainter::Antialiasing, true);
  p->setRenderHint(QPainter::SmoothPixmapTransform, true);

  QColor borderColor(40, 40, 40);
  float borderWidth = lineWidth_;

  if (pqPipelineSource *source = this->source_->getSource()) {
    pqOutputPort *source_port = source->getOutputPort(portID_);
    pqOutputPort *active_port = pqActiveObjects::instance().activePort();
    if (active_port == source_port) {
      borderColor = Qt::white;
      borderWidth *= 1.5;
    }
  }

  auto source_port = this->getPort();
  QColor color = utilpq::output_dataset_color(std::get<0>(source_port), std::get<1>(source_port));

  QRectF portRect(QPointF(-size_, size_) / 2.0f, QPointF(size_, -size_) / 2.0f);
  p->setBrush(color);
  p->setPen(QPen(borderColor, borderWidth));
  p->drawRect(portRect);
  p->restore();
}

PortConnectionIndicator::PortConnectionIndicator(
    PortGraphicsItem *parent, bool up, QColor color)
    : EditorGraphicsItem(parent), portConnectionItem_(parent), up_(up), color_(color) {
  setRect(-2.0f, -8.0f, 4.0f, 16.0f);
  setPos(0.0f, 0.0f);
}

void PortConnectionIndicator::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
  p->save();
  p->setRenderHint(QPainter::Antialiasing, true);

  auto connections = portConnectionItem_->getConnections();
  const bool selected = std::accumulate(connections.begin(), connections.end(), false,
                                        [](bool acc, auto &connection) { return acc || connection->isSelected(); });

  QColor color = color_;
  if (!connections.empty()) {
    QColor in_color = connections.front()->getColor();
    bool consistent_color = true;
    for (auto connection : connections) {
      if (in_color != connection->getColor()) {
        consistent_color = false;
        break;
      }
    }
    if (consistent_color) {
      color = in_color;
    } else {
      color = utilpq::default_color;
    }
  }

  const float width{selected ? (4.0f - 1.0f) / 2.0f : (3.0f - 0.5f) / 2.0f};
  const float length = 7.0f;

  QPainterPath path;
  if (up_) {
    path.moveTo(-width, -length);
    path.lineTo(-width, 0.0f);
    path.arcTo(QRectF(-width, -width, 2 * width, 2 * width), 180.0f, 180.0f);
    path.lineTo(width, -length);
  } else {
    path.moveTo(-width, length);
    path.lineTo(-width, 0.0f);
    path.arcTo(QRectF(-width, -width, 2 * width, 2 * width), 180.0f, -180.0f);
    path.lineTo(width, length);
  }
  QPainterPath closedPath(path);
  closedPath.closeSubpath();

  QLinearGradient gradBrush(QPointF(0.0f, 0.0f), QPointF(0.0, up_ ? -length : length));
  gradBrush.setColorAt(0.0f, color);
  gradBrush.setColorAt(0.75f, color);
  gradBrush.setColorAt(1.0f, QColor(color.red(), color.green(), color.blue(), 0));

  QLinearGradient gradPen(QPointF(0.0f, 0.0f), QPointF(0.0, up_ ? -length : length));
  QColor lineColor = selected ? Qt::darkRed : Qt::black;
  gradPen.setColorAt(0.0f, lineColor);
  gradPen.setColorAt(0.75f, lineColor);
  gradPen.setColorAt(1.0f, QColor(lineColor.red(), lineColor.green(), lineColor.blue(), 0));

  p->setPen(Qt::NoPen);
  p->setBrush(color);
  p->drawPath(closedPath);

  p->setBrush(Qt::NoBrush);
  p->setPen(QPen(lineColor, selected ? 1.0f : 0.5f));
  p->drawPath(path);

  p->restore();
}

}
