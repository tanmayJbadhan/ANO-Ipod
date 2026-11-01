#include "BatteryMonitor.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"
#include "soc/usb_serial_jtag_struct.h"

static void chargingOn();

bool BatteryMonitor::_found = false;
bool BatteryMonitor::_onUsb = false;
bool BatteryMonitor::_boostOn = false;
int BatteryMonitor::_lastPct = 0;
unsigned long BatteryMonitor::_lastReadMs = 0;

static constexpr unsigned long REFRESH_MS = 4000;

// Same shared I2C bus as touch/ANO (different address, no conflict).
static std::shared_ptr<Arduino_IIC_DriveBus> battBus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

static std::unique_ptr<Arduino_IIC> sy6970(
    new Arduino_SY6970(battBus, SY6970_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, DRIVEBUS_DEFAULT_VALUE));

bool BatteryMonitor::begin() {
  _found = sy6970->begin();
  if (!_found) return false;

  sy6970->IIC_Write_Device_State(sy6970->Arduino_IIC_Power::Device::POWER_DEVICE_ADC_MEASURE,
                                  sy6970->Arduino_IIC_Power::Device_State::POWER_DEVICE_ON);

  // The SY6970 runs off the cell and keeps its registers across ESP32
  // resets and deep sleep, so the boost may still be on from last time -
  // and while it is, the battery can't charge and USB can't be detected.
  // Start from a known state: boost off, charging allowed.
  enable5VBoost(false);
  chargingOn();
  delay(50);
  return true;
}

bool BatteryMonitor::enable5VBoost(bool on) {
  if (!_found) return false;

  if (on) {
    // Ask for 5.0V and a generous current ceiling before switching the
    // booster on, so the HAT doesn't brown out on transients.
    sy6970->IIC_Write_Device_Value(sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_OTG_VOLTAGE_LIMIT, 5000);
    sy6970->IIC_Write_Device_Value(sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_OTG_CHARGING_LIMIT, 500);
  }

  bool ok = sy6970->IIC_Write_Device_State(
      sy6970->Arduino_IIC_Power::Device::POWER_DEVICE_OTG_MODE,
      on ? sy6970->Arduino_IIC_Power::Device_State::POWER_DEVICE_ON
         : sy6970->Arduino_IIC_Power::Device_State::POWER_DEVICE_OFF);
  if (ok) _boostOn = on;
  return ok;
}

// Reads VBUS_STAT. Only meaningful while the boost is OFF: once boosting,
// the chip reports "OTG" whether or not USB is plugged in, because it is
// driving VBUS itself and stops looking for an input source.
static bool chargerSeesUsb(bool &ok) {
  String bus = sy6970->IIC_Read_Device_State(
      sy6970->Arduino_IIC_Power::Status_Information::POWER_BUS_STATUS);
  ok = !bus.startsWith("->");
  return ok && !(bus == "No Input" || bus == "OTG");
}

// A computer on the USB port sends a start-of-frame packet every
// millisecond; the S3's USB block counts them. If the counter moves, a
// host is attached - and this works even while the charger is boosting
// and blind to USB. (A wall charger sends no frames; see probeForUsb.)
static bool usbHostPresent() {
  uint32_t a = USB_SERIAL_JTAG.fram_num.sof_frame_index;
  delay(3);
  return USB_SERIAL_JTAG.fram_num.sof_frame_index != a;
}

static void chargingOn() {
  sy6970->IIC_Write_Device_State(sy6970->Arduino_IIC_Power::Device::POWER_DEVICE_CHARGING_MODE,
                                  sy6970->Arduino_IIC_Power::Device_State::POWER_DEVICE_ON);
}

bool BatteryMonitor::_boostWanted = false;

