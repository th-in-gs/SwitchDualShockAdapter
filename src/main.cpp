#include <Arduino.h>
#include <hd44780ioClass/hd44780_pinIO.h>

extern "C" {
    #include <usbdrv/usbdrv.h>
    #include "descriptors.h"

    // We declare this to be used by V-USB's 'osccal.h' oscilator calibration 
    // routine.
    uchar lastTimer0Value = 0;
    extern uchar usbDeviceAddr;
}

static hd44780_pinIO lcd(5, 7, 12, 11, 10, 9);

usbMsgLen_t usbFunctionSetup(uchar reportIn[8])
{
    static uint8_t sIdleRate = 0;

    lcd.print('S');

    usbRequest_t* rq = (usbRequest_t *)reportIn;

    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {

        lcd.print(rq->bRequest, 16);

        if(rq->bRequest == USBRQ_HID_GET_REPORT) {
            /*if(rq->wValue.bytes[0] == 0x42) {
                usbMsgPtr = (typeof(usbMsgPtr))(&sReports[sReportReadIndex]);
                return sizeof(*sReports);
            }*/
        } else if(rq->bRequest == USBRQ_HID_GET_IDLE) {
            usbMsgPtr = &sIdleRate;
            return 1;
        } else if(rq->bRequest == USBRQ_HID_SET_IDLE) {
            sIdleRate = rq->wValue.bytes[1];
        }
    }

    return 0; /* default for not implemented requests: return no reportIn back to host */
}

static uint8_t sReport[64] = { 0 };
static boolean sReportPending = false;
static boolean sInputReportsSuspended = false;
static boolean sLedIsOn = false;

static uint8_t prepareInputReportInBuffer(uint8_t *buffer) 
{
    buffer[0] = 0x81;
    memset(buffer, 0, 10);
    
    if(sLedIsOn) {
        buffer[3] |= 0b100;
    } else {
        buffer[3] |= 0b1000;
    }

    return 11;
}

static void report_P(uint8_t reportId, uint8_t reportCommand, const uint8_t *reportIn, uint8_t reportInLen) 
{
    if(sReportPending) {
        lcd.print("RprtClsh");
        return;
    }
    if(reportInLen > 62) {
        lcd.print("RprtBig");
        return;
    }
    sReport[0] = reportId;
    sReport[1] = reportCommand;
    if(reportInLen) {
        memcpy_P(&sReport[2], reportIn, reportInLen);
    }
    memset(&sReport[2 + reportInLen], 0, sizeof(sReport) - (2 + reportInLen));
    sReportPending = true;
}

static void uart_report_F(bool ack, byte subCommand, const uint8_t *reportIn, uint8_t reportInLen,  void *(*copyFunction)(void *, const void *, size_t))
{
    if(sReportPending) {
        lcd.print("RprtClsh");
        return;
    }
    if(reportInLen > 62) {
        lcd.print("RprtBig");
        return;
    }
    sReport[0] = 0x21;
    sReport[1] = usbSofCount;

    uint8_t inputBufferLength = prepareInputReportInBuffer(&sReport[2]);

    uint8_t ackByte = 0x00;
    if(ack) {
        ackByte = 0x80;
        if(reportInLen > 0) {
            ackByte |= subCommand;
        }
    }

    sReport[2 + inputBufferLength] = ackByte;
    sReport[3 + inputBufferLength] = subCommand;
    if(reportInLen) {
        copyFunction(&sReport[4 + inputBufferLength], reportIn, reportInLen);
    }
    memset(&sReport[4 + inputBufferLength + reportInLen], 0, sizeof(sReport) - (4 + inputBufferLength + reportInLen));
    sReportPending = true;
}

static void uartReport_P(bool ack, byte subCommand, const uint8_t *reportIn, uint8_t reportInLen)
{
    return uart_report_F(ack, subCommand, reportIn, reportInLen, memcpy_P);
}

static void uartReport(bool ack, byte subCommand, const uint8_t *reportIn, uint8_t reportInLen)
{
    return uart_report_F(ack, subCommand, reportIn, reportInLen, memcpy);
}

static void spiReport_P(uint16_t address, uint8_t length, const uint8_t *replyData, uint8_t replyDataLength)
{
    if(replyDataLength != length) {
        lcd.write('!');
    }

    uint8_t bufferLength = replyDataLength + 5;
    uint8_t buffer[bufferLength];
    buffer[0] = (uint8_t)(address & 0xFF);
    buffer[1] = (uint8_t)(address >> 8);
    buffer[2] = 0;
    buffer[3] = 0;
    buffer[4] = (uint8_t)(replyDataLength);
    memcpy_P(&buffer[5], replyData, replyDataLength);

    return uartReport(true, 0x10, buffer, bufferLength);
}

