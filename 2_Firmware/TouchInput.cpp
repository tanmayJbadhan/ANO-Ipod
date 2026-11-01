#include "TouchInput.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"

// Same wiring/pattern as 1_Examples/FT3168/FT3168.ino and 1_Examples/Lvgl/Lvgl.ino:
// shared I2C bus (IIC_SDA/IIC_SCL), FT3168 at FT3168_DEVICE_ADDRESS, interrupt
// on TP_INT sets a flag the main loop drains.
static std::shared_ptr<Arduino_IIC_DriveBus> touchBus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

static void touchInterruptHandler(void);

static std::unique_ptr<Arduino_IIC> ft3168(
    new Arduino_FT3x68(touchBus, FT3168_DEVICE_ADDRESS, TP_RST, TP_INT, touchInterruptHandler));

static void touchInterruptHandler(void) { ft3168->IIC_Interrupt_Flag = true; }

bool TouchInput::begin() {
  _found = ft3168->begin();
  if (!_found) return false;

  ft3168->IIC_Write_Device_State(ft3168->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                  ft3168->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);
  return true;
}

void TouchInput::poll() {
  _prevPressed = _pressed;
  if (!_found) return;

  if (ft3168->IIC_Interrupt_Flag == true) {
    ft3168->IIC_Interrupt_Flag = false;

    uint8_t fingers = (uint8_t)ft3168->IIC_Read_Device_Value(
        ft3168->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);

    if (fingers > 0) {
      _pressed = true;
      _x = (int16_t)ft3168->IIC_Read_Device_Value(ft3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
      _y = (int16_t)ft3168->IIC_Read_Device_Value(ft3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
    } else {
      _pressed = false;
    }
  }
  // If no fresh interrupt this poll, retain the last known state - the
  // FT3168 keeps interrupting periodically while a finger stays down, so
  // this only matters for the few ms between reports.
}
