#if defined(TARGET_M5STICKS3)

#include <Arduino.h>
#include <M5Unified.h>
#include <BleMouse.h>
#include <freertos/semphr.h>
#include "AirMouse.h"
#include "AirMouseImu.h"
#include "StickButtons.h"
#include "StickCalibration.h"

namespace {

namespace air = air_mouse;
constexpr uint32_t kButtonPollUs = 5000;
constexpr uint32_t kStatusIntervalMs = 200;
constexpr uint32_t kClickIndicatorMs = 300;
constexpr uint8_t kDisplayRotation = 0;
constexpr uint8_t kDisplayBrightness = 90;

static_assert(pdMS_TO_TICKS(air::kImuPollMs) > 0, "IMU polling requires a <=2ms RTOS tick");

air::Controller airMouse;
stick_calibration::Calibration bootCalibration;
stick_buttons::Controller buttons;
stick_buttons::Actions controls;
SemaphoreHandle_t imuMutex = nullptr;
BleMouse bleMouse("M5StickS3 IMU Mouse");
bool imuHealthy = false;
uint32_t sampleRateHz = 0;
uint32_t lastButtonUs = 0, lastReportUs = 0, lastStatusMs = 0, lastClickMs = 0;
const char* clickIndicator = "";
const char* imuSetupError = nullptr;
struct ImuDiagnostics {
  uint8_t address = 0, chipId = 0, init = 0, error = 0, power = 0, config[4] = {};
  air::Vec3 nativeGyro;
  float accelNorm = 0;
  uint32_t gyroHz = 0, accelHz = 0, lastGoodUs = 0, maxReadUs = 0, maxWaitUs = 0, overruns = 0;
  bool haveSample = false;
} diagnostics;
uint32_t motionReports = 0;

bool readImuRegisters() {
  using Bmi = m5::BMI270_Class;
  auto* sensor = M5.Imu.getImuInstancePtr(0);
  if (!sensor) {
    return false;
  }
  diagnostics.address = sensor->getAddress();
  return sensor->readRegister(Bmi::CHIP_ID_ADDR, &diagnostics.chipId, 1) &&
         sensor->readRegister(Bmi::INTERNAL_STATUS_ADDR, &diagnostics.init, 1) &&
         sensor->readRegister(Bmi::ERR_REG_ADDR, &diagnostics.error, 1) &&
         sensor->readRegister(Bmi::PWR_CTRL_ADDR, &diagnostics.power, 1) &&
         sensor->readRegister(Bmi::ACC_CONF_ADDR, diagnostics.config, 4);
}

const char* initializeImu() {
  if (M5.getBoard() != m5::board_t::board_M5StickS3) {
    return "WRONG BOARD";
  }
  if (!M5.In_I2C.isEnabled() || !M5.Imu.isEnabled()) {
    return "IMU DISABLED";
  }
  if (M5.Imu.getType() != m5::imu_bmi270) {
    return "WRONG IMU TYPE";
  }
  if (!readImuRegisters()) {
    return "I2C READ FAILED";
  }
  if (diagnostics.chipId != 0x24) {
    return "CHIP ID ERROR";
  }
  // isEnabled() only identifies a driver; it does not prove the config blob loaded.
  if ((diagnostics.init & 0x0F) != 0x01) {
    return "BMI INIT FAILED";
  }
  if ((diagnostics.power & 0x06) != 0x06) {
    return "ACC/GYR POWER OFF";
  }
  const bool configured = configureAirMouseImu();
  delay(5);
  if (!readImuRegisters()) {
    return "I2C READ FAILED";
  }
  if (!configured) {
    return "ODR CONFIG FAILED";
  }
  // Fatal/config errors matter; AUX/FIFO flags do not gate this accel/gyro-only path.
  if (diagnostics.error & 0x1F) {
    return "BMI CONFIG ERROR";
  }
  return nullptr;
}

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
  uint32_t sampleCount = 0, gyroCount = 0, accelCount = 0;
  const TickType_t period = pdMS_TO_TICKS(air::kImuPollMs);
  air::Vec3 accel;
  bool haveAccel = false;
  for (;;) {
    char event[160] = {};
    const uint32_t lockStartUs = micros();
    // Serialize M5Unified access and controller state between the two tasks.
    xSemaphoreTake(imuMutex, portMAX_DELAY);
    const uint32_t readStartUs = micros();
    diagnostics.maxWaitUs = std::max(diagnostics.maxWaitUs, readStartUs - lockStartUs);
    const auto updated = M5.Imu.update();
    const uint32_t now = micros();
    const auto data = M5.Imu.getImuData();
    diagnostics.maxReadUs = std::max(diagnostics.maxReadUs,
                                   static_cast<uint32_t>(micros() - readStartUs));
    if (updated & m5::IMU_Class::sensor_mask_accel) {
      ++accelCount;
      accel = air::stickToPointerFrame({data.accel.x, data.accel.y, data.accel.z});
      diagnostics.accelNorm = air::length(accel);
      haveAccel = true;
      lastAccelUs = now;
    }
    if (updated & m5::IMU_Class::sensor_mask_gyro) {
      ++gyroCount;
      diagnostics.nativeGyro = {data.gyro.x, data.gyro.y, data.gyro.z};
    }
    if ((updated & m5::IMU_Class::sensor_mask_gyro) && haveAccel &&
        now - lastAccelUs <= air::kMaxSampleGapUs) {
      const auto gyro = air::stickToPointerFrame({data.gyro.x, data.gyro.y, data.gyro.z});
      if (air::finite(accel) && air::finite(gyro) && air::length(accel) > 0.1f) {
        if (airMouse.calibrated) {
          airMouse.sample(gyro, accel, now);
        } else {
          bootCalibration.add(gyro, accel);
        }
        lastGoodUs = now;
        diagnostics.lastGoodUs = now;
        diagnostics.haveSample = true;
        ++sampleCount;
        if (!imuHealthy) {
          snprintf(event, sizeof(event), "IMU samples recovered");
        }
        imuHealthy = true;
      }
    }
    if (!airMouse.calibrated) {
      const auto previousAttempts = bootCalibration.failedAttempts;
      bootCalibration.poll(now);
      if (bootCalibration.ready) {
        airMouse.bias = bootCalibration.bias;
        airMouse.gravity = bootCalibration.gravity;
        airMouse.calibrated = true;
        airMouse.clearMotion();
        snprintf(event, sizeof(event), "Calibration %s: pointer-frame bias %.3f %.3f %.3f dps",
                 bootCalibration.degraded ? "WARNING: best available" : "OK",
                 airMouse.bias.x, airMouse.bias.y, airMouse.bias.z);
      } else if (previousAttempts != bootCalibration.failedAttempts) {
        snprintf(event, sizeof(event), "Calibration attempt %lu failed%s",
                 static_cast<unsigned long>(bootCalibration.failedAttempts),
                 bootCalibration.noData() ? ": NO VALID SENSOR DATA" : ": retrying");
      }
    }
    if (now - lastGoodUs > air::kMaxSampleGapUs) {
      if (imuHealthy) {
        snprintf(event, sizeof(event), "IMU samples missing: pointing stopped");
      }
      imuHealthy = false;
      airMouse.clearMotion();
    }
    if (now - rateWindowUs >= 1000000U) {
      diagnostics.gyroHz = static_cast<uint64_t>(gyroCount) * 1000000U / (now - rateWindowUs);
      diagnostics.accelHz = static_cast<uint64_t>(accelCount) * 1000000U / (now - rateWindowUs);
      sampleRateHz = static_cast<uint32_t>(
          static_cast<uint64_t>(sampleCount) * 1000000U / (now - rateWindowUs));
      sampleCount = gyroCount = accelCount = 0;
      rateWindowUs = now;
    }
    const bool late = xTaskGetTickCount() - wake >= period;
    if (late) {
      ++diagnostics.overruns;
    }
    xSemaphoreGive(imuMutex);
    if (event[0]) {
      Serial.println(event);
    }
    if (late) {
      // Do not spin at priority 2 trying to catch up after a slow I2C transaction.
      wake = xTaskGetTickCount();
      vTaskDelay(1);
    } else {
      vTaskDelayUntil(&wake, period);
    }
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
    ++motionReports;
  }
}

