#ifndef __descriptors_h_included__
#define __descriptors_h_included__

#include <stdint.h>

typedef struct
{
  uint8_t  reportId;                                 // Report ID = 0x30 (48) '0'
                                                     // Collection: CA:Joystick
  uint8_t  BTN_JoystickButton1 : 1;                  // Usage 0x00090001: Button 1 Primary/trigger, Value = 0 to 1
  uint8_t  BTN_JoystickButton2 : 1;                  // Usage 0x00090002: Button 2 Secondary, Value = 0 to 1
  uint8_t  BTN_JoystickButton3 : 1;                  // Usage 0x00090003: Button 3 Tertiary, Value = 0 to 1
  uint8_t  BTN_JoystickButton4 : 1;                  // Usage 0x00090004: Button 4, Value = 0 to 1
  uint8_t  BTN_JoystickButton5 : 1;                  // Usage 0x00090005: Button 5, Value = 0 to 1
  uint8_t  BTN_JoystickButton6 : 1;                  // Usage 0x00090006: Button 6, Value = 0 to 1
  uint8_t  BTN_JoystickButton7 : 1;                  // Usage 0x00090007: Button 7, Value = 0 to 1
  uint8_t  BTN_JoystickButton8 : 1;                  // Usage 0x00090008: Button 8, Value = 0 to 1
  uint8_t  BTN_JoystickButton9 : 1;                  // Usage 0x00090009: Button 9, Value = 0 to 1
  uint8_t  BTN_JoystickButton10 : 1;                 // Usage 0x0009000A: Button 10, Value = 0 to 1
  uint8_t  BTN_JoystickButton11 : 1;                 // Usage 0x0009000B: Button 11, Value = 0 to 1
  uint8_t  BTN_JoystickButton12 : 1;                 // Usage 0x0009000C: Button 12, Value = 0 to 1
  uint8_t  BTN_JoystickButton13 : 1;                 // Usage 0x0009000D: Button 13, Value = 0 to 1
  uint8_t  BTN_JoystickButton14 : 1;                 // Usage 0x0009000E: Button 14, Value = 0 to 1
  uint8_t  : 1;                                      // Pad
  uint8_t  : 1;                                      // Pad
                                                     // Collection: CA:Joystick CP:Pointer
  uint16_t GD_JoystickPointerX;                      // Usage 0x00010030: X, Value = 0 to 65535
  uint16_t GD_JoystickPointerY;                      // Usage 0x00010031: Y, Value = 0 to 65535
  uint16_t GD_JoystickPointerZ;                      // Usage 0x00010032: Z, Value = 0 to 65535
  uint16_t GD_JoystickPointerRz;                     // Usage 0x00010035: Rz, Value = 0 to 65535
                                                     // Collection: CA:Joystick
  uint8_t  GD_JoystickHatSwitch : 4;                 // Usage 0x00010039: Hat switch, Value = 0 to 7, Physical = Value x 45 in degrees
  uint8_t  BTN_JoystickButton15 : 1;                 // Usage 0x0009000F: Button 15, Value = 0 to 1, Physical = Value x 315 in degrees
  uint8_t  BTN_JoystickButton16 : 1;                 // Usage 0x00090010: Button 16, Value = 0 to 1, Physical = Value x 315 in degrees
  uint8_t  BTN_JoystickButton17 : 1;                 // Usage 0x00090011: Button 17, Value = 0 to 1, Physical = Value x 315 in degrees
  uint8_t  BTN_JoystickButton18 : 1;                 // Usage 0x00090012: Button 18, Value = 0 to 1, Physical = Value x 315 in degrees
  uint8_t  pad_7[52];                                // Pad
} inputReport30_t;

#endif