#include "../include/AirMouse.h"
#include "../include/StickButtons.h"
#include <cassert>
#include <cstdio>

using namespace air_mouse;

bool near(float a, float b, float tolerance = 0.001f) {
  return std::fabs(a - b) <= tolerance;
}

uint32_t calibrate(Controller& c, Vec3 bias = {}, Vec3 gravity = {0, 0, 1},
                   uint32_t start = 10000) {
  c.setInput(false, true, start);
  uint32_t now = start;
  for (uint32_t i = 0; i <= kCalibrationSamples; ++i) {
    c.sample(bias, gravity, now);
    now += 2500;
  }
  assert(c.calibrated);
  assert(near(length(c.bias - bias), 0));
  return now - 2500;
}

void testDirections() {
  const Vec3 up(0, 0, 1);
  auto right = projectRates({0, 0, -20}, up, true);
  assert(kCursorXSign * right.yaw > 0 && near(right.pitch, 0));
  auto left = projectRates({0, 0, 20}, up, true);
  assert(kCursorXSign * left.yaw < 0);
  auto raise = projectRates({20, 0, 0}, up, true);
  assert(kCursorYSign * raise.pitch < 0 && near(raise.yaw, 0));
  assert(kCursorYSign * projectRates({-20, 0, 0}, up, true).pitch > 0);
  const auto roll = projectRates({0, 20, 0}, up, true);
  assert(near(roll.yaw, 0) && near(roll.pitch, 0));

  for (int degrees = -180; degrees <= 180; degrees += 15) {
    const float a = degrees * 3.14159265359f / 180.0f;
    // Express the same world yaw/pitch in a body frame rolled around forward (+Y).
    const Vec3 rolledUp(-std::sin(a), 0, std::cos(a));
    const Vec3 rolledRight(std::cos(a), 0, std::sin(a));
    auto yaw = projectRates(rolledUp * -20, rolledUp, true);
    auto pitch = projectRates(rolledRight * 20, rolledUp, true);
    assert(near(yaw.yaw, -20) && near(yaw.pitch, 0));
    assert(near(pitch.pitch, 20) && near(pitch.yaw, 0));
  }
  auto fallback = projectRates({12, 99, -20}, {0, 0, 2}, false);
  assert(near(fallback.yaw, -20) && near(fallback.pitch, 12));
  fallback = projectRates({12, 99, -20}, {0, 1, 0}, true);
  assert(near(fallback.yaw, -20) && near(fallback.pitch, 12));
}

void testCalibration() {
  Controller moving;
  moving.setInput(false, true, 0);
  for (uint32_t i = 0; i < 1200; ++i) {
    moving.sample({i % 2 ? 2.0f : -2.0f, 0, 0}, {0, 0, 1}, i * 2500);
  }
  assert(!moving.calibrated && moving.calibrationRetries >= 2);
  assert(moving.pending.x == 0 && moving.pending.y == 0);
  const Vec3 bias(0.4f, -0.3f, 0.6f);
  calibrate(moving, bias, {0, 0, 1}, 4000000);

  Controller shaking;
  for (uint32_t i = 0; i < 800; ++i) {
    shaking.sample({}, {0, 0, i % 2 ? 1.0f : 1.5f}, i * 2500);
  }
  assert(!shaking.calibrated);
  Controller rotating;
  for (uint32_t i = 0; i < 800; ++i) {
    rotating.sample({0, 0, 6}, {0, 0, 1}, i * 2500);
  }
  assert(!rotating.calibrated);
}

void testStaticTiltAndBias() {
  Controller c;
  const Vec3 bias(0.3f, -0.2f, 0.4f);
  uint32_t now = calibrate(c, bias);
  for (int i = 0; i < 1000; ++i) {
    c.sample(bias, {0.6f, 0, 0.8f}, now += 2500);
  }
  assert(c.pending.x == 0 && c.pending.y == 0);
  for (int i = 0; i < 4000; ++i) {
    c.sample(bias + Vec3(0, 0, 0.5f), {0.6f, 0, 0.8f}, now += 2500);
  }
  assert(c.bias.z > bias.z && c.bias.z < bias.z + 0.5f);
  assert(c.pending.x == 0 && c.pending.y == 0);
  const auto tracked = c.bias;
  for (int i = 0; i < 400; ++i) {
    c.sample(bias + Vec3(0, 0, -20), {0.6f, 0, 0.8f}, now += 2500);
  }
  assert(near(length(c.bias - tracked), 0));
  for (int i = 0; i < 400; ++i) {
    c.sample(bias, {0, 0, 1.5f}, now += 2500);
  }
  assert(near(length(c.bias - tracked), 0));
}

