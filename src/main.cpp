#include <Arduino.h>
#include <M5Unified.h>
#include <BleMouse.h>

constexpr float kTouchSensitivity = 1.8f;
constexpr int kTouchMovementThresholdPixels = 4;
constexpr int kMaxMouseDelta = 127;
constexpr uint32_t kClickThresholdMs = 220U;

static_assert(kTouchSensitivity > 0.0f);
static_assert(kTouchMovementThresholdPixels >= 0);

struct TouchpadState {
  bool active = false;
  bool moved = false;
  bool dragHeld = false;
};

TouchpadState touchpadState;
BleMouse bleMouse("M5Stack CoreS3 Touchpad");
uint32_t touchStartMs = 0;
bool statusDrawn = false;
bool displayedConnected = false;
bool displayedActive = false;
bool displayedDragHeld = false;

int clampMouseDelta(int value) {
  if (value < -kMaxMouseDelta) {
    return -kMaxMouseDelta;
  }
  if (value > kMaxMouseDelta) {
    return kMaxMouseDelta;
  }
  return value;
}

bool movedBeyondTapThreshold(const m5::touch_detail_t& touch) {
  return abs(touch.distanceX()) >= kTouchMovementThresholdPixels ||
         abs(touch.distanceY()) >= kTouchMovementThresholdPixels;
}

void moveMouseByTouch(const m5::touch_detail_t& touch) {
  const int deltaX = static_cast<int>(roundf(touch.deltaX() * kTouchSensitivity));
  const int deltaY = static_cast<int>(roundf(touch.deltaY() * kTouchSensitivity));

  if (bleMouse.isConnected() && (deltaX != 0 || deltaY != 0)) {
    bleMouse.move(clampMouseDelta(deltaX), clampMouseDelta(deltaY), 0, 0);
  }
}

void updateTouchpad() {
  M5.update();
  const auto touch = M5.Touch.getDetail();

  if (touch.wasPressed()) {
    touchStartMs = millis();
    touchpadState.active = true;
    touchpadState.moved = false;
    touchpadState.dragHeld = false;
  }

  if (touch.isPressed()) {
    if (movedBeyondTapThreshold(touch)) {
      touchpadState.moved = true;
    }

    if (touchpadState.moved) {
      moveMouseByTouch(touch);
    } else if (!touchpadState.dragHeld &&
               (millis() - touchStartMs) >= kClickThresholdMs) {
      bleMouse.press(MOUSE_LEFT);
      touchpadState.dragHeld = true;
    }
  }

  if (touch.wasReleased()) {
    if (touchpadState.dragHeld) {
      bleMouse.release(MOUSE_LEFT);
    } else if (!touchpadState.moved && bleMouse.isConnected()) {
      bleMouse.click(MOUSE_LEFT);
    }

    touchpadState.active = false;
    touchpadState.moved = false;
    touchpadState.dragHeld = false;
  }
}

void drawInstructions() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 10);
  M5.Display.printf("BLE Touchpad\n");
  M5.Display.setCursor(10, 120);
  M5.Display.printf("Tap: click\n");
  M5.Display.setCursor(10, 145);
  M5.Display.printf("Swipe: move\n");
  M5.Display.setCursor(10, 170);
  M5.Display.printf("Hold: drag\n");
}

void drawStatus() {
  const bool connected = bleMouse.isConnected();
  if (statusDrawn && connected == displayedConnected &&
      touchpadState.active == displayedActive &&
      touchpadState.dragHeld == displayedDragHeld) {
    return;
  }

  M5.Display.fillRect(10, 40, 300, 55, TFT_BLACK);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 40);
  M5.Display.printf("Conn: %s\n", connected ? "YES" : "NO");
  M5.Display.setCursor(10, 70);
  M5.Display.printf("Touch: %s\n", touchpadState.dragHeld
                                       ? "DRAG"
                                       : (touchpadState.active ? "ON" : "OFF"));

  statusDrawn = true;
  displayedConnected = connected;
  displayedActive = touchpadState.active;
  displayedDragHeld = touchpadState.dragHeld;
}

void setup() {
  M5.begin();

  M5.Display.setBrightness(90);
  bleMouse.begin();
  drawInstructions();
  drawStatus();
  delay(500);
}

void loop() {
  updateTouchpad();
  drawStatus();
  delay(10);
}
