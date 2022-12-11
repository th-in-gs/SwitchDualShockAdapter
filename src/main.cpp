#include <Arduino.h>

extern "C" {
    #include <usbdrv/usbdrv.h>
    #include "descriptors.h"

    // We declare this to be used by V-USB's 'osccal.h' oscilator calibration 
    // routine.
    uint8_t lastTimer0Value = 0;
}


static gameControllerInputReport sReports[2] = { 0 };
volatile static uint8_t sReportReadIndex = 0;

static uint8_t sIdleRate;

usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    usbRequest_t* rq = (usbRequest_t*)data;

    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {
        if(rq->bRequest == USBRQ_HID_GET_REPORT) {
            if(rq->wValue.bytes[0] == 0x42) {
                usbMsgPtr = (typeof(usbMsgPtr))(&sReports[sReportReadIndex]);
                return sizeof(*sReports);
            }
        } else if(rq->bRequest == USBRQ_HID_GET_IDLE) {
            usbMsgPtr = &sIdleRate;
            return 1;
        } else if(rq->bRequest == USBRQ_HID_SET_IDLE) {
            sIdleRate = rq->wValue.bytes[1];
        }
    }

    return 0; /* default for not implemented requests: return no data back to host */
}

static boolean sLedIsOn = false;

static void prepareReport()
{
    static uint16_t buttonCounter = 1;
    uint8_t nextReportIndex = sReportReadIndex == 1 ? 0 : 1;
    gameControllerInputReport *report = &sReports[nextReportIndex];
    report->reportId = 0x42;

    uint8_t fakeButtons = (uint8_t)(buttonCounter >> 2);
    report->buttons1to7 = fakeButtons;
    report->buttons8to16 = 0xff - fakeButtons;
    report->leftStickX = fakeButtons;
    report->leftStickY = 0xff - fakeButtons;
    report->rightStickX = 0xff - fakeButtons;
    report->rightStickY = fakeButtons;

    ++buttonCounter;
    sReportReadIndex = nextReportIndex;
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

void loop()
{
    ledHeartbeat();
    usbPoll();
    if(usbInterruptIsReady()) {
        prepareReport();
        usbSetInterrupt((unsigned char*)&sReports[sReportReadIndex], sizeof(*sReports));
    }
}