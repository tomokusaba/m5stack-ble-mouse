#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace air_mouse {

constexpr uint32_t kImuPollMs = 2;
constexpr uint32_t kReportIntervalUs = 8000;
constexpr uint32_t kMaxSampleGapUs = 30000;
constexpr uint32_t kTouchFreezeUs = 150000;
constexpr float kPixelsPerDegree = 22.0f;
constexpr int kCursorXSign = -1;
constexpr int kCursorYSign = -1;
constexpr float kRateDeadzoneDps = 1.0f;
constexpr float kRateKneeDps = 1.0f;
constexpr float kRateFilterAlpha = 0.65f;
constexpr float kAcceleration = 0.6f;
constexpr float kAccelerationFullScaleDps = 200.0f;
constexpr float kGravityTimeConstantS = 0.15f;
constexpr float kGravityToleranceG = 0.20f;
constexpr float kMinHorizontalProjection = 0.10f;
constexpr uint32_t kCalibrationDurationUs = 1000000;
constexpr uint32_t kCalibrationSamples = 400;
constexpr float kCalibrationMaxRateDps = 5.0f;
constexpr float kCalibrationGyroStdDevDps = 0.25f;
constexpr float kCalibrationAccelStdDevG = 0.015f;
constexpr float kSteadyGravityToleranceG = 0.06f;
constexpr float kSteadyAccelDeltaG = 0.025f;
constexpr float kBiasTrackingMaxRateDps = 1.5f;
constexpr uint32_t kBiasStillTimeUs = 500000;
constexpr float kBiasTimeConstantS = 30.0f;
constexpr float kShakeThresholdG = 3.0f;
constexpr uint32_t kShakeSamples = 2;
constexpr uint32_t kShakeCooldownUs = 1200000;
constexpr uint32_t kShakeQuietUs = 400000;
constexpr float kScrollStepsPerDegree = 0.35f;
constexpr int kScrollSign = 1;

static_assert(kCursorXSign == 1 || kCursorXSign == -1, "Invalid X sign");
static_assert(kCursorYSign == 1 || kCursorYSign == -1, "Invalid Y sign");
static_assert(kRateFilterAlpha >= 0.5f && kRateFilterAlpha <= 1.0f, "Use a light filter");
static_assert(kPixelsPerDegree > 0 && kRateKneeDps > 0, "Invalid motion gain");
static_assert(kScrollSign == 1 || kScrollSign == -1, "Invalid scroll sign");
static_assert(kScrollStepsPerDegree > 0, "Invalid scroll gain");

struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3() = default;
  Vec3(float xValue, float yValue, float zValue) : x(xValue), y(yValue), z(zValue) {}
  Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
  Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

