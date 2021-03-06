// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/panels/panel_window_event_handler.h"

#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/events/event.h"
#include "ui/base/hit_test.h"

namespace ash {
namespace internal {

PanelWindowEventHandler::PanelWindowEventHandler(aura::Window* owner)
    : ToplevelWindowEventHandler(owner) {
}

PanelWindowEventHandler::~PanelWindowEventHandler() {
}

void PanelWindowEventHandler::OnMouseEvent(ui::MouseEvent* event) {
  aura::Window* target = static_cast<aura::Window*>(event->target());
  if (event->type() == ui::ET_MOUSE_PRESSED &&
      event->flags() & ui::EF_IS_DOUBLE_CLICK &&
      event->IsOnlyLeftMouseButton() &&
      target->delegate()->GetNonClientComponent(event->location()) ==
          HTCAPTION) {
    target->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
    return;
  }
  ToplevelWindowEventHandler::OnMouseEvent(event);
}

void PanelWindowEventHandler::OnGestureEvent(ui::GestureEvent* event) {
  aura::Window* target = static_cast<aura::Window*>(event->target());
  if (event->type() == ui::ET_GESTURE_TAP &&
      event->details().tap_count() == 2 &&
      target->delegate()->GetNonClientComponent(event->location()) ==
          HTCAPTION) {
    target->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
    event->StopPropagation();
    return;
  }
  ToplevelWindowEventHandler::OnGestureEvent(event);
}

}  // namespace internal
}  // namespace ash