void usbFunctionWriteOut(uchar *data, uchar len)
{
    lcd.print('.');
    const uint8_t *reportIn;

    static uint8_t reportAccumulationBuffer[64];
    static uint8_t reportAccumulationBufferCursor = 0;
    static uint8_t bytesNeeded = 0;

    if(bytesNeeded == 0) {
        switch(data[0]) {
        case 0x00:
            bytesNeeded = 2;
            break;
        case 0x01:
            bytesNeeded = 16;
            break;
        case 0x10:
            bytesNeeded = 10;
            break;
        case 0x80:
            bytesNeeded = 2;
            break;
        default:
            bytesNeeded = len;
        }
    }

    if(reportAccumulationBufferCursor > 0 || bytesNeeded > len) {
        len = min(bytesNeeded - reportAccumulationBufferCursor, len);

        memcpy(&reportAccumulationBuffer[reportAccumulationBufferCursor], data, len);

        reportAccumulationBufferCursor += len;

        if(reportAccumulationBufferCursor == bytesNeeded) {
            reportAccumulationBufferCursor = 0;
            reportIn = reportAccumulationBuffer;
            bytesNeeded = 0;
        } else {
            return;
        }
    } else {
        reportIn = data;
        bytesNeeded = 0;
    }


    if(reportIn[0] == 0) {
        return;
    }

    uint8_t commandLow = reportIn[0];
    uint8_t commandHigh = reportIn[1];

    lcd.print(reportIn[0], 16);
    lcd.print(':');

    switch(commandLow) {
    case 0x00:
        lcd.print(commandHigh, 16);
        break;
    case 0x01: {
        uint8_t subCommand = reportIn[10];
        lcd.print(subCommand, 16);

        switch(subCommand) {
        case 0x01: {
            // Bluetooth manual pairing (?)
            static const PROGMEM uint8_t reply[] = { 0x03, 0x01 };
            uartReport_P(true, subCommand, reply, sizeof(reply));
        } break;
        case 0x02: {
            // Request device info
            static const PROGMEM uint8_t reply[] = {
                0x03, 0x48, // FW Version
                0x03, // Pro Controller
                0x02, // Unknown (Always 0x02)
                0xc7, 0xa3, 0x22, 0x53, 0x23, 0x43, // 6 bytes of MAC address
                0x03, // Unknown
                0x01, // Colors?
            };
            uartReport_P(true, subCommand, reply, sizeof(reply));
        } break;
        case 0x10: {
            // NVRAM read
            uint16_t address = reportIn[11] | reportIn[12] << 8;
            uint16_t length = reportIn[15];
            lcd.print(':');
            lcd.print(address, 16);
            lcd.print('-');
            lcd.print(length, 16);
            switch(address) {
            case 0x6000: {
                static const PROGMEM uint8_t reply[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
                spiReport_P(address, length, reply, sizeof(reply));
            } break;
            case 0x6050: {
                static const PROGMEM uint8_t reply[] = { 0xbc, 0x11, 0x42, 0x75, 0xa9, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
                spiReport_P(address, length, reply, sizeof(reply));
            } break;
            case 0x6080: {
                static const PROGMEM uint8_t reply[] = { 0x50, 0xfd, 0x00, 0x00, 0xc6, 0x0f, 0x0f, 0x30, 0x61, 0x96, 0x30, 0xf3, 0xd4, 0x14, 0x54, 0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63 };
                spiReport_P(address, length, reply, sizeof(reply));
            } break;
            case 0x6098: {
                static const PROGMEM uint8_t reply[] = { 0x0f, 0x30, 0x61, 0x96, 0x30, 0xf3, 0xd4, 0x14, 0x54, 0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63 };
                spiReport_P(address, length, reply, sizeof(reply));
            } break;
            case 0x603d: {
                static const PROGMEM uint8_t reply[] = { 0xba, 0x15, 0x62, 0x11, 0xb8, 0x7f, 0x29, 0x06, 0x5b, 0xff, 0xe7, 0x7e, 0x0e, 0x36, 0x56, 0x9e, 0x85, 0x60, 0xff, 0x32, 0x32, 0x32, 0xff, 0xff, 0xff };
                spiReport_P(address, length, reply, sizeof(reply));
            } break;
            case 0x8010: {
                static const PROGMEM uint8_t reply[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xb2, 0xa1 };
                spiReport_P(address, length, reply, sizeof(reply));
            } break;
            case 0x8028: {
                static const PROGMEM uint8_t reply[] = { 0xbe, 0xff, 0x3e, 0x00, 0xf0, 0x01, 0x00, 0x40, 0x00, 0x40, 0x00, 0x40, 0xfe, 0xff, 0xfe, 0xff, 0x08, 0x00, 0xe7, 0x3b, 0xe7, 0x3b, 0xe7, 0x3b };
                spiReport_P(address, length, reply, sizeof(reply));
            } break;
            default:
                uartReport_P(false, subCommand, NULL, 0);
                break;
            }
        } break;
        case 0x21: {
            // Set NFC/IR MCU configuration
            // From original code: FIXME: Check ack value
            static const PROGMEM uint8_t reply[] = { 0x01, 0x00, 0xff, 0x00, 0x03, 0x00, 0x05, 0x01 };
            uartReport_P(true, subCommand, reply, sizeof(reply));
        } break;
        case 0x03: // Set input report mode
        case 0x04: // Trigger buttons elapsed time (?)
        case 0x08: // Set shipment low power state
        case 0x30: // Set player lights
        case 0x38: // Set HOME light
        case 0x40: // Set IMU enabled state
        case 0x41: // Set IMU sesitivity
        case 0x48: // Set vibration enabled state
            // Unhandled: - Empty response
            uartReport_P(true, subCommand, NULL, 0);
            break;
        default:
            lcd.print('?');
            uartReport_P(false, subCommand, NULL, 0);
            break;
        }
    } break;
    case 0x10:
        lcd.print(commandHigh, 16);
        break;
    case 0x80:
        lcd.print(commandHigh, 16);
        switch(commandHigh) {
        case 0x05:
            // Stop HID Reports
            sInputReportsSuspended = true;
            break;
        case 0x04:
            // Start HID Reports
            sInputReportsSuspended = false;
            break;
        case 0x01: {
            // Request controller info inc. MAC address
            static const PROGMEM uint8_t reply[] = { 0x00, 0x03, 0x43, 0x23, 0x53, 0x22, 0xa3, 0xc7 };
            report_P(0x81, commandHigh, reply, sizeof(reply));
        } break;
        case 0x02:
            report_P(0x81, commandHigh, NULL, 0);
            break;
        default:
            lcd.print('?');
            break;
        }
    }
}


static void prepareInputReport()
{
    sReport[0] = 0x30;
    sReport[1] = usbSofCount;
    uint8_t reportLength = prepareInputReportInBuffer(&sReport[2]);
    memset(&sReport[2 + reportLength], 0, sizeof(sReport) - (2 + reportLength));
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

    lcd.begin(8, 2);
    lcd.setCursor(0, 0);
    lcd.cursor();
    lcd.lineWrap();
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
        // lcd.print(sLedIsOn ? '*' : (char)0b10100101);
    }
}

static void sendReportBlocking()
{
    static uint8_t count = 0;
    static bool lastLedState = false;
    if(sLedIsOn != lastLedState) {
        lastLedState = sLedIsOn;
        lcd.setCursor(0, 0);
        lcd.print('$');
        lcd.print(count, 10);
        count = 0;
    }

    ++count;
    uint8_t report[sizeof(sReport)];
    memcpy(report, sReport, sizeof(sReport));
    uint8_t reportCursor = 0;
    do {
        while(!usbInterruptIsReady()) {
            usbPoll();
        }
        uint8_t bytesToSend = min(8, (uint8_t)sizeof(report) - reportCursor);
        usbSetInterrupt(&sReport[reportCursor], bytesToSend);
        reportCursor += bytesToSend;
    } while(reportCursor < sizeof(report));    
    //lcd.print('<');
}

void loop()
{
    ledHeartbeat();
    usbPoll();

#if 1
    if(usbDeviceAddr != 0 && usbInterruptIsReady()) {
        if(sReportPending) {
            //lcd.print('>');
            //lcd.print(sReport[0], 16);
            sendReportBlocking();
            sReportPending = false;
        } else if (!sInputReportsSuspended) {
            prepareInputReport();
            sendReportBlocking();
        }
    }
#else
    if(usbInterruptIsReady()) {
        static uchar lastAddress = 0;
        static int i = 0;
        if(usbDeviceAddr == 0) {
            lastAddress = 0;
            i = 0;
        }
        if(lastAddress != usbDeviceAddr) {
            i = 1;
            lastAddress = usbDeviceAddr;
            lcd.print(" ^");
            lcd.print(lastAddress, 16);
            lcd.print('^');
        }

        uint8_t sReport[64] = { 0 };
        switch(i) {
        case 0:
            break;
        case 1:
            lcd.print(i);
            memcpy(sReport, (const uint8_t[]) { 0x81, 0x03 }, 2);
            usbSetInterrupt(sReport, sizeof(sReport));
            ++i;
            break;
        case 2:
            lcd.print(i);
            memcpy(sReport, (const uint8_t[]) { 0x81, 0x01, 0x00, 0x03 }, 4);
            usbSetInterrupt(sReport, sizeof(sReport));
            ++i;
            break;
        default: {
            if(sReportPending) {
                lcd.print('>');
                usbSetInterrupt(sReport, sizeof(sReport));
                sReportPending = false;
            }
        } break;
            // default:
            break;
        }
    }
#endif
}