void drawStatus() {
  const uint32_t nowMs = millis();
  if (nowMs - lastStatusMs < kStatusIntervalMs) {
    return;
  }
  lastStatusMs = nowMs;
  xSemaphoreTake(imuMutex, portMAX_DELAY);
  const uint32_t nowUs = micros();
  const bool calibrated = airMouse.calibrated;
  const bool paused = !airMouse.pointingEnabled;
  const bool frozen = airMouse.freezeActive(nowUs);
  const bool healthy = imuHealthy;
  const bool fallback = airMouse.gravityFallback;
  const uint32_t hz = sampleRateHz;
  const auto boot = bootCalibration;
  const auto diag = diagnostics;
  const air::Vec3 bias(-airMouse.bias.y, airMouse.bias.x, airMouse.bias.z);
  xSemaphoreGive(imuMutex);
  if (nowMs - lastClickMs >= kClickIndicatorMs) {
    clickIndicator = "";
  }
  const bool connected = bleMouse.isConnected();
  const char* gate = imuSetupError ? imuSetupError : !healthy ? "NO SAMPLES" :
                     !calibrated ? "CALIBRATING" : !connected ? "BLE WAIT" :
                     paused ? "PAUSED" : frozen ? "FREEZE" : "READY";
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextFont(1);
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(false);
  M5.Display.setTextColor(healthy ? TFT_GREEN : TFT_RED, TFT_BLACK);
  M5.Display.setCursor(1, 1);
  M5.Display.printf("StickS3 %s %s\nID:%02X @%02X TYPE:%d\n",
                    M5.Imu.getType() == m5::imu_bmi270 ? "BMI270" : "UNKNOWN",
                    healthy ? "OK" : "OFF", diag.chipId, diag.address, M5.Imu.getType());
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.printf("I2C%d SDA%d SCL%d\nIN:%02X ER:%02X P:%02X\nCFG:%02X/%02X/%02X/%02X\n",
                    M5.In_I2C.getPort(), M5.In_I2C.getSDA(), M5.In_I2C.getSCL(),
                    diag.init, diag.error, diag.power,
                    diag.config[0], diag.config[1], diag.config[2], diag.config[3]);
  M5.Display.printf("BLE:%s Hz:%lu\nG:%lu A:%lu\n",
                    connected ? "ON" : "WAIT", static_cast<unsigned long>(hz),
                    static_cast<unsigned long>(diag.gyroHz), static_cast<unsigned long>(diag.accelHz));
  M5.Display.setTextColor(boot.degraded || boot.noData() ? TFT_YELLOW : TFT_CYAN, TFT_BLACK);
  if (imuSetupError) {
    M5.Display.print("CAL: SENSOR ERROR\n");
  } else if (!calibrated) {
    M5.Display.printf("Calibrating %lu/%lu %.1fs\n",
                      static_cast<unsigned long>(std::min(boot.failedAttempts + 1,
                                                         stick_calibration::kMaximumAttempts)),
                      static_cast<unsigned long>(stick_calibration::kMaximumAttempts),
                      boot.remainingUs(nowUs) * 0.000001f);
    if (boot.noData()) {
      M5.Display.print("NO VALID DATA\n");
    }
  } else {
    M5.Display.println(boot.degraded ? "CAL:WARN best bias" : "CAL:OK");
  }
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.printf("\nGyro dps (native)\nX:%+8.2f\nY:%+8.2f\nZ:%+8.2f\n",
                    diag.nativeGyro.x, diag.nativeGyro.y, diag.nativeGyro.z);
  M5.Display.printf("Bias dps (native)\nX:%+8.3f\nY:%+8.3f\nZ:%+8.3f\n",
                    bias.x, bias.y, bias.z);
  M5.Display.printf("Accel:%.3fg\n", diag.accelNorm);
  if (diag.haveSample) {
    M5.Display.printf("Age:%lums\n", static_cast<unsigned long>((nowUs - diag.lastGoodUs) / 1000));
  } else {
    M5.Display.print("Age: no samples\n");
  }
  M5.Display.printf("Paused:%d Freeze:%d\nGATE:%s\nA:%d B:%d DRAG:%d\n",
                    paused, frozen, gate, M5.BtnA.isPressed(), M5.BtnB.isPressed(), controls.leftHeld);
  M5.Display.printf("%s %s CLK:%s\nTX:%lu\nRead:%luus\nLock:%luus\nLate:%lu\n",
                    controls.scroll ? "SCROLL" : "POINT", fallback ? "BODY" : "ROLL",
                    clickIndicator, static_cast<unsigned long>(motionReports),
                    static_cast<unsigned long>(diag.maxReadUs),
                    static_cast<unsigned long>(diag.maxWaitUs),
                    static_cast<unsigned long>(diag.overruns));
  if (boot.degraded) {
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.print("Reset at rest to retry");
  }
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_imu = true;
  config.external_imu = false;
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
  imuSetupError = initializeImu();
  Serial.printf("StickS3 board=%d IMU=%d enabled=%d internal I2C%d SDA%d SCL%d\n",
                M5.getBoard(), M5.Imu.getType(), M5.Imu.isEnabled(),
                M5.In_I2C.getPort(), M5.In_I2C.getSDA(), M5.In_I2C.getSCL());
  Serial.printf("BMI270 @%02X ID=%02X INIT=%02X ERR=%02X PWR=%02X CFG=%02X/%02X/%02X/%02X: %s\n",
                diagnostics.address, diagnostics.chipId, diagnostics.init,
                diagnostics.error, diagnostics.power, diagnostics.config[0],
                diagnostics.config[1], diagnostics.config[2], diagnostics.config[3],
                imuSetupError ? imuSetupError : "OK");
  imuMutex = xSemaphoreCreateMutex();
  if (!imuMutex) {
    fatal("IMU mutex failed");
  }
  bleMouse.begin();
  bootCalibration.begin(micros());
  lastStatusMs = millis() - kStatusIntervalMs;
  drawStatus();
  if (!imuSetupError &&
      xTaskCreate(sampleImuTask, "stick-imu", 4096, nullptr, 2, nullptr) != pdPASS) {
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
