#include <Arduino.h>
#include <M5Unified.h>
#include <BleMouse.h>

#if defined(TARGET_M5STACK_CORES3)
#include "AirMouse.h"
#include "AirMouseImu.h"
#include <freertos/semphr.h>
#endif

#if !defined(TARGET_M5STICKS3) && !defined(TARGET_M5STACK_CORES3)
#error "Select m5stack-sticks3 or m5stack-cores3 in PlatformIO."
#endif

#if defined(TARGET_M5STACK_CORES3)

namespace {

constexpr uint32_t kClickThresholdMs = 220U;

int clampHidDelta(int value) {
  return constrain(value, -127, 127);
}

namespace air = air_mouse;
constexpr uint32_t kTouchPollUs = 5000;
constexpr uint32_t kStatusIntervalMs = 200;

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

static_assert(kTrackpadSensitivity > 0.0f);
static_assert(pdMS_TO_TICKS(air::kImuPollMs) > 0, "IMU polling requires a <=2ms RTOS tick");

struct TouchGestureState {
  bool active = false;
  bool moved = false;
  bool twoFinger = false;
  uint32_t startedMs = 0;
};

air::Controller airMouse;
SemaphoreHandle_t airMouseMutex = nullptr;
bool imuHealthy = true;
uint32_t imuSampleRateHz = 0;
bool magnetometerAvailable = false;
float magneticHeadingDeg = 0;
bool micHealthy = false;
TouchGestureState touchGesture;
BleMouse bleMouse("M5Stack CoreS3 Sensor Mouse");
bool dragLockEnabled = false;
bool virtualLeftHeld = false;
bool virtualMiddleHeld = false;
bool virtualRightHeld = false;
bool leftButtonPressed = false;
bool holdToggleCaptured = false;
uint32_t lastClapMs = 0;
uint32_t lastStatusDrawMs = 0;
uint32_t lastTouchPollUs = 0;
uint32_t lastReportUs = 0;

void coreFatal(const char* message) {
  Serial.println(message);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, 8);
  M5.Display.println(message);
  while (true) {
    delay(1000);
  }
}

bool configureCoreImu() {
  return configureAirMouseImu();
}

void sampleAirMouseTask(void*) {
  TickType_t wake = xTaskGetTickCount();
  uint32_t lastGoodUs = micros();
  uint32_t lastMagUs = 0;
  uint32_t lastAccelUs = 0;
  uint32_t rateWindowUs = lastGoodUs;
  uint32_t sampleCount = 0;
  air::Vec3 accel;
  bool haveAccel = false;
  for (;;) {
    // This mutex also covers M5.update(): touch and IMU share the internal I2C bus.
    xSemaphoreTake(airMouseMutex, portMAX_DELAY);
    const auto updated = M5.Imu.update();
    const uint32_t now = micros();
    const auto data = M5.Imu.getImuData();
    if (updated & m5::IMU_Class::sensor_mask_accel) {
      accel = {data.accel.x, data.accel.y, data.accel.z};
      haveAccel = true;
      lastAccelUs = now;
    }
    if ((updated & m5::IMU_Class::sensor_mask_gyro) && haveAccel &&
        now - lastAccelUs <= air::kMaxSampleGapUs) {
      const air::Vec3 gyro(data.gyro.x, data.gyro.y, data.gyro.z);
      if (air::finite(accel) && air::finite(gyro)) {
        airMouse.sample(gyro, accel, now);
        lastGoodUs = now;
        ++sampleCount;
        if (!imuHealthy) {
          Serial.println("IMU samples recovered");
        }
        imuHealthy = true;
      }
    }
    if (now - lastGoodUs > air::kMaxSampleGapUs) {
      if (imuHealthy) {
        Serial.println("IMU samples missing: pointing stopped");
      }
      imuHealthy = false;
      airMouse.clearMotion();
    }
    if (now - rateWindowUs >= 1000000U) {
      imuSampleRateHz = static_cast<uint32_t>(
          static_cast<uint64_t>(sampleCount) * 1000000U / (now - rateWindowUs));
      sampleCount = 0;
      rateWindowUs = now;
    }
    if (updated & m5::IMU_Class::sensor_mask_mag) {
      lastMagUs = now;
      const air::Vec3 mag(data.mag.x, data.mag.y, data.mag.z);
      magnetometerAvailable = air::finite(mag) && air::length(mag) > 0.01f;
      if (magnetometerAvailable) {
        const auto g = airMouse.gravity;
        const float roll = atan2f(g.y, g.z);
        const float pitch = atan2f(-g.x, sqrtf(g.y * g.y + g.z * g.z));
        const float fieldX = mag.x * cosf(pitch) + mag.z * sinf(pitch);
        const float fieldY = mag.x * sinf(roll) * sinf(pitch) +
                             mag.y * cosf(roll) - mag.z * sinf(roll) * cosf(pitch);
        magneticHeadingDeg = atan2f(fieldY, fieldX) * 180.0f / PI;
        if (magneticHeadingDeg < 0) {
          magneticHeadingDeg += 360.0f;
        }
      }
    } else if (now - lastMagUs > 500000U) {
      magnetometerAvailable = false;
    }
    xSemaphoreGive(airMouseMutex);
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(air::kImuPollMs));
  }
}

bool touchingOrButtonHeld() {
  for (uint8_t i = 0; i < M5.Touch.getCount(); ++i) {
    if (M5.Touch.getDetail(i).isPressed()) {
      return true;
    }
  }
  return M5.BtnA.isPressed() || M5.BtnB.isPressed() || M5.BtnC.isPressed();
}

