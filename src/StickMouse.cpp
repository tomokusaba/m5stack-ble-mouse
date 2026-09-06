#if defined(TARGET_M5STICKS3)

#include <Arduino.h>
#include <M5Unified.h>
#include <BleMouse.h>
#include <freertos/semphr.h>
#include "AirMouse.h"
#include "AirMouseImu.h"
#include "StickButtons.h"

namespace {

namespace air = air_mouse;
constexpr uint32_t kButtonPollUs = 5000;
constexpr uint32_t kStatusIntervalMs = 200;
constexpr uint32_t kClickIndicatorMs = 300;
constexpr uint8_t kDisplayRotation = 0;
constexpr uint8_t kDisplayBrightness = 90;

static_assert(pdMS_TO_TICKS(air::kImuPollMs) > 0, "IMU polling requires a <=2ms RTOS tick");

air::Controller airMouse;
stick_buttons::Controller buttons;
stick_buttons::Actions controls;
SemaphoreHandle_t imuMutex = nullptr;
BleMouse bleMouse("M5StickS3 IMU Mouse");
bool imuHealthy = true;
uint32_t sampleRateHz = 0;
uint32_t lastButtonUs = 0, lastReportUs = 0, lastStatusMs = 0, lastClickMs = 0;
const char* clickIndicator = "";

void fatal(const char* message) {
  Serial.println(message);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.setCursor(4, 4);
  M5.Display.println(message);
  while (true) {
    delay(1000);
  }
}

void sampleImuTask(void*) {
  TickType_t wake = xTaskGetTickCount();
  uint32_t lastGoodUs = micros(), lastAccelUs = 0, rateWindowUs = lastGoodUs;
  uint32_t sampleCount = 0;
  air::Vec3 accel;
  bool haveAccel = false;
  for (;;) {
    // Serialize M5Unified access and controller state between the two tasks.
    xSemaphoreTake(imuMutex, portMAX_DELAY);
    const auto updated = M5.Imu.update();
    const uint32_t now = micros();
    const auto data = M5.Imu.getImuData();
    if (updated & m5::IMU_Class::sensor_mask_accel) {
      accel = air::stickToPointerFrame({data.accel.x, data.accel.y, data.accel.z});
      haveAccel = true;
      lastAccelUs = now;
    }
    if ((updated & m5::IMU_Class::sensor_mask_gyro) && haveAccel &&
        now - lastAccelUs <= air::kMaxSampleGapUs) {
      const auto gyro = air::stickToPointerFrame({data.gyro.x, data.gyro.y, data.gyro.z});
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
      sampleRateHz = static_cast<uint32_t>(
          static_cast<uint64_t>(sampleCount) * 1000000U / (now - rateWindowUs));
      sampleCount = 0;
      rateWindowUs = now;
    }
    xSemaphoreGive(imuMutex);
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(air::kImuPollMs));
  }
}

void click(uint8_t button, const char* indicator) {
  xSemaphoreTake(imuMutex, portMAX_DELAY);
  airMouse.freeze(micros());
  xSemaphoreGive(imuMutex);
  if (bleMouse.isConnected() && !bleMouse.isPressed(button)) {
    // Preserve an A-button drag while clicking B.
    bleMouse.press(button);
    bleMouse.release(button);
    clickIndicator = indicator;
    lastClickMs = millis();
  }
}

void updateButtons() {
  const uint32_t now = micros();
  if (now - lastButtonUs < kButtonPollUs) {
    return;
  }
  lastButtonUs = now;
  const bool connected = bleMouse.isConnected();
  xSemaphoreTake(imuMutex, portMAX_DELAY);
  M5.update();
  const auto next = buttons.update(M5.BtnA.isPressed(), M5.BtnB.isPressed(),
                                   connected, millis());
  const uint32_t inputUs = micros();
  if (next.leftHeld != controls.leftHeld || next.scroll != controls.scroll) {
    airMouse.freeze(inputUs);
  }
  airMouse.setScrollMode(next.scroll);
  airMouse.setInput(next.freezePointer, connected, inputUs);
  xSemaphoreGive(imuMutex);

  if (next.leftHeld != controls.leftHeld) {
    if (connected && next.leftHeld) {
      bleMouse.press(MOUSE_LEFT);
    } else {
      bleMouse.release(MOUSE_LEFT);
    }
  }
  controls = next;
  if (controls.leftClick) {
    click(MOUSE_LEFT, "LEFT");
  }
  if (controls.rightClick) {
    click(MOUSE_RIGHT, "RIGHT");
  }
  if (controls.middleClick) {
    click(MOUSE_MIDDLE, "MIDDLE");
  }
}

