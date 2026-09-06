#pragma once

#include "AirMouse.h"
#include <limits>

namespace stick_calibration {

constexpr uint32_t kMinimumDurationUs = 1000000;
constexpr uint32_t kAttemptDurationUs = 1500000;
constexpr uint32_t kMinimumSamples = 200;
constexpr uint32_t kMaximumAttempts = 3;
constexpr float kGyroStdDevDps = 0.8f;
constexpr float kAccelStdDevG = 0.04f;
constexpr float kGravityToleranceG = 0.15f;
constexpr float kMaximumMeanRateDps = 5.0f;

// Fixed wall-clock windows cannot be restarted forever by a single noisy sample.
class Calibration {
 public:
  bool ready = false;
  bool degraded = false;
  uint32_t failedAttempts = 0;
  air_mouse::Vec3 bias, gravity;

  void begin(uint32_t now) {
    *this = Calibration{};
    windowStartUs_ = now;
  }

  void add(air_mouse::Vec3 gyro, air_mouse::Vec3 accel) {
    if (!ready && air_mouse::finite(gyro) && air_mouse::finite(accel) &&
        air_mouse::length(accel) > 0.1f) {
      gyroWindow_.add(gyro);
      accelWindow_.add(accel);
      lastAccel_ = accel;
    }
  }

  uint32_t remainingUs(uint32_t now) const {
    const uint32_t elapsed = now - windowStartUs_;
    return ready || elapsed >= kAttemptDurationUs ? 0 : kAttemptDurationUs - elapsed;
  }

  bool noData() const { return !ready && failedAttempts >= kMaximumAttempts; }

  void poll(uint32_t now) {
    if (ready) {
      return;
    }
    const uint32_t elapsed = now - windowStartUs_;
    const bool steady = gyroWindow_.count >= kMinimumSamples &&
                        gyroWindow_.stable(kGyroStdDevDps) &&
                        accelWindow_.stable(kAccelStdDevG) &&
                        air_mouse::length(gyroWindow_.mean) <= kMaximumMeanRateDps &&
                        std::fabs(air_mouse::length(accelWindow_.mean) - 1) <= kGravityToleranceG;
    if (elapsed >= kMinimumDurationUs && steady) {
      bias = gyroWindow_.mean;
      gravity = accelWindow_.mean;
      ready = true;
      return;
    }
    if (elapsed < kAttemptDurationUs) {
      return;
    }
    // Rank complete attempts by gyro/accel variation and gravity consistency.
    if (gyroWindow_.count > 0) {
      const float divisor = static_cast<float>(std::max(1U, gyroWindow_.count - 1));
      const auto g = gyroWindow_.m2, a = accelWindow_.m2;
      const float score = (g.x + g.y + g.z + 100 * (a.x + a.y + a.z)) / divisor +
                          10 * std::fabs(air_mouse::length(accelWindow_.mean) - 1);
      if (score < bestScore_) {
        bestScore_ = score;
        bias = gyroWindow_.mean;
        // A tumbling device can have a zero average of otherwise valid acceleration.
        gravity = air_mouse::length(accelWindow_.mean) > 0.1f ? accelWindow_.mean : lastAccel_;
        haveBest_ = true;
      }
    }
    failedAttempts = std::min(kMaximumAttempts, failedAttempts + 1);
    if (failedAttempts >= kMaximumAttempts && haveBest_) {
      ready = degraded = true;
    }
    // No samples is a sensor fault, not a usable zero-bias calibration.
    gyroWindow_ = {};
    accelWindow_ = {};
    windowStartUs_ = now;
  }

 private:
  air_mouse::Moments gyroWindow_, accelWindow_;
  air_mouse::Vec3 lastAccel_;
  uint32_t windowStartUs_ = 0;
  float bestScore_ = std::numeric_limits<float>::max();
  bool haveBest_ = false;
};

}  // namespace stick_calibration