void reportAirMouse() {
  const uint32_t now = micros();
  if (now - lastReportUs < air::kReportIntervalUs) {
    return;
  }
  lastReportUs = now;
  int dx = 0, dy = 0;
  const bool connected = bleMouse.isConnected();
  xSemaphoreTake(airMouseMutex, portMAX_DELAY);
  airMouse.setInput(touchingOrButtonHeld(), connected, now);
  if (imuHealthy && !airMouse.blocked(now)) {
    dx = air::PixelAccumulator::take(airMouse.pending.x);
    dy = air::PixelAccumulator::take(airMouse.pending.y);
  } else {
    airMouse.clearMotion();
  }
  xSemaphoreGive(airMouseMutex);
  if (connected && (dx || dy)) {
    bleMouse.move(static_cast<signed char>(dx), static_cast<signed char>(dy));
  }
}

void coreClick(uint8_t button) {
  xSemaphoreTake(airMouseMutex, portMAX_DELAY);
  airMouse.freeze(micros());
  xSemaphoreGive(airMouseMutex);
  // BleMouse::click clears all held buttons, including the drag lock.
  if (bleMouse.isConnected() && !bleMouse.isPressed(button)) {
    bleMouse.press(button);
    bleMouse.release(button);
  }
}

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

void finishTouchGesture() {
  if (!touchGesture.moved && bleMouse.isConnected()) {
    if (touchGesture.twoFinger || millis() - touchGesture.startedMs >= kClickThresholdMs) {
      coreClick(MOUSE_RIGHT);
    } else {
      coreClick(MOUSE_LEFT);
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
    if (!touch.isPressed() || !isInTrackpad(touch)) {
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
    coreClick(mouseButton);
  }
}

void updateVirtualButtons() {
  updateVirtualButton(M5.BtnA, MOUSE_LEFT, virtualLeftHeld);
  updateVirtualButton(M5.BtnB, MOUSE_MIDDLE, virtualMiddleHeld);
  updateVirtualButton(M5.BtnC, MOUSE_RIGHT, virtualRightHeld);
}

void updateClapClick() {
  if (!kEnableClapClick || !micHealthy || M5.Mic.isRecording()) {
    return;
  }

  static int16_t samples[kClapSampleCount];
  static bool recorded = false;
  if (recorded) {
    int peak = 0;
    for (const auto sample : samples) {
      peak = max(peak, abs(static_cast<int>(sample)));
    }
    if (peak >= kClapPeakThreshold && millis() - lastClapMs >= kClapCooldownMs) {
      coreClick(MOUSE_LEFT);
      lastClapMs = millis();
    }
  }
  recorded = M5.Mic.record(samples, kClapSampleCount);
  if (!recorded) {
    Serial.println("Mic recording failed: clap disabled");
    micHealthy = false;
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
  if (millis() - lastStatusDrawMs < kStatusIntervalMs) {
    return;
  }
  lastStatusDrawMs = millis();
  xSemaphoreTake(airMouseMutex, portMAX_DELAY);
  const char* airStatus = !imuHealthy ? "ERROR" : !airMouse.calibrated ? "CAL" :
                         !airMouse.pointingEnabled ? "PAUSE" :
                         airMouse.blocked(micros()) ? "FREEZE" : "ON";
  const bool calibrating = !airMouse.calibrated;
  const uint32_t retries = airMouse.calibrationRetries;
  const bool magAvailable = magnetometerAvailable;
  const float heading = magneticHeadingDeg;
  const bool fallback = airMouse.gravityFallback;
  const uint32_t sampleRateHz = imuSampleRateHz;
  xSemaphoreGive(airMouseMutex);
  M5.Display.fillRect(4, 4, M5.Display.width() - 8, 38, TFT_BLACK);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, 8);
  M5.Display.printf("BLE:%s AIR:%s MAG:%s %03.0f",
                    bleMouse.isConnected() ? "ON" : "WAIT",
                    airStatus, magAvailable ? "OK" : "--", heading);
  M5.Display.setCursor(8, 24);
  if (calibrating) {
    M5.Display.printf("Calibrating... Keep still! Retry:%lu", static_cast<unsigned long>(retries));
  } else {
    M5.Display.printf("%luHz / %s / MIC:%s", static_cast<unsigned long>(sampleRateHz),
                      fallback ? "BODY AXES" : "ROLL COMP",
                      !kEnableClapClick ? "OFF" : micHealthy ? "ON" : "ERROR");
  }
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

  if (!configureCoreImu()) {
    coreFatal("BMI270 configuration failed");
  }
  airMouseMutex = xSemaphoreCreateMutex();
  if (!airMouseMutex) {
    coreFatal("IMU mutex allocation failed");
  }
  if (kEnableClapClick) {
    micHealthy = M5.Mic.isEnabled() && M5.Mic.begin();
    if (!micHealthy) {
      Serial.println("Mic init failed: clap disabled");
    }
  }

  bleMouse.begin();
  drawCoreUi();
  lastStatusDrawMs = millis() - kStatusIntervalMs;
  drawCoreStatus();
  if (xTaskCreate(sampleAirMouseTask, "air-imu", 4096, nullptr, 2, nullptr) != pdPASS) {
    coreFatal("IMU task allocation failed");
  }
}

void loop() {
  const uint32_t now = micros();
  if (now - lastTouchPollUs >= kTouchPollUs) {
    lastTouchPollUs = now;
    xSemaphoreTake(airMouseMutex, portMAX_DELAY);
    M5.update();
    airMouse.setInput(touchingOrButtonHeld(), bleMouse.isConnected(), micros());
    xSemaphoreGive(airMouseMutex);
    updateTrackpad();
    updateHoldToggle();
    updateVirtualButtons();
  }
  reportAirMouse();
  updateClapClick();
  drawCoreStatus();
  delay(1);
}

#endif
