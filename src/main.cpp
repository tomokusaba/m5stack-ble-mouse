#include <Arduino.h>
#include <M5Unified.h>
#include <BleMouse.h>

constexpr float kDeadzoneDeg = 1.2f;
constexpr float kSensitivity = 7.0f;
constexpr float kMaxDelta = 24.0f;
constexpr float kFilterAlpha = 0.12f;
constexpr float kResponseCurve = 0.95f;
constexpr float kVelocitySmoothing = 0.18f;
constexpr uint32_t kMotionIntervalMs = 8U;
constexpr uint32_t kClickThresholdMs = 220U;
constexpr float kVerticalSensitivityMultiplier = 1.75f;
// Use -1 to reverse an axis for a device-specific calibration.
constexpr int kCursorXSign = 1;
constexpr int kCursorYSign = -1;

static_assert(kVerticalSensitivityMultiplier > 0.0f);
static_assert(kCursorXSign == -1 || kCursorXSign == 1);
static_assert(kCursorYSign == -1 || kCursorYSign == 1);

struct MouseButtonState {
  bool left = false;
  bool pressHeld = false;
};

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

struct ImuState {
  float horizontalTilt = 0.0f;
  float verticalTilt = 0.0f;
  float filteredHorizontalTilt = 0.0f;
  float filteredVerticalTilt = 0.0f;
};

struct PointerState {
  float smoothX = 0.0f;
  float smoothY = 0.0f;
};

ImuState imuState;
PointerState pointerState;
MouseButtonState mouseButtonState;
BleMouse bleMouse("M5StickS3 IMU Mouse");
uint32_t lastMotionSampleMs = 0;
uint32_t buttonPressStartMs = 0;

void applyButtonState(bool shouldHold, uint8_t button) {
  if (shouldHold) {
    bleMouse.press(button);
  } else {
    bleMouse.release(button);
  }
}

void updateButtons() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    buttonPressStartMs = millis();
    mouseButtonState.left = true;
    mouseButtonState.pressHeld = false;
  }

  if (M5.BtnA.isPressed() && !mouseButtonState.pressHeld &&
      (millis() - buttonPressStartMs) >= kClickThresholdMs) {
    mouseButtonState.pressHeld = true;
    applyButtonState(true, MOUSE_LEFT);
  }

  if (M5.BtnA.wasReleased()) {
    const uint32_t holdDurationMs = millis() - buttonPressStartMs;
    if (mouseButtonState.pressHeld) {
      applyButtonState(false, MOUSE_LEFT);
    } else if (holdDurationMs < kClickThresholdMs) {
      bleMouse.click(MOUSE_LEFT);
    }

    mouseButtonState.left = false;
    mouseButtonState.pressHeld = false;
  }
}

void readImu() {
  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;

  M5.Imu.getAccelData(&ax, &ay, &az);

  // M5StickS3 portrait frame: +X is up, +Y is right, and +Z faces the user.
  imuState.horizontalTilt = atan2f(-ay, ax) * 180.0f / PI;
  imuState.verticalTilt = atan2f(az, sqrtf(ax * ax + ay * ay)) * 180.0f / PI;

  imuState.filteredHorizontalTilt =
      (1.0f - kFilterAlpha) * imuState.filteredHorizontalTilt +
      kFilterAlpha * imuState.horizontalTilt;
  imuState.filteredVerticalTilt =
      (1.0f - kFilterAlpha) * imuState.filteredVerticalTilt +
      kFilterAlpha * imuState.verticalTilt;
}

float computeTiltVelocity(float tiltAngle, float sensitivityMultiplier = 1.0f) {
  const float magnitude = fabsf(tiltAngle);
  if (magnitude <= kDeadzoneDeg) {
    return 0.0f;
  }

  const float normalized = clampFloat((magnitude - kDeadzoneDeg) / (20.0f - kDeadzoneDeg), 0.0f, 1.0f);
  const float gain = kSensitivity * sensitivityMultiplier * (0.9f + normalized * 2.1f);
  const float velocity = powf(magnitude, kResponseCurve) * 0.58f * gain;

  return copysignf(clampFloat(velocity, 0.0f, kMaxDelta), tiltAngle);
}

void moveMouseByImu() {
  const float targetX = kCursorXSign * computeTiltVelocity(
      imuState.filteredHorizontalTilt);
  const float targetY = kCursorYSign * computeTiltVelocity(
      imuState.filteredVerticalTilt, kVerticalSensitivityMultiplier);

  pointerState.smoothX += (targetX - pointerState.smoothX) * kVelocitySmoothing;
  pointerState.smoothY += (targetY - pointerState.smoothY) * kVelocitySmoothing;

  if (bleMouse.isConnected() && (fabsf(pointerState.smoothX) > 0.05f || fabsf(pointerState.smoothY) > 0.05f)) {
    bleMouse.move(static_cast<int>(roundf(pointerState.smoothX)), static_cast<int>(roundf(pointerState.smoothY)), 0, 0);
  }
}

void drawStatus() {
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 10);
  M5.Display.printf("BLE HID Mouse\n");
  M5.Display.setCursor(10, 40);
  M5.Display.printf("Conn: %s\n", bleMouse.isConnected() ? "YES" : "NO");
  M5.Display.setCursor(10, 70);
  M5.Display.printf("Click:%s\n", mouseButtonState.pressHeld ? "HOLD" : (mouseButtonState.left ? "ON" : "OFF"));
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true;
  M5.begin(cfg);

  M5.Display.setBrightness(90);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.println("Starting BLE Mouse...");

  if (!M5.Imu.begin()) {
    M5.Display.println("IMU init failed");
    while (true) {
      delay(1000);
    }
  }

  readImu();
  pointerState.smoothX = 0.0f;
  pointerState.smoothY = 0.0f;

  bleMouse.begin();
  M5.Display.println("BLE HID ready");
  delay(500);
}

void loop() {
  const uint32_t now = millis();

  if ((now - lastMotionSampleMs) >= kMotionIntervalMs) {
    lastMotionSampleMs = now;
    readImu();
    moveMouseByImu();
  }

  updateButtons();
  drawStatus();
  delay(10);
}
