/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceGlyphs.h>

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace Superbird {

static constexpr qreal GRID = 16.0;
static constexpr qreal STROKE = 1.5;

static QPainterPath polyline(std::initializer_list<QPointF> points)
{
    QPainterPath path;
    auto it = points.begin();
    path.moveTo(*it);
    for (++it; it != points.end(); ++it)
        path.lineTo(*it);
    return path;
}

static QPainterPath glyph_path(Workspaces::Glyph glyph)
{
    using Workspaces::Glyph;
    QPainterPath path;
    switch (glyph) {
    case Glyph::Home:
        path.addPath(polyline({ { 1.75, 8 }, { 8, 2.25 }, { 14.25, 8 } }));
        path.addPath(polyline({ { 3.5, 6.75 }, { 3.5, 13.75 }, { 12.5, 13.75 }, { 12.5, 6.75 } }));
        path.addPath(polyline({ { 6.5, 13.75 }, { 6.5, 10 }, { 9.5, 10 }, { 9.5, 13.75 } }));
        break;
    case Glyph::Work:
        path.addRoundedRect(QRectF(1.75, 5, 12.5, 8.75), 1.75, 1.75);
        path.addPath(polyline({ { 5.5, 5 }, { 5.5, 2.75 }, { 10.5, 2.75 }, { 10.5, 5 } }));
        path.addPath(polyline({ { 1.75, 8.75 }, { 14.25, 8.75 } }));
        break;
    case Glyph::School:
        path.addPath(polyline({ { 1.25, 6 }, { 8, 2.75 }, { 14.75, 6 }, { 8, 9.25 }, { 1.25, 6 } }));
        path.moveTo(4, 7.5);
        path.lineTo(4, 10.75);
        path.cubicTo(4, 13.25, 12, 13.25, 12, 10.75);
        path.lineTo(12, 7.5);
        path.addPath(polyline({ { 14.75, 6 }, { 14.75, 10.25 } }));
        break;
    case Glyph::Focus:
        path.addEllipse(QPointF(8, 8), 6.25, 6.25);
        path.addEllipse(QPointF(8, 8), 3.25, 3.25);
        break;
    case Glyph::Relax:
        // A cup with steam rising from it.
        path.moveTo(2.75, 6.5);
        path.lineTo(11, 6.5);
        path.lineTo(11, 10);
        path.cubicTo(11, 12.5, 9.25, 14, 6.875, 14);
        path.cubicTo(4.5, 14, 2.75, 12.5, 2.75, 10);
        path.closeSubpath();
        path.moveTo(11, 7.75);
        path.cubicTo(14.25, 7.5, 14.25, 11.5, 11, 11.25);
        path.addPath(polyline({ { 5.25, 2 }, { 5.25, 4.25 } }));
        path.addPath(polyline({ { 8.5, 2 }, { 8.5, 4.25 } }));
        break;
    case Glyph::Code:
        path.addPath(polyline({ { 5.25, 4.25 }, { 1.75, 8 }, { 5.25, 11.75 } }));
        path.addPath(polyline({ { 10.75, 4.25 }, { 14.25, 8 }, { 10.75, 11.75 } }));
        path.addPath(polyline({ { 9.25, 3 }, { 6.75, 13 } }));
        break;
    case Glyph::Book:
        path.moveTo(8, 4.5);
        path.quadTo(5, 2.75, 1.5, 3.25);
        path.lineTo(1.5, 12.5);
        path.quadTo(5, 12, 8, 13.75);
        path.quadTo(11, 12, 14.5, 12.5);
        path.lineTo(14.5, 3.25);
        path.quadTo(11, 2.75, 8, 4.5);
        path.lineTo(8, 13.75);
        break;
    case Glyph::Heart:
        path.moveTo(8, 13.75);
        path.cubicTo(1.25, 9.5, 1.25, 3, 5, 2.75);
        path.cubicTo(6.5, 2.65, 7.5, 3.5, 8, 4.75);
        path.cubicTo(8.5, 3.5, 9.5, 2.65, 11, 2.75);
        path.cubicTo(14.75, 3, 14.75, 9.5, 8, 13.75);
        break;
    case Glyph::Star: {
        QPolygonF star;
        for (int point = 0; point < 10; ++point) {
            auto radius = point % 2 == 0 ? 6.5 : 2.75;
            auto angle = qDegreesToRadians(-90.0 + (point * 36.0));
            star << QPointF(8 + (radius * qCos(angle)), 8.5 + (radius * qSin(angle)));
        }
        star << star.first();
        path.addPolygon(star);
        break;
    }
    case Glyph::Music:
        path.addEllipse(QPointF(4.5, 12), 2, 1.6);
        path.addEllipse(QPointF(11.75, 10.5), 2, 1.6);
        path.addPath(polyline({ { 6.5, 12 }, { 6.5, 4 }, { 13.75, 2.5 }, { 13.75, 10.5 } }));
        break;
    case Glyph::Travel:
        // A map pin.
        path.moveTo(8, 14.25);
        path.cubicTo(5, 10.75, 3.25, 8.5, 3.25, 6.25);
        path.cubicTo(3.25, 3.6, 5.4, 1.75, 8, 1.75);
        path.cubicTo(10.6, 1.75, 12.75, 3.6, 12.75, 6.25);
        path.cubicTo(12.75, 8.5, 11, 10.75, 8, 14.25);
        path.addEllipse(QPointF(8, 6.25), 1.9, 1.9);
        break;
    case Glyph::Shopping:
        path.addPath(polyline({ { 2.75, 5.5 }, { 13.25, 5.5 }, { 12.5, 14 }, { 3.5, 14 }, { 2.75, 5.5 } }));
        path.moveTo(5.75, 7.25);
        path.lineTo(5.75, 4.5);
        path.cubicTo(5.75, 1.5, 10.25, 1.5, 10.25, 4.5);
        path.lineTo(10.25, 7.25);
        break;
    case Glyph::Chat: {
        QPainterPath bubble;
        bubble.addRoundedRect(QRectF(1.75, 2.5, 12.5, 9), 2.75, 2.75);
        QPainterPath tail;
        tail.addPolygon(QPolygonF({ { 4.5, 10 }, { 4, 14 }, { 8.5, 10.5 } }));
        tail.closeSubpath();
        path = bubble.united(tail);
        break;
    }
    case Glyph::Finance:
        path.addPath(polyline({ { 1.75, 12 }, { 5.75, 8 }, { 8.75, 10.25 }, { 14, 4.75 } }));
        path.addPath(polyline({ { 10.5, 4.75 }, { 14, 4.75 }, { 14, 8.25 } }));
        break;
    case Glyph::Ideas:
        // A light bulb.
        path.moveTo(6, 11.5);
        path.lineTo(6, 10.25);
        path.cubicTo(4.25, 9.1, 3.5, 7.75, 3.5, 6.25);
        path.cubicTo(3.5, 3.6, 5.5, 1.75, 8, 1.75);
        path.cubicTo(10.5, 1.75, 12.5, 3.6, 12.5, 6.25);
        path.cubicTo(12.5, 7.75, 11.75, 9.1, 10, 10.25);
        path.lineTo(10, 11.5);
        path.closeSubpath();
        path.addPath(polyline({ { 6.25, 14 }, { 9.75, 14 } }));
        break;
    }
    return path;
}

void paint_workspace_glyph(QPainter& painter, QRectF const& rect, Workspaces::Glyph glyph, QColor const& color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    auto side = qMin(rect.width(), rect.height());
    auto scale = side / GRID;
    painter.translate(rect.center().x() - (side / 2), rect.center().y() - (side / 2));
    painter.scale(scale, scale);
    // NB: Below about 12 px, a 1.5 px stroke on the grid closes the glyphs up, so the stroke thins a little there.
    auto stroke = side < 12 ? STROKE * 0.85 : STROKE;
    QPen pen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    auto path = glyph_path(glyph);
    painter.drawPath(path);
    if (glyph == Workspaces::Glyph::Focus) {
        painter.setBrush(color);
        painter.drawEllipse(QPointF(8, 8), 0.6, 0.6);
    }
    painter.restore();
}

}
