#pragma once

#include <cstdint>

namespace stick_buttons {

constexpr uint32_t kDragHoldMs = 450;
constexpr uint32_t kScrollHoldMs = 350;
constexpr uint32_t kDoubleClickMs = 300;
constexpr uint32_t kDebounceMs = 10;

struct Actions {
  bool leftClick = false, rightClick = false, middleClick = false;
  bool leftHeld = false, scroll = false, freezePointer = false;
};

// Inputs are already debounced by M5Unified. Delay B's single click so a double
// click never emits an unwanted right click before the middle click.
class Controller {
 public:
  Actions update(bool a, bool b, bool connected, uint32_t nowMs) {
    Actions out;
    if (!connected) {
      aActive_ = bActive_ = dragging_ = scrolling_ = pendingRight_ = false;
    } else {
      if (a && !previousA_) {
        aActive_ = true;
        aStartMs_ = nowMs;
      }
      if (aActive_ && a && nowMs - aStartMs_ >= kDragHoldMs) {
        dragging_ = true;
      }
      if (!a && previousA_) {
        out.leftClick = aActive_ && !dragging_ && nowMs - aStartMs_ < kDragHoldMs;
        aActive_ = dragging_ = false;
      }
      if (b && !previousB_) {
        if (pendingRight_ && nowMs - bReleaseMs_ >= kDoubleClickMs) {
          out.rightClick = true;
          pendingRight_ = false;
        }
        bActive_ = true;
        bStartMs_ = nowMs;
      }
      if (bActive_ && b && nowMs - bStartMs_ >= kScrollHoldMs) {
        scrolling_ = true;
        pendingRight_ = false;
      }
      if (!b && previousB_) {
        if (bActive_ && !scrolling_ && nowMs - bStartMs_ < kScrollHoldMs) {
          if (pendingRight_ && bStartMs_ - bReleaseMs_ < kDoubleClickMs) {
            out.middleClick = true;
            pendingRight_ = false;
          } else {
            pendingRight_ = true;
            bReleaseMs_ = nowMs;
          }
        } else if (bActive_) {
          pendingRight_ = false;
        }
        bActive_ = scrolling_ = false;
      }
      if (pendingRight_ && !b && nowMs - bReleaseMs_ >= kDoubleClickMs) {
        out.rightClick = true;
        pendingRight_ = false;
      }
    }
    previousA_ = a;
    previousB_ = b;
    out.leftHeld = dragging_;
    out.scroll = scrolling_;
    out.freezePointer = (a && !dragging_) || (b && !scrolling_);
    return out;
  }

 private:
  uint32_t aStartMs_ = 0, bStartMs_ = 0, bReleaseMs_ = 0;
  bool previousA_ = false, previousB_ = false;
  bool aActive_ = false, bActive_ = false;
  bool dragging_ = false, scrolling_ = false, pendingRight_ = false;
};

}  // namespace stick_buttons
