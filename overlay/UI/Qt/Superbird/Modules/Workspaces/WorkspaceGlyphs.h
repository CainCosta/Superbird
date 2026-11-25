/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacePresets.h>

#include <QColor>
#include <QRectF>

class QPainter;

namespace Superbird {

// Strokes a workspace glyph into the rect, as a line icon drawn the way the chrome's own icons are: on a 16 by 16 grid
// with rounded 1.5 px strokes, scaled to the rect.
void paint_workspace_glyph(QPainter&, QRectF const&, Workspaces::Glyph, QColor const&);

}