void testSoftKnee() {
  assert(softenRate(0.9f) == 0 && softenRate(-1.0f) == 0);
  assert(softenRate(1.001f) < 0.000001f);
  assert(near(softenRate(1.5f), 0.125f));
  assert(near(softenRate(2.0f), 0.5f));
  assert(near(softenRate(3.0f), 1.5f));
  for (float rate = 0; rate < 200; rate += 0.25f) {
    assert(near(softenRate(-rate), -softenRate(rate)));
  }
}

float turn(uint32_t intervalUs, bool jitter = false) {
  Controller c;
  uint32_t now = calibrate(c);
  const uint32_t start = now;
  const uint32_t end = now + 1000000;
  uint32_t report = now;
  int sent = 0;
  while (now < end) {
    const uint32_t dt = jitter ? (((now - start) / 1000) % 2 ? 2000 : 3000) : intervalUs;
    now = std::min(end, now + dt);
    c.sample({0, 0, -20}, {0, 0, 1}, now);
    if (now - report >= kReportIntervalUs) {
      sent += PixelAccumulator::take(c.pending.x);
      report = now;
    }
  }
  return sent + c.pending.x;
}

void testIntegrationAndRemainders() {
  const float expected = 22.0f * softenRate(20) * 1.06f;
  const float nominal = turn(2500);
  assert(near(nominal, expected, 1.0f));
  assert(near(turn(5000), nominal, 1.5f));
  assert(near(turn(2500, true), nominal, 1.5f));

  Controller step;
  uint32_t stepNow = calibrate(step);
  const float fullStepDelta = kPixelsPerDegree * softenRate(20) * 1.06f * 0.0025f;
  float lastDelta = 0;
  for (int i = 0; i < 3; ++i) {
    const float before = step.pending.x;
    step.sample({0, 0, -20}, {0, 0, 1}, stepNow += 2500);
    lastDelta = step.pending.x - before;
  }
  assert(lastDelta >= fullStepDelta * 0.95f);  // 95% response in 7.5ms.

  PixelAccumulator a;
  int sent = 0;
  for (int i = 0; i < 100; ++i) {
    a.x += 0.22f;
    sent += PixelAccumulator::take(a.x);
  }
  assert(near(sent + a.x, 22));
  a.x = 1000.75f;
  sent = 0;
  for (int i = 0; i < 8; ++i) {
    const int delta = PixelAccumulator::take(a.x);
    assert(delta <= 127);
    sent += delta;
  }
  assert(sent == 1000 && near(a.x, 0.75f));
  a.y = -1000.75f;
  sent = 0;
  for (int i = 0; i < 8; ++i) {
    const int delta = PixelAccumulator::take(a.y);
    assert(delta >= -127);
    sent += delta;
  }
  assert(sent == -1000 && near(a.y, -0.75f));

  Controller c;
  uint32_t now = calibrate(c);
  for (int i = 0; i < 400; ++i) {
    c.sample({0, 0, -2}, {0, 0, 1}, now += 2500);
  }
  assert(c.pending.x > 10 && c.pending.x < 12);
  const float total = c.pending.x;
  for (int i = 0; i < 20; ++i) {
    c.sample({}, {0, 0, 1}, now += 2500);
  }
  assert(c.pending.x == total);  // No filter tail.
}

void testFreezeDisconnectAndGap() {
  Controller c;
  uint32_t now = calibrate(c);
  c.sample({0, 0, -50}, {0, 0, 1}, now += 2500);
  assert(c.pending.x > 0);
  c.setInput(true, true, now);
  assert(c.pending.x == 0);
  for (int i = 0; i < 100; ++i) {
    c.sample({0, 0, -50}, {0, 0, 1}, now += 2500);
  }
  assert(c.pending.x == 0);
  c.setInput(false, true, now);
  for (int i = 0; i < 59; ++i) {
    c.sample({0, 0, -50}, {0, 0, 1}, now += 2500);
    assert(c.pending.x == 0);
  }
  c.sample({0, 0, -50}, {0, 0, 1}, now += 2500);
  assert(c.pending.x > 0);
  c.setInput(false, false, now);
  c.sample({0, 0, -50}, {0, 0, 1}, now += 2500);
  assert(c.pending.x == 0);
  c.setInput(false, true, now);
  c.sample({0, 0, -50}, {0, 0, 1}, now += 100000);
  assert(c.pending.x == 0);
  assert(c.calibrationRetries == 0);
}

void testShake() {
  Controller c;
  uint32_t now = calibrate(c);
  c.sample({}, {0, 0, 3.2f}, now += 2500);
  assert(c.pointingEnabled);
  c.sample({}, {0, 0, 3.2f}, now += 2500);
  assert(!c.pointingEnabled);
  for (int i = 0; i < 1000; ++i) {
    c.sample({}, {0, 0, 3.2f}, now += 2500);
  }
  assert(!c.pointingEnabled);
  for (int i = 0; i < 160; ++i) {
    c.sample({}, {0, 0, 1}, now += 2500);
  }
  c.sample({}, {0, 0, 3.2f}, now += 2500);
  c.sample({}, {0, 0, 3.2f}, now += 2500);
  assert(c.pointingEnabled && c.blocked(now));
}

