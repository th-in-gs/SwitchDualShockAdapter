#ifndef __descriptors_h_included__
#define __descriptors_h_included__

#include <stdint.h>

typedef struct SwitchReport {
    int connectionInfo:4;
    int batteryLevel:4;

    int yButton:1;
    int xButton:1;
    int bButton:1;
    int aButton:1;
    int shoulderRightRightButton:1;
    int shoulderRightLeftButton:1;
    int rShoulderButton:1;
    int zRShoulderButton:1;

    int minusButton:1;
    int plusButton:1;
    int rStickButton:1;
    int lStickButton:1;
    int homeButton:1;
    int captureButton:1;
    int unusedButton:1;
    int chargingGrip:1;

    int downButton:1;
    int upButton:1;
    int rightButton:1;
    int leftButton:1;
    int shoulderLeftRightButton:1;
    int shoulderLeftLeftButton:1;
    int lShoulderButton:1;
    int zLShoulderButton:1;

    uint8_t leftStick[3];
    uint8_t rightStick[3];
    uint8_t vibrationReport;
} SwitchReport;

typedef struct DualShockReport {
    uint8_t effEff;
    uint8_t reportId;
    uint8_t fiveAy;

    int selectButton:1;
    int l3Button:1;
    int r3Button:1;
    int startButton:1;
    int upButton:1;
    int rightButton:1;
    int downButton:1;
    int leftButton:1;

    int l2Button:1;
    int r2Button:1;
    int l1Button:1;
    int r1Button:1;
    int triangleButton:1;
    int circleButton:1;
    int crossButton:1;
    int squareButton:1;

    uint8_t rightStickX;
    uint8_t rightStickY;
    uint8_t leftStickX;
    uint8_t leftStickY;
} DualShockReport;

#endif