#pragma once

#include <M5Unified.h>
#include <utility/imu/BMI270_Class.hpp>

inline bool configureAirMouseImu() {
  using Bmi = m5::BMI270_Class;
  auto* sensor = M5.Imu.getImuInstancePtr(0);
  if (!sensor || M5.Imu.getType() != m5::imu_bmi270) {
    return false;
  }
  // Bosch ACC_CONF/GYR_CONF: 400Hz, normal bandwidth, performance mode.
  // Preserve the 8g / 2000dps ranges used by M5Unified's conversion factors.
  const uint8_t config[] = {0xAA, 0x02, 0xEA, 0x00};
  uint8_t actual[sizeof(config)] = {};
  if (!sensor->writeRegister(Bmi::ACC_CONF_ADDR, config, sizeof(config)) ||
      !sensor->readRegister(Bmi::ACC_CONF_ADDR, actual, sizeof(actual)) ||
      memcmp(config, actual, sizeof(config)) != 0) {
    return false;
  }
  M5.Imu.setCalibration(0, 0, 0);
  // Do not inherit a one-sample/NVS gyro bias; the new pipeline calibrates at rest.
  for (size_t index = 3; index < 6; ++index) {
    M5.Imu.setOffsetData(index, 0);
  }
  return M5.Imu.setAxisOrder(m5::IMU_Class::axis_x_pos, m5::IMU_Class::axis_y_pos,
                            m5::IMU_Class::axis_z_pos);
}