void testClockWrap() {
  Controller c;
  uint32_t now = calibrate(c, {}, {0, 0, 1}, UINT32_MAX - 1100000);
  c.freeze(now);
  for (int i = 0; i < 100; ++i) {
    c.sample({0, 0, -20}, {0, 0, 1}, now += 2500);
  }
  assert(now < 1000000 && c.pending.x > 0);

  c.freeze(now);
  const uint32_t interactionTime = now;
  assert(c.blocked(now));
  assert(!c.blocked(now + kTouchFreezeUs));
  assert(!c.blocked(interactionTime));  // Same counter value one full wrap later.
}

void testStickAxesAndParity() {
  const Vec3 up = stickToPointerFrame({0, 0, 1});
  const auto right = projectRates(stickToPointerFrame({0, 0, -20}), up, true);
  const auto raise = projectRates(stickToPointerFrame({0, 20, 0}), up, true);
  const auto roll = projectRates(stickToPointerFrame({-20, 0, 0}), up, true);
  assert(kCursorXSign * right.yaw > 0 && near(right.pitch, 0));
  assert(kCursorYSign * raise.pitch < 0 && near(raise.yaw, 0));
  assert(near(roll.yaw, 0) && near(roll.pitch, 0));
  const auto fallback = projectRates(stickToPointerFrame({99, 12, -20}), {0, 0, 2}, false);
  assert(near(fallback.yaw, -20) && near(fallback.pitch, 12));

  for (int degrees = -180; degrees <= 180; degrees += 15) {
    const float a = degrees * 3.14159265359f / 180.0f;
    const Vec3 nativeUp(0, -std::sin(a), std::cos(a));
    const Vec3 nativeRight(0, std::cos(a), std::sin(a));
    const auto yaw = projectRates(stickToPointerFrame(nativeUp * -20),
                                  stickToPointerFrame(nativeUp), true);
    const auto pitch = projectRates(stickToPointerFrame(nativeRight * 20),
                                    stickToPointerFrame(nativeUp), true);
    assert(near(yaw.yaw, -20) && near(yaw.pitch, 0));
    assert(near(pitch.pitch, 20) && near(pitch.yaw, 0));
  }
  Controller core, stick;
  const Vec3 nativeBias(0.2f, -0.3f, 0.4f);
  const Vec3 pointerBias = stickToPointerFrame(nativeBias);
  uint32_t now = calibrate(core, pointerBias);
  calibrate(stick, pointerBias);
  for (int i = 0; i < 2000; ++i) {
    const Vec3 nativeGyro = nativeBias + Vec3(0, i % 2 ? 12 : -5, -20);
    const Vec3 pointerGyro(nativeGyro.y, -nativeGyro.x, nativeGyro.z);
    now += i % 2 ? 2000 : 3000;
    core.sample(pointerGyro, {0, 0, 1}, now);
    stick.sample(stickToPointerFrame(nativeGyro), stickToPointerFrame({0, 0, 1}), now);
    assert(core.pending.x == stick.pending.x && core.pending.y == stick.pending.y);
    assert(core.pending.wheel == 0 && stick.pending.wheel == 0);
  }
}

void testGyroScroll() {
  Controller c;
  uint32_t now = calibrate(c);
  c.sample({0, 0, -20}, {0, 0, 1}, now += 2500);
  assert(c.pending.x > 0);
  c.setScrollMode(true);
  assert(c.pending.x == 0 && c.pending.y == 0);
  for (int i = 0; i < 400; ++i) {
    c.sample(stickToPointerFrame({0, 20, -30}), {0, 0, 1}, now += 2500);
  }
  assert(c.pending.x == 0 && c.pending.y == 0);
  assert(near(c.pending.wheel, kScrollStepsPerDegree * softenRate(20), 0.02f));
  const float total = c.pending.wheel;
  for (int i = 0; i < 100; ++i) {
    c.sample({}, stickToPointerFrame({0.6f, 0, 0.8f}), now += 2500);
  }
  assert(c.pending.wheel == total);  // Static tilt never scrolls either.
  int sent = PixelAccumulator::take(c.pending.wheel);
  assert(sent > 0 && near(sent + c.pending.wheel, total));
  c.freeze(now);
  assert(c.pending.wheel == 0);
  c.sample({20, 0, 0}, {0, 0, 1}, now += 2500);
  assert(c.pending.wheel == 0);
  for (int i = 0; i < 100; ++i) {
    c.sample({-20, 0, 0}, {0, 0, 1}, now += 2500);
  }
  assert(c.pending.wheel < 0);
  c.setScrollMode(false);
  assert(c.pending.wheel == 0);
  c.sample({0, 0, -20}, {0, 0, 1}, now += 2500);
  assert(c.pending.x > 0 && c.pending.wheel == 0);
  c.setScrollMode(true);
  c.pointingEnabled = false;
  c.sample({20, 0, 0}, {0, 0, 1}, now += 2500);
  assert(c.pending.wheel == 0);
}