void reportMotion() {
  const uint32_t now = micros();
  if (now - lastReportUs < air::kReportIntervalUs) {
    return;
  }
  lastReportUs = now;
  const bool connected = bleMouse.isConnected();
  int dx = 0, dy = 0, wheel = 0;
  xSemaphoreTake(imuMutex, portMAX_DELAY);
  const uint32_t reportUs = micros();
  airMouse.setInput(controls.freezePointer, connected, reportUs);
  if (imuHealthy && !airMouse.blocked(reportUs)) {
    dx = air::PixelAccumulator::take(airMouse.pending.x);
    dy = air::PixelAccumulator::take(airMouse.pending.y);
    wheel = air::PixelAccumulator::take(airMouse.pending.wheel);
  } else {
    airMouse.clearMotion();
  }
  xSemaphoreGive(imuMutex);
  if (connected && (dx || dy || wheel)) {
    bleMouse.move(static_cast<signed char>(dx), static_cast<signed char>(dy),
                  static_cast<signed char>(wheel));
  }
}

void drawStatus() {
  const uint32_t nowMs = millis();
  if (nowMs - lastStatusMs < kStatusIntervalMs) {
    return;
  }
  lastStatusMs = nowMs;
  xSemaphoreTake(imuMutex, portMAX_DELAY);
  const bool calibrated = airMouse.calibrated;
  const bool paused = !airMouse.pointingEnabled;
  const bool frozen = airMouse.blocked(micros());
  const bool healthy = imuHealthy;
  const bool fallback = airMouse.gravityFallback;
  const uint32_t retries = airMouse.calibrationRetries, hz = sampleRateHz;
  xSemaphoreGive(imuMutex);
  if (nowMs - lastClickMs >= kClickIndicatorMs) {
    clickIndicator = "";
  }
  const char* mode = !healthy ? "IMU ERROR" : !calibrated ? "CALIBRATING" :
                     paused ? "PAUSED" : frozen ? "FREEZE" :
                     controls.scroll ? "SCROLL" : controls.leftHeld ? "DRAG" : "POINT";
  M5.Display.fillRect(0, 0, M5.Display.width(), 154, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(healthy ? TFT_GREEN : TFT_RED, TFT_BLACK);
  M5.Display.setCursor(4, 4);
  M5.Display.printf("StickS3 AIR MOUSE\n\nBLE: %s\n\n%s\n\n",
                    bleMouse.isConnected() ? "ON" : "WAIT", mode);
  if (!calibrated) {
    M5.Display.printf("Calibrating...\nKeep still ~1 sec\nRetry: %lu\n",
                      static_cast<unsigned long>(retries));
  } else {
    M5.Display.printf("%lu Hz / %s\n", static_cast<unsigned long>(hz),
                      fallback ? "BODY" : "ROLL COMP");
  }
  M5.Display.setCursor(4, 108);
  M5.Display.printf("A:%s B:%s\n\nCLICK: %s", controls.leftHeld ? "DRAG" : "--",
                    controls.scroll ? "SCROLL" : "--", clickIndicator);
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_imu = true;
  config.internal_mic = false;
  config.internal_spk = false;
  // Leave the hardware RESET/PWR button to its factory functions.
  config.pmic_button = false;
  M5.begin(config);
  M5.Display.setRotation(kDisplayRotation);
  M5.Display.setBrightness(kDisplayBrightness);
  M5.Display.fillScreen(TFT_BLACK);
  M5.BtnA.setDebounceThresh(stick_buttons::kDebounceMs);
  M5.BtnB.setDebounceThresh(stick_buttons::kDebounceMs);
  if (M5.getBoard() != m5::board_t::board_M5StickS3 || !configureAirMouseImu()) {
    fatal("StickS3 BMI270\nconfiguration failed");
  }
  imuMutex = xSemaphoreCreateMutex();
  if (!imuMutex) {
    fatal("IMU mutex failed");
  }
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setCursor(4, 158);
  M5.Display.print("A: left / hold drag\n\nB: right / 2x middle\n\nB hold: tip scroll\n\nShake: pause/resume");
  bleMouse.begin();
  lastStatusMs = millis() - kStatusIntervalMs;
  drawStatus();
  if (xTaskCreate(sampleImuTask, "stick-imu", 4096, nullptr, 2, nullptr) != pdPASS) {
    fatal("IMU task failed");
  }
}

void loop() {
  updateButtons();
  reportMotion();
  drawStatus();
  delay(1);
}

#endif
