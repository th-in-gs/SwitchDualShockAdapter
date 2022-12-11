#include <Arduino.h>

extern "C" {
    #include <usbdrv/usbdrv.h>

    // We declare this to be used by V-USB's 'osccal.h' oscilator calibration 
    // routine.
    uint8_t lastTimer0Value = 0;
}

#include "descriptors.h"


static inputReport30_t reports[2] = {0};
volatile static uint8_t reportReadIndex = 0;

usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    usbRequest_t* rq = (usbRequest_t*)data;

    /* The following requests are never used. But since they are required by
     * the specification, we implement them in this example.
     */
    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) { /* class request type */
        if(rq->bRequest == USBRQ_HID_GET_REPORT) {
            if(rq->wValue.bytes[0] == 0x30) {
                /* we only have one report type, so don't look at wValue */
                usbMsgPtr = (typeof(usbMsgPtr))(&reports[reportReadIndex]);
                return sizeof(inputReport30_t);
            }
        }
    } else {
        /* no vendor specific requests implemented */
    }
    return 0; /* default for not implemented requests: return no data back to host */
}

static boolean sLedIsOn = false;

static void prepareReport()
{
    uint8_t nextReportIndex = reportReadIndex == 1 ? 0 : 1;
    inputReport30_t *report = &reports[nextReportIndex];
    report->reportId = 0x30;
    report->BTN_JoystickButton1 = sLedIsOn;
/*
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
*/

    *report = {
        .reportId = 0x30,
    };

    reportReadIndex = nextReportIndex;
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);

    // Disable interrupts for USB reset.
    noInterrupts();         

    // Initialize V-USB.
    usbInit(); 

    // V-USB wiki (http://vusb.wikidot.com/driver-api) says:
    //      "In theory, you don't need this, but it prevents inconsistencies 
    //      between host and device after hardware or watchdog resets."
    usbDeviceDisconnect(); 
    delay(250); 
    usbDeviceConnect();
    
    // Enable interrupts again.
    interrupts();           
}

// Call regularly to blink the LED every 1 second.
static void ledHeartbeat()
{
    static unsigned long lastBeat = 0;

    unsigned long timeNow = millis();
    if(timeNow - lastBeat >= 1000) {
        lastBeat = timeNow;
        sLedIsOn = !sLedIsOn;
        digitalWrite(LED_BUILTIN, sLedIsOn);
    }
}

static void pollInputHeartbeat() 
{
    static unsigned long lastBeat = 0;

    unsigned long timeNow = millis();
    if(timeNow - lastBeat >= 10) {
        lastBeat = timeNow;
        prepareReport();
    }
}

void loop()
{
    ledHeartbeat();
    pollInputHeartbeat();
    usbPoll();
    if(usbInterruptIsReady()) {
        usbSetInterrupt((unsigned char*)&reports[reportReadIndex], sizeof(sizeof(inputReport30_t)));
    }
}