bool BatteryMonitor::managePowerPath() {
  if (!_found) return false;

  if (!_boostOn) {
    bool ok;
    bool usb = chargerSeesUsb(ok);
    if (!ok) return _onUsb;
    if (usb != _onUsb) Serial.println(usb ? "USB in - charging" : "on battery");
    _onUsb = usb;
    if (usb) {
      chargingOn();
    } else if (_boostWanted) {
      enable5VBoost(true);
      Serial.println("boost on - VBUS 5V for the DAC");
    }
  } else {
    // Boosting: the charger can't tell us about USB any more.
    if (!_boostWanted) {
      enable5VBoost(false); // re-detected properly on the next call
      Serial.println("boost off - nothing playing");
    } else if (usbHostPresent()) {
      enable5VBoost(false);
      Serial.println("computer detected - boost off");
    }
  }
  return _onUsb;
}

bool BatteryMonitor::probeForUsb() {
  if (!_found || !_boostOn) return _onUsb;

  // Stop boosting just long enough for the charger to look for an input.
  enable5VBoost(false);
  delay(150);
  bool ok;
  bool usb = chargerSeesUsb(ok);
  if (ok && usb) {
    _onUsb = true;
    chargingOn();
    Serial.println("USB found at track change - charging");
  } else if (_boostWanted) {
    enable5VBoost(true);
  }
  return _onUsb;
}

bool BatteryMonitor::isCharging() {
  if (!_found) return false;
  String s = sy6970->IIC_Read_Device_State(
      sy6970->Arduino_IIC_Power::Status_Information::POWER_CHARGING_STATUS);
  return s == "Pre Chargeing" || s == "Fast Charging";
}

int BatteryMonitor::busVoltageMv() {
  if (!_found) return -1;
  double v = sy6970->IIC_Read_Device_Value(
      sy6970->Arduino_IIC_Power::Value_Information::POWER_INPUT_VOLTAGE);
  return (int)v;
}

bool BatteryMonitor::otgFault() {
  if (!_found) return false;
  String s = sy6970->IIC_Read_Device_State(
      sy6970->Arduino_IIC_Power::Status_Information::POWER_OTG_FAULT_STATUS);
  return s.indexOf("Normal") < 0 && !s.startsWith("->");
}

void BatteryMonitor::powerOff() {
  if (!_found) return;
  // Opening the battery FET disconnects the cell from the system rail,
  // which is a genuine off rather than a low-power sleep. It can't work
  // while USB is supplying the rail, so callers fall back to deep sleep.
  sy6970->IIC_Write_Device_State(sy6970->Arduino_IIC_Power::Device::POWER_BATFET_MODE,
                                  sy6970->Arduino_IIC_Power::Device_State::POWER_DEVICE_OFF);
}

int BatteryMonitor::voltageToPercent(float mv) {
  // Single-cell LiPo open-circuit-voltage curve. Approximate by design -
  // there's no coulomb-counting fuel gauge on this board, just a voltage
  // reading, so this is "good enough for a status icon."
  struct Point {
    float mv;
    int pct;
  };
  static const Point curve[] = {
      {4200, 100}, {4060, 90}, {3980, 80}, {3920, 70}, {3870, 60},
      {3820, 50}, {3790, 40}, {3770, 30}, {3740, 20}, {3680, 10}, {3450, 0},
  };
  const int n = sizeof(curve) / sizeof(curve[0]);

  if (mv >= curve[0].mv) return 100;
  if (mv <= curve[n - 1].mv) return 0;

  for (int i = 0; i < n - 1; i++) {
    if (mv <= curve[i].mv && mv >= curve[i + 1].mv) {
      float t = (mv - curve[i + 1].mv) / (curve[i].mv - curve[i + 1].mv);
      return (int)(curve[i + 1].pct + t * (curve[i].pct - curve[i + 1].pct));
    }
  }
  return 0;
}

void BatteryMonitor::poll() {
  if (!_found) return;

  unsigned long now = millis();
  if (_lastReadMs != 0 && now - _lastReadMs < REFRESH_MS) return;
  _lastReadMs = now;

  double mv = sy6970->IIC_Read_Device_Value(sy6970->Arduino_IIC_Power::Value_Information::POWER_BATTERY_VOLTAGE);
  // Sanity-guard against a garbage/no-battery-attached reading rather than
  // showing a nonsense percentage - keep the last good value instead.
  if (mv > 2500 && mv < 4300) {
    _lastPct = voltageToPercent((float)mv);
  }
}