void testStickClickAndDrag() {
  stick_buttons::Controller b;
  auto a = b.update(true, false, true, 0);
  assert(!a.leftClick && !a.leftHeld && a.freezePointer);
  a = b.update(false, false, true, 100);
  assert(a.leftClick && !a.leftHeld);
  a = b.update(false, false, true, 105);
  assert(!a.leftClick);
  b.update(true, false, true, 200);
  a = b.update(true, false, true, 200 + stick_buttons::kDragHoldMs);
  assert(a.leftHeld && !a.freezePointer);
  a = b.update(false, false, true, 700);
  assert(!a.leftHeld && !a.leftClick);
  b.update(true, false, true, 800);
  a = b.update(false, false, true, 1400);
  assert(!a.leftClick);  // A stalled input poll must not turn a long press into a click.
}

void testStickRightAndMiddle() {
  stick_buttons::Controller b;
  b.update(false, true, true, 0);
  auto a = b.update(false, false, true, 50);
  assert(!a.rightClick && !a.middleClick);
  a = b.update(false, false, true, 349);
  assert(!a.rightClick);
  a = b.update(false, false, true, 350);
  assert(a.rightClick && !a.middleClick);
  assert(!b.update(false, false, true, 355).rightClick);
  b.update(false, true, true, 400);
  b.update(false, false, true, 450);
  b.update(false, true, true, 600);
  a = b.update(false, false, true, 650);
  assert(a.middleClick && !a.rightClick);
  a = b.update(false, false, true, 1000);
  assert(!a.middleClick && !a.rightClick);
  // A's drag stays held through a B click.
  b.update(true, false, true, 1100);
  b.update(true, true, true, 1600);
  b.update(true, false, true, 1650);
  a = b.update(true, false, true, 1950);
  assert(a.leftHeld && a.rightClick);
}

void testStickScrollButtons() {
  stick_buttons::Controller b;
  b.update(false, true, true, 0);
  auto a = b.update(false, true, true, 349);
  assert(!a.scroll && a.freezePointer);
  a = b.update(false, true, true, 350);
  assert(a.scroll && !a.freezePointer);
  a = b.update(false, false, true, 500);
  assert(!a.scroll && !a.rightClick && !a.middleClick);
  assert(!b.update(false, false, true, 900).rightClick);
  // A pending short B click is cancelled by a following scroll hold.
  b.update(false, true, true, 1000);
  b.update(false, false, true, 1050);
  b.update(false, true, true, 1100);
  a = b.update(false, true, true, 1450);
  assert(a.scroll && !a.rightClick);
  b.update(false, false, true, 1500);
  assert(!b.update(false, false, true, 1900).rightClick);
  b.update(false, true, true, 2000);
  b.update(false, false, true, 2400);
  assert(!b.update(false, false, true, 2800).rightClick);
}

void testStickDisconnectAndWrap() {
  stick_buttons::Controller b;
  b.update(true, false, true, 0);
  assert(b.update(true, false, true, 500).leftHeld);
  assert(!b.update(true, false, false, 510).leftHeld);
  auto a = b.update(true, false, true, 1000);
  assert(!a.leftHeld && a.freezePointer);  // Reconnect never resumes a held button.
  assert(!b.update(false, false, true, 1100).leftClick);
  b.update(false, true, true, 1200);
  b.update(false, false, true, 1250);
  b.update(false, false, false, 1260);
  assert(!b.update(false, false, true, 2000).rightClick);
  const uint32_t start = UINT32_MAX - 100;
  b.update(true, false, true, start);
  assert(b.update(true, false, true, start + 450U).leftHeld);
  b.update(false, false, true, start + 500U);
  b.update(false, true, true, UINT32_MAX - 80);
  b.update(false, false, true, UINT32_MAX - 50);
  b.update(false, true, true, 50);
  a = b.update(false, false, true, 100);
  assert(a.middleClick && !a.rightClick);
}

int main() {
  testDirections();
  testCalibration();
  testStaticTiltAndBias();
  testSoftKnee();
  testIntegrationAndRemainders();
  testFreezeDisconnectAndGap();
  testShake();
  testClockWrap();
  testStickAxesAndParity();
  testGyroScroll();
  testStickClickAndDrag();
  testStickRightAndMiddle();
  testStickScrollButtons();
  testStickDisconnectAndWrap();
  std::puts("Shared air mouse: 14 deterministic test groups passed");
}
