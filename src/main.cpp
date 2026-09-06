#include <Arduino.h>
#include <M5Unified.h>
#include <BleMouse.h>

#if !defined(TARGET_M5STICKS3) && !defined(TARGET_M5STACK_CORES3)
#error "Select m5stack-sticks3 or m5stack-cores3 in PlatformIO."
#endif

namespace {

constexpr uint32_t kClickThresholdMs = 220U;

int clampHidDelta(int value) {
  return constrain(value, -127, 127);
}

#if defined(TARGET_M5STICKS3)

constexpr float kStickDeadzoneDeg = 1.2f;
constexpr float kStickSensitivity = 7.0f;
constexpr float kStickVerticalSensitivityMultiplier = 1.75f;
constexpr float kStickFilterAlpha = 0.12f;
constexpr float kStickVelocitySmoothing = 0.18f;
constexpr uint32_t kStickMotionIntervalMs = 8U;
constexpr int kStickCursorXSign = 1;
constexpr int kStickCursorYSign = -1;

static_assert(kStickCursorXSign == -1 || kStickCursorXSign == 1);
static_assert(kStickCursorYSign == -1 || kStickCursorYSign == 1);

struct StickPointerState {
  float filteredHorizontalTilt = 0.0f;
  float filteredVerticalTilt = 0.0f;
  float previousHorizontalTilt = 0.0f;
  float previousVerticalTilt = 0.0f;
  float smoothX = 0.0f;
  float smoothY = 0.0f;
};

StickPointerState stickPointer;
BleMouse bleMouse("M5StickS3 IMU Mouse");
uint32_t lastStickMotionMs = 0;
uint32_t stickButtonPressedMs = 0;
bool stickButtonDragHeld = false;

float clampFloat(float value, float minimum, float maximum) {
  return value < minimum ? minimum : (value > maximum ? maximum : value);
}

float stickAxisVelocity(float current, float previous, float multiplier = 1.0f) {
  const float delta = current - previous;
  const float magnitude = fabsf(delta);
  if (magnitude <= kStickDeadzoneDeg) {
    return 0.0f;
  }

  const float normalized = clampFloat((magnitude - kStickDeadzoneDeg) / 18.8f, 0.0f, 1.0f);
  const float gain = kStickSensitivity * multiplier * (0.9f + normalized * 2.1f);
  return copysignf(clampFloat(powf(magnitude, 0.95f) * 0.58f * gain, 0.0f, 24.0f), delta);
}

void updateStickPointer() {
  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  M5.Imu.getAccelData(&ax, &ay, &az);

  const float horizontalTilt = atan2f(-ay, ax) * 180.0f / PI;
  const float verticalTilt = atan2f(az, sqrtf(ax * ax + ay * ay)) * 180.0f / PI;
  stickPointer.filteredHorizontalTilt +=
      (horizontalTilt - stickPointer.filteredHorizontalTilt) * kStickFilterAlpha;
  stickPointer.filteredVerticalTilt +=
      (verticalTilt - stickPointer.filteredVerticalTilt) * kStickFilterAlpha;

  const float targetX = kStickCursorXSign * stickAxisVelocity(
      stickPointer.filteredHorizontalTilt, stickPointer.previousHorizontalTilt);
  const float targetY = kStickCursorYSign * stickAxisVelocity(
      stickPointer.filteredVerticalTilt, stickPointer.previousVerticalTilt,
      kStickVerticalSensitivityMultiplier);
  stickPointer.smoothX += (targetX - stickPointer.smoothX) * kStickVelocitySmoothing;
  stickPointer.smoothY += (targetY - stickPointer.smoothY) * kStickVelocitySmoothing;

  if (bleMouse.isConnected() &&
      (fabsf(stickPointer.smoothX) > 0.05f || fabsf(stickPointer.smoothY) > 0.05f)) {
    bleMouse.move(static_cast<signed char>(clampHidDelta(roundf(stickPointer.smoothX))),
                  static_cast<signed char>(clampHidDelta(roundf(stickPointer.smoothY))));
  }

  stickPointer.previousHorizontalTilt = stickPointer.filteredHorizontalTilt;
  stickPointer.previousVerticalTilt = stickPointer.filteredVerticalTilt;
}

void updateStickButton() {
  if (M5.BtnA.wasPressed()) {
    stickButtonPressedMs = millis();
    stickButtonDragHeld = false;
  }
  if (M5.BtnA.isPressed() && !stickButtonDragHeld &&
      millis() - stickButtonPressedMs >= kClickThresholdMs) {
    bleMouse.press(MOUSE_LEFT);
    stickButtonDragHeld = true;
  }
  if (M5.BtnA.wasReleased()) {
    if (stickButtonDragHeld) {
      bleMouse.release(MOUSE_LEFT);
    } else if (millis() - stickButtonPressedMs < kClickThresholdMs) {
      bleMouse.click(MOUSE_LEFT);
    }
  }
}

void drawStickStatus() {
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.printf("BLE IMU Mouse\n");
  M5.Display.setCursor(8, 36);
  M5.Display.printf("Conn: %s\n", bleMouse.isConnected() ? "YES" : "NO");
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_imu = true;
  M5.begin(config);
  M5.Display.setBrightness(90);
  M5.Display.fillScreen(TFT_BLACK);
  if (!M5.Imu.isEnabled() && !M5.Imu.begin()) {
    M5.Display.println("IMU init failed");
    while (true) {
      delay(1000);
    }
  }

  bleMouse.begin();
  drawStickStatus();
}

void loop() {
  M5.update();
  updateStickButton();

  if (millis() - lastStickMotionMs >= kStickMotionIntervalMs) {
    lastStickMotionMs = millis();
    updateStickPointer();
  }
  drawStickStatus();
  delay(8);
}

#else

// M5Unified returns CoreS3 axes as +X right, +Y toward the top, +Z out of the display.
constexpr float kAirMouseDeadzoneDps = 2.0f;
constexpr float kAirMousePixelsPerDps = 0.045f;
constexpr float kAirMouseSmoothing = 0.22f;
constexpr float kGravityMinimumG = 0.70f;
constexpr float kGravityMaximumG = 1.30f;
constexpr float kMinimumUprightGravityG = 0.30f;
constexpr float kShakeThresholdG = 1.75f;
constexpr uint32_t kShakeCooldownMs = 900U;
constexpr int kAirMouseXSign = 1;
constexpr int kAirMouseYSign = -1;

constexpr float kTrackpadSensitivity = 1.55f;
constexpr float kScrollSensitivity = 0.18f;
constexpr int kTouchMovementThresholdPixels = 4;
constexpr int kScrollMovementThresholdPixels = 2;
constexpr uint16_t kVirtualButtonBarHeight = 30;
constexpr uint16_t kHoldToggleHeight = 30;
constexpr uint32_t kVirtualButtonHoldMs = 450U;

constexpr bool kEnableClapClick = false;
constexpr int kClapPeakThreshold = 9000;
constexpr uint32_t kClapCooldownMs = 500U;
constexpr size_t kClapSampleCount = 64;

static_assert(kAirMouseXSign == -1 || kAirMouseXSign == 1);
static_assert(kAirMouseYSign == -1 || kAirMouseYSign == 1);
static_assert(kGravityMinimumG < kGravityMaximumG);
static_assert(kTrackpadSensitivity > 0.0f);

struct AirMouseState {
  float smoothX = 0.0f;
  float smoothY = 0.0f;
  float magneticHeadingDeg = 0.0f;
  bool pointingEnabled = true;
  bool gravityStable = false;
  bool upright = false;
  bool magnetometerAvailable = false;
};

struct TouchGestureState {
  bool active = false;
  bool moved = false;
  bool twoFinger = false;
  uint32_t startedMs = 0;
};

AirMouseState airMouse;
TouchGestureState touchGesture;
BleMouse bleMouse("M5Stack CoreS3 Sensor Mouse");
bool dragLockEnabled = false;
bool virtualLeftHeld = false;
bool virtualMiddleHeld = false;
bool virtualRightHeld = false;
bool leftButtonPressed = false;
bool holdToggleCaptured = false;
uint32_t lastShakeMs = 0;
uint32_t lastClapMs = 0;
uint32_t lastStatusDrawMs = 0;

int trackpadBottomY() {
  return M5.Display.height() - kVirtualButtonBarHeight - kHoldToggleHeight;
}

bool isInTrackpad(const m5::touch_detail_t& touch) {
  return touch.y >= 0 && touch.y < trackpadBottomY();
}

bool isOnHoldToggle(const m5::touch_detail_t& touch) {
  return touch.y >= trackpadBottomY() &&
         touch.y < M5.Display.height() - kVirtualButtonBarHeight;
}

void syncLeftButton() {
  const bool shouldPress = dragLockEnabled || virtualLeftHeld;
  if (shouldPress == leftButtonPressed) {
    return;
  }
  if (shouldPress) {
    bleMouse.press(MOUSE_LEFT);
  } else {
    bleMouse.release(MOUSE_LEFT);
  }
  leftButtonPressed = shouldPress;
}

void clearAirMouseMotion() {
  airMouse.smoothX = 0.0f;
  airMouse.smoothY = 0.0f;
}

void updateAirMouse() {
  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  float gx = 0.0f;
  float gy = 0.0f;
  float gz = 0.0f;
  M5.Imu.getAccelData(&ax, &ay, &az);
  M5.Imu.getGyroData(&gx, &gy, &gz);

  const float gravityG = sqrtf(ax * ax + ay * ay + az * az);
  airMouse.gravityStable = gravityG >= kGravityMinimumG && gravityG <= kGravityMaximumG;
  airMouse.upright = fabsf(ay) >= kMinimumUprightGravityG;

  float mx = 0.0f;
  float my = 0.0f;
  float mz = 0.0f;
  airMouse.magnetometerAvailable = M5.Imu.getMag(&mx, &my, &mz);
  if (airMouse.magnetometerAvailable) {
    const float roll = atan2f(ay, az);
    const float pitch = atan2f(-ax, sqrtf(ay * ay + az * az));
    const float horizontalFieldX = mx * cosf(pitch) + mz * sinf(pitch);
    const float horizontalFieldY =
        mx * sinf(roll) * sinf(pitch) + my * cosf(roll) - mz * sinf(roll) * cosf(pitch);
    airMouse.magneticHeadingDeg = atan2f(horizontalFieldY, horizontalFieldX) * 180.0f / PI;
    if (airMouse.magneticHeadingDeg < 0.0f) {
      airMouse.magneticHeadingDeg += 360.0f;
    }
  }

  if (gravityG >= kShakeThresholdG && millis() - lastShakeMs >= kShakeCooldownMs) {
    airMouse.pointingEnabled = !airMouse.pointingEnabled;
    lastShakeMs = millis();
    clearAirMouseMotion();
  }

  const float angularSpeedDps = sqrtf(gx * gx + gy * gy);
  if (!airMouse.pointingEnabled || !airMouse.gravityStable || !airMouse.upright ||
      angularSpeedDps <= kAirMouseDeadzoneDps) {
    clearAirMouseMotion();
    return;
  }

  const float targetX = kAirMouseXSign * gy * kAirMousePixelsPerDps;
  const float targetY = kAirMouseYSign * gx * kAirMousePixelsPerDps;
  airMouse.smoothX += (targetX - airMouse.smoothX) * kAirMouseSmoothing;
  airMouse.smoothY += (targetY - airMouse.smoothY) * kAirMouseSmoothing;

  if (bleMouse.isConnected() &&
      (fabsf(airMouse.smoothX) >= 0.5f || fabsf(airMouse.smoothY) >= 0.5f)) {
    bleMouse.move(static_cast<signed char>(clampHidDelta(roundf(airMouse.smoothX))),
                  static_cast<signed char>(clampHidDelta(roundf(airMouse.smoothY))));
  }
}

void finishTouchGesture() {
  if (!touchGesture.moved && bleMouse.isConnected()) {
    if (touchGesture.twoFinger || millis() - touchGesture.startedMs >= kClickThresholdMs) {
      bleMouse.click(MOUSE_RIGHT);
    } else {
      bleMouse.click(MOUSE_LEFT);
    }
  }
  touchGesture = {};
}

void updateTrackpad() {
  uint8_t trackpadTouches = 0;
  const m5::touch_detail_t* firstTouch = nullptr;
  int totalScrollDeltaY = 0;

  for (uint8_t index = 0; index < M5.Touch.getCount(); ++index) {
    const auto& touch = M5.Touch.getDetail(index);
    if (!isInTrackpad(touch)) {
      continue;
    }
    if (firstTouch == nullptr) {
      firstTouch = &touch;
    }
    ++trackpadTouches;
    totalScrollDeltaY += touch.deltaY();
  }

  if (!touchGesture.active && firstTouch != nullptr) {
    touchGesture.active = true;
    touchGesture.startedMs = millis();
  }
  if (!touchGesture.active) {
    return;
  }
  if (trackpadTouches == 0) {
    finishTouchGesture();
    return;
  }

  if (trackpadTouches >= 2) {
    touchGesture.twoFinger = true;
    const int averageDeltaY = totalScrollDeltaY / trackpadTouches;
    if (abs(averageDeltaY) >= kScrollMovementThresholdPixels) {
      touchGesture.moved = true;
      const int wheel = clampHidDelta(roundf(-averageDeltaY * kScrollSensitivity));
      if (bleMouse.isConnected() && wheel != 0) {
        bleMouse.move(0, 0, static_cast<signed char>(wheel), 0);
      }
    }
    return;
  }

  if (touchGesture.twoFinger || firstTouch == nullptr) {
    return;
  }
  if (abs(firstTouch->distanceX()) >= kTouchMovementThresholdPixels ||
      abs(firstTouch->distanceY()) >= kTouchMovementThresholdPixels) {
    touchGesture.moved = true;
  }
  if (!touchGesture.moved) {
    return;
  }

  const int deltaX = clampHidDelta(roundf(firstTouch->deltaX() * kTrackpadSensitivity));
  const int deltaY = clampHidDelta(roundf(firstTouch->deltaY() * kTrackpadSensitivity));
  if (bleMouse.isConnected() && (deltaX != 0 || deltaY != 0)) {
    bleMouse.move(static_cast<signed char>(deltaX), static_cast<signed char>(deltaY));
  }
}

void updateHoldToggle() {
  const auto& touch = M5.Touch.getDetail();
  if (touch.wasPressed() && isOnHoldToggle(touch)) {
    holdToggleCaptured = true;
  }
  if (holdToggleCaptured && touch.wasReleased()) {
    dragLockEnabled = !dragLockEnabled;
    holdToggleCaptured = false;
    syncLeftButton();
  }
}

void updateVirtualButton(m5::Button_Class& button, uint8_t mouseButton, bool& held) {
  if (button.isHolding() && !held) {
    held = true;
    if (mouseButton == MOUSE_LEFT) {
      syncLeftButton();
    } else {
      bleMouse.press(mouseButton);
    }
  }
  if (button.wasReleasedAfterHold() && held) {
    held = false;
    if (mouseButton == MOUSE_LEFT) {
      syncLeftButton();
    } else {
      bleMouse.release(mouseButton);
    }
  } else if (button.wasClicked() && bleMouse.isConnected()) {
    bleMouse.click(mouseButton);
  }
}

void updateVirtualButtons() {
  updateVirtualButton(M5.BtnA, MOUSE_LEFT, virtualLeftHeld);
  updateVirtualButton(M5.BtnB, MOUSE_MIDDLE, virtualMiddleHeld);
  updateVirtualButton(M5.BtnC, MOUSE_RIGHT, virtualRightHeld);
}

void updateClapClick() {
  if (!kEnableClapClick || !M5.Mic.isEnabled()) {
    return;
  }

  static int16_t samples[kClapSampleCount];
  if (!M5.Mic.record(samples, kClapSampleCount)) {
    return;
  }

  int peak = 0;
  for (const auto sample : samples) {
    peak = max(peak, abs(static_cast<int>(sample)));
  }
  if (peak >= kClapPeakThreshold && millis() - lastClapMs >= kClapCooldownMs) {
    if (bleMouse.isConnected()) {
      bleMouse.click(MOUSE_LEFT);
    }
    lastClapMs = millis();
  }
}

void drawCoreUi() {
  const int width = M5.Display.width();
  const int height = M5.Display.height();
  const int trackpadBottom = trackpadBottomY();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.drawRect(0, 0, width, trackpadBottom, TFT_DARKGREEN);
  M5.Display.setTextColor(TFT_DARKGREEN, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, trackpadBottom - 12);
  M5.Display.print("TRACKPAD: 1F MOVE / 2F SCROLL");
  M5.Display.drawRect(0, trackpadBottom, width, kHoldToggleHeight, TFT_YELLOW);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(8, trackpadBottom + 10);
  M5.Display.print("HOLD / DRAG TOGGLE");

  const int buttonY = height - kVirtualButtonBarHeight;
  const int buttonWidth = width / 3;
  M5.Display.drawRect(0, buttonY, buttonWidth, kVirtualButtonBarHeight, TFT_CYAN);
  M5.Display.drawRect(buttonWidth, buttonY, buttonWidth, kVirtualButtonBarHeight, TFT_CYAN);
  M5.Display.drawRect(buttonWidth * 2, buttonY, width - buttonWidth * 2,
                      kVirtualButtonBarHeight, TFT_CYAN);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setCursor(14, buttonY + 10);
  M5.Display.print("LEFT");
  M5.Display.setCursor(buttonWidth + 10, buttonY + 10);
  M5.Display.print("MIDDLE");
  M5.Display.setCursor(buttonWidth * 2 + 12, buttonY + 10);
  M5.Display.print("RIGHT");
}

void drawCoreStatus() {
  if (millis() - lastStatusDrawMs < 200U) {
    return;
  }
  lastStatusDrawMs = millis();
  M5.Display.fillRect(4, 4, M5.Display.width() - 8, 22, TFT_BLACK);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, 8);
  M5.Display.printf("BLE:%s AIR:%s MAG:%s %03.0f",
                    bleMouse.isConnected() ? "ON" : "WAIT",
                    airMouse.pointingEnabled ? "ON" : "PAUSE",
                    airMouse.magnetometerAvailable ? "OK" : "--",
                    airMouse.magneticHeadingDeg);
  M5.Display.fillRect(4, trackpadBottomY() + 2, M5.Display.width() - 8, 18, TFT_BLACK);
  M5.Display.setTextColor(dragLockEnabled ? TFT_RED : TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(8, trackpadBottomY() + 10);
  M5.Display.printf("HOLD / DRAG: %s", dragLockEnabled ? "ON" : "OFF");
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_imu = true;
  config.internal_mic = kEnableClapClick;
  config.internal_spk = false;
  M5.begin(config);
  M5.Display.setBrightness(90);
  M5.setTouchButtonHeight(kVirtualButtonBarHeight);
  M5.BtnA.setHoldThresh(kVirtualButtonHoldMs);
  M5.BtnB.setHoldThresh(kVirtualButtonHoldMs);
  M5.BtnC.setHoldThresh(kVirtualButtonHoldMs);

  if (!M5.Imu.isEnabled() && !M5.Imu.begin()) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.println("IMU init failed");
    while (true) {
      delay(1000);
    }
  }
  if (kEnableClapClick && M5.Mic.isEnabled() && !M5.Mic.begin()) {
    M5.Display.println("Mic init failed");
  }

  bleMouse.begin();
  drawCoreUi();
  drawCoreStatus();
}

void loop() {
  M5.update();
  updateAirMouse();
  updateTrackpad();
  updateHoldToggle();
  updateVirtualButtons();
  updateClapClick();
  drawCoreStatus();
  delay(8);
}

#endif
