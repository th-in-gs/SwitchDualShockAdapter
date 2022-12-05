#include <Arduino.h>

extern "C" {
    #include <usbdrv/usbdrv.h>

    // We declare this to be used by V-USB's 'osccal.h' oscilator calibration 
    // routine.
    uint8_t lastTimer0Value = 0;
}

usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    return 0;
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
void heartbeat()
{
    static unsigned long lastBlink = 0;

    unsigned long timeNow = millis();
    if(timeNow - lastBlink >= 1000) {
        lastBlink = 0;
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
}

void loop()
{
    heartbeat();
    usbPoll();
}