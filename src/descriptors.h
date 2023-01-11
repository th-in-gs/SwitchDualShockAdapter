#ifndef __descriptors_h_included__
#define __descriptors_h_included__

#include <stdint.h>

typedef struct {
    uint8_t reportId;

    uint8_t buttons1to7;
    uint8_t buttons8to16;

    int8_t leftStickX;
    int8_t leftStickY;
    int8_t rightStickX;
    int8_t rightStickY;
} GameControllerInputReport;

#endif