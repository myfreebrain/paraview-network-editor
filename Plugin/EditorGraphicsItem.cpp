#include "EditorGraphicsItem.h"
#include "NetworkEditor.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QToolTip>

namespace ParaViewNetworkEditor {

EditorGraphicsItem::EditorGraphicsItem() : QGraphicsRectItem() {}

EditorGraphicsItem::EditorGraphicsItem(QGraphicsItem *parent) : QGraphicsRectItem(parent) {}

EditorGraphicsItem::~EditorGraphicsItem() = default;

QPoint EditorGraphicsItem::mapPosToSceen(QPointF inPos) const {
  if (scene() != nullptr                                  // the focus item belongs to a scene
      && !scene()->views().isEmpty()                      // that scene is displayed in a view...
      && scene()->views().first() != nullptr              // ... which is not null...
      && scene()->views().first()->viewport() != nullptr  // ... and has a viewport
      ) {
    QPointF sceneP = mapToScene(inPos);
    QGraphicsView *v = scene()->views().first();
    QPoint viewP = v->mapFromScene(sceneP);
    return v->viewport()->mapToGlobal(viewP);
  } else {
    return QPoint(0, 0);
  }
}

void EditorGraphicsItem::showToolTip(QGraphicsSceneHelpEvent *) {}

void EditorGraphicsItem::showToolTipHelper(QGraphicsSceneHelpEvent *e, QString string) const {
  QGraphicsView *v = scene()->views().first();
  QRectF rect = this->mapRectToScene(this->rect());
  QRect viewRect = v->mapFromScene(rect).boundingRect();
  e->accept();
  QToolTip::showText(e->screenPos(), string, v, viewRect);
}

NetworkEditor *EditorGraphicsItem::getNetworkEditor() const {
  return qobject_cast<NetworkEditor *>(scene());
}

}