// StickS3 native +X points to USB, +Y to screen right, +Z out of the screen.
inline Vec3 stickToPointerFrame(Vec3 native) {
  return {native.y, -native.x, native.z};
}

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }
inline bool finite(Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

struct Rates {
  float yaw, pitch;
};

inline Rates projectRates(Vec3 omega, Vec3 gravity, bool validGravity) {
  const float norm = length(gravity);
  if (!validGravity || norm < 0.1f) {
    return {omega.z, omega.x};
  }
  // At rest the accelerometer measures UP (specific force), not downward gravity.
  const Vec3 up = gravity * (1.0f / norm);
  const Vec3 forward(0, 1, 0);
  const Vec3 horizontal = forward - up * dot(forward, up);
  const float horizontalLength = length(horizontal);
  if (horizontalLength < kMinHorizontalProjection) {
    return {omega.z, omega.x};
  }
  const Vec3 right = cross(horizontal * (1.0f / horizontalLength), up);
  return {dot(omega, up), dot(omega, right)};
}

inline float softenRate(float rate) {
  const float excess = std::fabs(rate) - kRateDeadzoneDps;
  if (excess <= 0) {
    return 0;
  }
  const float value = excess < kRateKneeDps
                          ? excess * excess / (2.0f * kRateKneeDps)
                          : excess - kRateKneeDps * 0.5f;
  return std::copysign(value, rate);
}

struct PixelAccumulator {
  float x = 0, y = 0, wheel = 0;
  static int take(float& value) {
    const int delta = static_cast<int>(std::max(-127.0f, std::min(127.0f, value)));
    value -= delta;
    return delta;
  }
  void clear() { x = y = wheel = 0; }
};

struct Moments {
  uint32_t count = 0;
  Vec3 mean, m2;
  void add(Vec3 value) {
    ++count;
    const Vec3 delta = value - mean;
    mean = mean + delta * (1.0f / count);
    const Vec3 residual = value - mean;
    m2 = m2 + Vec3(delta.x * residual.x, delta.y * residual.y, delta.z * residual.z);
  }
  bool stable(float maxStdDev) const {
    const float limit = maxStdDev * maxStdDev * (count - 1);
    return count > 1 && m2.x <= limit && m2.y <= limit && m2.z <= limit;
  }
};

class Controller {
 public:
  bool calibrated = false;
  bool pointingEnabled = true;
  bool gravityFallback = false;
  uint32_t calibrationRetries = 0;
  Vec3 bias, gravity;
  PixelAccumulator pending;

  void setScrollMode(bool enabled) {
    if (scrollMode_ != enabled) {
      clearMotion();
    }
    scrollMode_ = enabled;
  }

  void clearMotion() {
    pending.clear();
    filteredYaw_ = filteredPitch_ = 0;
  }

  void freeze(uint32_t now) {
    hasInteraction_ = true;
    lastInteractionUs_ = now;
    clearMotion();
  }

  void setInput(bool touchingOrButtonHeld, bool connected, uint32_t now) {
    if (touchingOrButtonHeld || interacting_) {
      freeze(now);
    }
    interacting_ = touchingOrButtonHeld;
    connected_ = connected;
    if (!connected_) {
      clearMotion();
    }
  }

  bool blocked(uint32_t now) {
    // Retire elapsed windows so they cannot reactivate on a later micros() wrap.
    if (hasInteraction_ && now - lastInteractionUs_ >= kTouchFreezeUs) {
      hasInteraction_ = false;
    }
    if (hasShaken_ && now - lastShakeUs_ >= kShakeQuietUs) {
      hasShaken_ = false;
    }
    return !calibrated || !connected_ || !pointingEnabled || interacting_ ||
           hasInteraction_ || hasShaken_;
  }

  void sample(Vec3 rawGyro, Vec3 accel, uint32_t now) {
    const uint32_t elapsedUs = now - previousSampleUs_;
    const bool gap = !hasSample_ || elapsedUs > kMaxSampleGapUs;
    previousSampleUs_ = now;
    hasSample_ = true;
    if (!finite(rawGyro) || !finite(accel)) {
      clearMotion();
      stillUs_ = 0;
      if (!calibrated) {
        resetCalibrationWindow();
      }
      return;
    }
    if (gap) {
      clearMotion();
      stillUs_ = 0;
      if (!calibrated) {
        resetCalibrationWindow();
      }
    }
    if (!calibrated) {
      calibrate(rawGyro, accel, now);
      previousAccel_ = accel;
      return;
    }

    const float dt = elapsedUs * 0.000001f;
    Vec3 omega = rawGyro - bias;
    const float accelNorm = length(accel);
    const bool validGravity = std::fabs(accelNorm - 1.0f) <= kGravityToleranceG;
    const bool steady = std::fabs(accelNorm - 1.0f) <= kSteadyGravityToleranceG &&
                        length(accel - previousAccel_) <= kSteadyAccelDeltaG &&
                        length(accel - gravity) <= kSteadyGravityToleranceG;
    previousAccel_ = accel;
    if (gap) {
      if (validGravity) {
        gravity = accel;
      }
      return;
    }
    if (steady && length(omega) < kBiasTrackingMaxRateDps) {
      stillUs_ = std::min(kBiasStillTimeUs, stillUs_ + elapsedUs);
      if (stillUs_ >= kBiasStillTimeUs) {
        bias = bias + omega * (dt / (kBiasTimeConstantS + dt));
        omega = rawGyro - bias;
      }
    } else {
      stillUs_ = 0;
    }
    if (validGravity) {
      gravity = gravity + (accel - gravity) * (dt / (kGravityTimeConstantS + dt));
    }
    gravityFallback = !validGravity ||
                      length(cross(Vec3(0, 1, 0), gravity)) <
                          kMinHorizontalProjection * length(gravity);

    updateShake(accelNorm, elapsedUs, now);
    if (blocked(now)) {
      clearMotion();
      return;
    }
    const Rates rates = projectRates(omega, gravity, validGravity);
    const float yaw = softenRate(rates.yaw);
    const float pitch = softenRate(rates.pitch);
    // Drop the filter tail inside the deadzone, but retain sub-pixel displacement.
    filteredYaw_ = yaw == 0 ? 0 : filteredYaw_ + kRateFilterAlpha * (yaw - filteredYaw_);
    filteredPitch_ = pitch == 0 ? 0 : filteredPitch_ + kRateFilterAlpha * (pitch - filteredPitch_);
    const float rate = std::sqrt(rates.yaw * rates.yaw + rates.pitch * rates.pitch);
    const float gain = kPixelsPerDegree *
                       (1.0f + kAcceleration * std::min(rate / kAccelerationFullScaleDps, 1.0f));
    if (scrollMode_) {
      pending.wheel += kScrollSign * kScrollStepsPerDegree * filteredPitch_ * dt;
    } else {
      pending.x += kCursorXSign * gain * filteredYaw_ * dt;
      pending.y += kCursorYSign * gain * filteredPitch_ * dt;
    }
  }

 private:
  Moments gyroWindow_, accelWindow_;
  Vec3 firstAccel_, previousAccel_;
  uint32_t windowStartUs_ = 0, previousSampleUs_ = 0, stillUs_ = 0;
  uint32_t lastInteractionUs_ = 0, lastShakeUs_ = 0, quietUs_ = 0, shakeSamples_ = 0;
  float filteredYaw_ = 0, filteredPitch_ = 0;
  bool hasSample_ = false, connected_ = false, interacting_ = false;
  bool hasInteraction_ = false, hasShaken_ = false, shakeArmed_ = true;
  bool scrollMode_ = false;

  void resetCalibrationWindow() {
    if (gyroWindow_.count != 0) {
      ++calibrationRetries;
    }
    gyroWindow_ = {};
    accelWindow_ = {};
  }

  void calibrate(Vec3 gyro, Vec3 accel, uint32_t now) {
    if (std::fabs(length(accel) - 1.0f) > kSteadyGravityToleranceG ||
        length(gyro) > kCalibrationMaxRateDps) {
      resetCalibrationWindow();
      return;
    }
    if (gyroWindow_.count == 0) {
      windowStartUs_ = now;
      firstAccel_ = accel;
    }
    if (length(accel - firstAccel_) > kSteadyGravityToleranceG) {
      resetCalibrationWindow();
      return;
    }
    gyroWindow_.add(gyro);
    accelWindow_.add(accel);
    if (gyroWindow_.count < kCalibrationSamples || now - windowStartUs_ < kCalibrationDurationUs) {
      return;
    }
    if (gyroWindow_.stable(kCalibrationGyroStdDevDps) &&
        accelWindow_.stable(kCalibrationAccelStdDevG)) {
      bias = gyroWindow_.mean;
      gravity = accelWindow_.mean;
      calibrated = true;
      clearMotion();
    } else {
      resetCalibrationWindow();
    }
  }

  void updateShake(float accelNorm, uint32_t dtUs, uint32_t now) {
    if (std::fabs(accelNorm - 1.0f) <= kGravityToleranceG) {
      quietUs_ = std::min(kShakeQuietUs, quietUs_ + dtUs);
    } else {
      quietUs_ = 0;
    }
    if (!shakeArmed_ && quietUs_ >= kShakeQuietUs &&
        now - lastShakeUs_ >= kShakeCooldownUs) {
      shakeArmed_ = true;
    }
    if (accelNorm >= kShakeThresholdG && !interacting_) {
      ++shakeSamples_;
    } else {
      shakeSamples_ = 0;
    }
    if (shakeArmed_ && shakeSamples_ >= kShakeSamples) {
      pointingEnabled = !pointingEnabled;
      hasShaken_ = true;
      lastShakeUs_ = now;
      shakeArmed_ = false;
      shakeSamples_ = 0;
      clearMotion();
    }
  }
};

}  // namespace air_mouse
