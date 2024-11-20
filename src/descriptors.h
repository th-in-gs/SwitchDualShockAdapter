#ifndef __descriptors_h_included__
#define __descriptors_h_included__

#include <stdint.h>

typedef struct SwitchReport {
    int connectionInfo:4;
    int batteryLevel:4;

    union {
        uint8_t buttons1;
        struct {
            int yButton : 1;
            int xButton : 1;
            int bButton : 1;
            int aButton : 1;
            int shoulderRightRightButton : 1;
            int shoulderRightLeftButton : 1;
            int rShoulderButton : 1;
            int zRShoulderButton : 1;
        };
    };

    union {
        uint8_t buttons2;
        struct {
            int minusButton : 1;
            int plusButton : 1;
            int rStickButton : 1;
            int lStickButton : 1;
            int homeButton : 1;
            int captureButton : 1;
            int unusedButton : 1;
            int chargingGrip : 1;
        };
    };

    union {
        uint8_t buttons3;
        struct {
            int downButton : 1;
            int upButton : 1;
            int rightButton : 1;
            int leftButton : 1;
            int shoulderLeftRightButton : 1;
            int shoulderLeftLeftButton : 1;
            int lShoulderButton : 1;
            int zLShoulderButton : 1;
        };
    };

    uint8_t leftStick[3];
    uint8_t rightStick[3];
    uint8_t vibrationReport;
} SwitchReport;

typedef struct DualShockReport {
    uint8_t effEff;
    union {
        uint8_t reportId;
        struct {
            uint8_t reportLength : 4;
            uint8_t deviceMode : 4;
        };
    };
    uint8_t fiveAy;

    union {
        uint8_t buttons1;
        struct {
            uint8_t selectButton : 1;   // 0
            uint8_t l3Button : 1;       // 1
            uint8_t r3Button : 1;       // 2
            uint8_t startButton : 1;    // 3
            uint8_t upButton : 1;       // 4
            uint8_t rightButton : 1;    // 5
            uint8_t downButton : 1;     // 6
            uint8_t leftButton : 1;     // 7
        };
    };

    union {
        uint8_t buttons2;
        struct {
            uint8_t l2Button : 1;       // 0
            uint8_t r2Button : 1;       // 1
            uint8_t l1Button : 1;       // 2
            uint8_t r1Button : 1;       // 3
            uint8_t triangleButton : 1; // 4
            uint8_t circleButton : 1;   // 5
            uint8_t crossButton : 1;    // 6
            uint8_t squareButton : 1;   // 7
        };
    };

    uint8_t rightStickX;
    uint8_t rightStickY;
    uint8_t leftStickX;
    uint8_t leftStickY;
} DualShockReport;

#define EMPTY_DUAL_SHOCK_REPORT { 0, { 0 }, 0, { 0xff }, { 0xff }, 0x80, 0x80, 0x80, 0x80 }

#endif