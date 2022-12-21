#include <Arduino.h>

extern "C" {
    #include <usbdrv/usbdrv.h>
    #include "descriptors.h"
    void usbFunctionRxHook(const uchar *data, uchar len);

    // We declare this to be used by V-USB's 'osccal.h' oscilator calibration 
    // routine.
    uchar lastTimer0Value = 0;
    extern uchar usbDeviceAddr;
}

#define lcd Serial

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    DDRC |= (1<<5 | 1<<4 | 1<<3);

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

   // Serial.begin(266667);
}

static void halt(uint8_t i)
{
    lcd.write(0b11110011);
    lcd.print(i, 16);
    while(true);
}

static uint8_t sReports[2][64] = { 0 };
static const uint8_t sReportSize = sizeof(sReports[0]);
static uint8_t sCurrentReport = 0;
static boolean sReportPending = false;
static boolean sInputReportsSuspended = false;
static boolean sLedIsOn = false;

static uint8_t prepareInputSubReportInBuffer(uint8_t *buffer) 
{    
    PORTC |= 1<<3;
    static uint8_t count = 0;
    static boolean previousLedState  = sLedIsOn;

    if(previousLedState != sLedIsOn) {
        // Seems like a good time to output some debug stats on how many 
        // reports per second we're managing to generate.
        lcd.print("\r\n\nFPS: ");
        lcd.print(count, 10);
        lcd.print("\r\n\n");

        count = 0;
        previousLedState = sLedIsOn;
    }

    buffer[0] = 0x81;
    memset(&buffer[1], 0, 10);
    
    // We'll alternately press the left and right dpad buttons for testing.
    if(sLedIsOn) {
        buffer[3] |= 0b100;
    } else {
        buffer[3] |= 0b1000;
    }

    ++count;

    PORTC &= ~(1<<3);
    return 11;
}

static void prepareInputReport()
{
    uint8_t *report = sReports[sCurrentReport];
    report[0] = 0x30;
    report[1] = usbSofCount;
    const uint8_t innerReportLength = prepareInputSubReportInBuffer(&report[2]);
    memset(&report[2 + innerReportLength], 0, sReportSize - (2 + innerReportLength));
}

static void report_P(uint8_t reportId, uint8_t reportCommand, const uint8_t *reportIn, uint8_t reportInLen) 
{
    if(sReportPending) {
        lcd.print("RprtClsh");
        halt(0);
        return;
    }

    const uint8_t reportSize = sReportSize;
    if(reportInLen > reportSize - 2) {
        lcd.print("RprtBig");
        halt(0);
        return;
    }
    
    uint8_t *report = sReports[sCurrentReport];
    report[0] = reportId;
    report[1] = reportCommand;
    memcpy_P(&report[2], reportIn, reportInLen);
    memset(&report[2 + reportInLen], 0, reportSize - (2 + reportInLen));
    sReportPending = true;
}

static void uart_report_F(bool ack, byte subCommand, const uint8_t *reportIn, uint8_t reportInLen,  void *(*copyFunction)(void *, const void *, size_t))
{
    if(sReportPending) {
        lcd.print("RprtClsh");
        halt(0);
        return;
    }

    const uint8_t reportSize = sReportSize;
    if(reportInLen > reportSize - 2) {
        lcd.print("RprtBig");
        halt(0);
        return;
    }

    uint8_t *report = sReports[sCurrentReport];
    report[0] = 0x21;
    report[1] = usbSofCount;

    const uint8_t inputBufferLength = prepareInputSubReportInBuffer(&report[2]);

    uint8_t ackByte = 0x00;
    if(ack) {
        ackByte = 0x80;
        if(reportInLen > 0) {
            ackByte |= subCommand;
        }
    }

    report[2 + inputBufferLength] = ackByte;
    report[3 + inputBufferLength] = subCommand;
    copyFunction(&report[4 + inputBufferLength], reportIn, reportInLen);
    memset(&report[4 + inputBufferLength + reportInLen], 0, reportSize - (4 + inputBufferLength + reportInLen));
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

    const uint8_t bufferLength = replyDataLength + 5;
    uint8_t buffer[bufferLength];
    buffer[0] = (uint8_t)(address & 0xFF);
    buffer[1] = (uint8_t)(address >> 8);
    buffer[2] = 0;
    buffer[3] = 0;
    buffer[4] = (uint8_t)(replyDataLength);
    memcpy_P(&buffer[5], replyData, replyDataLength);

    return uartReport(true, 0x10, buffer, bufferLength);
}

template <typename Out>
static void outputAndIterateBuffer(const uint8_t *data, const uint8_t len, const uint8_t offset, Out &out, void (*f)(uint8_t, uint8_t))
{
    for(uint8_t i = offset; i < len; ++i) {
        uint8_t ch = data[i];
        if(i == 4) {
            out.print("  ");
        }
        out.print((ch >> 4) & 0xf, 16);
        out.print(ch & 0xf, 16);
        if(f) {
            f(i, ch);
        }
    }
}

static void usbFunctionWriteOutOrStall_inner(const uchar *data, const uchar len, const boolean stall)
{
    static uint8_t reportId;
    static uint8_t reportAccumulationBuffer[sReportSize];
    static uint8_t accumulatedReportBytes = 0;

    if(stall) {
        // The host has told us it's stalling the endpoint.
        // Abandon reception of any in-progress reports - we're not going to
        // get the rest of it :-(
        accumulatedReportBytes = 0;
        return;
    }

    if(accumulatedReportBytes == 0) {
        reportId = data[0];
        lcd.print("\r\n\n");
    }

    /*
    lcd.print("\r\n<");
    outputAndIterateBuffer(&usbRxToken, 1, 0, lcd, NULL);
    lcd.print("|");
    outputAndIterateBuffer(&usbMsgFlags, 1, 0, lcd, NULL);
    lcd.print("|");
    outputAndIterateBuffer(&len, 1, 0, lcd, NULL);
    lcd.print("|");
    uint8_t sofs = usbSofCount;
    outputAndIterateBuffer(&sofs, 1, 0, lcd, NULL);
    lcd.print(">  ");

    outputAndIterateBuffer(data, len, 0, lcd, [](uint8_t i, uint8_t ch) {
        reportAccumulationBuffer[accumulatedReportBytes + i] = ch;
    });
    if(len < 8) {
        lcd.print('[');
        outputAndIterateBuffer(data, 8, len, lcd, NULL);
        lcd.print(']');
    }
    */

    // Different reports are different lengths!
    // Work out if this one is complete.
    bool reportComplete = false;

    switch(reportId) {
    case 0x00: // Unknown.
    case 0x80: // Regular commands.  
        if(len != 2 || accumulatedReportBytes != 0) { // Always only 2 bytes?
            halt(1 | reportId);
        }
        reportComplete = true;
        break;
    case 0x01: // 'UART' commands.
    case 0x10: // Unknown. Status?
        if(accumulatedReportBytes == 8) { // These are always two packets long.
            reportComplete = true;
        }
        break;
    default:
        halt(2 | reportId);
        return;
    }

    lcd.print('>');

    // Get ready to process the report if we have it all - 
    // or stow this packet away for accumulation if we
    const uint8_t *reportIn;
    if(reportComplete && accumulatedReportBytes == 0) {
        // This report is only one packet long - we can just use it directly, 
        // no need to deal with the reportAccumulationBuffer.
        reportIn = data;
    } else {
        // We need to concatenate the data into the reportAccumulationBuffer
        // before we either process the report or return.

        memcpy(reportAccumulationBuffer + accumulatedReportBytes, data, len);
        if(reportComplete) {
            // We've completed this report. Parse it, and set out accumulation
            // counter back to zero for the next incoming report.
            reportIn = reportAccumulationBuffer;
            accumulatedReportBytes = 0;
        } else {
            // We need more data to complete the report.
            accumulatedReportBytes += len;
            return;
        }
    }

    // Deal with the report!
    const uint8_t commandOrSequenceNumber = reportIn[1];

    lcd.print(' ');
    lcd.print(reportId, 16);
    lcd.print(':');
    lcd.print(commandOrSequenceNumber, 16);

    switch(reportId) {
    case 0x80: {
        // A 'Regular' command.

        const uint8_t command = commandOrSequenceNumber;
        switch(command) {
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
            report_P(0x81, command, reply, sizeof(reply));
        } break;
        case 0x02:
            report_P(0x81, command, NULL, 0);
            break;
        default:
            lcd.print('?');
            break;
        }
    } break;
    case 0x01: {
        // A 'UART' request.

        const uint8_t subCommand = reportIn[10];
        lcd.print(':');
        lcd.print(subCommand, 16);

        switch(subCommand) {
        case 0x00: {
            // Do nothing (return report)
            static const PROGMEM uint8_t reply[] = { 0x03 };
            uartReport_P(true, subCommand, reply, sizeof(reply));
        } break;
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
            const uint16_t address = reportIn[11] | reportIn[12] << 8;
            const uint16_t length = reportIn[15];
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
        case 0x03: // Set input report mode
        case 0x04: // Trigger buttons elapsed time (?)
        case 0x08: // Set shipment low power state
        case 0x21: // Set NFC/IR MCU configuration
        case 0x30: // Set player lights
        case 0x38: // Set HOME light
        case 0x40: // Set IMU enabled state
        case 0x41: // Set IMU sesitivity
        case 0x48: // Set vibration enabled state
            // Unhandled, but we'll tell the switch we've handled it...
            uartReport_P(true, subCommand, NULL, 0);
            break;
        default:
            uartReport_P(false, subCommand, NULL, 0);
            halt(0b10000000 | subCommand);
            break;
        }
    } break;
    case 0x10:
    case 0x00:
        break;
    default:
        // We should never reach here because we should've halted above.
        halt(3 | reportId);
        break;
    }

    // Let's see how the 12.8MHz tuning for the internal oscillator is doing.
    lcd.print(' ');
    lcd.print(OSCCAL, 16);
}

static void usbFunctionWriteOutOrStall(const uchar *data, const uchar len, const boolean stall)
{
    PORTC |= 1 << 4;
    usbFunctionWriteOutOrStall_inner(data, len, stall);
    PORTC &= ~(1 << 4);
}

void usbFunctionWriteOut(uchar *data, uchar len)
{
    usbFunctionWriteOutOrStall(data, len, false);
}

void usbFunctionRxHook(const uchar *data, const uchar len)
{
#if 0
    if(usbRxToken == 0b00101101 && data[0] == 2 && data[1] == 1 ) {
        // This is an ENDPOINT_HALT for OUT endpoint 1 (i.e. the one to us from
        // the host). 
        // We mneed to abandon any in-progress report reception - we won't get
        // the rest of the report.
        lcd.print("\n!!H!!\n");
        usbFunctionWriteOutOrStall(data, len, true);
    }
#else
    if(usbRxToken == USBPID_SETUP) {
        const usbRequest_t *request = (const usbRequest_t *)data;
        if((request->bmRequestType & USBRQ_RCPT_MASK) == USBRQ_RCPT_ENDPOINT && 
           request->bRequest == USBRQ_CLEAR_FEATURE
            /* && request->wIndex.bytes[0] == 1*/) {
            // This is an clear of ENDPOINT_HALT for OUT endpoint 1
            // (i.e. the one to us from the host).
            // We need to abandon any old in-progress report reception - we
            // won't get the rest of the report from before the stall.
            lcd.print("\n!HALT\n");
            usbFunctionWriteOutOrStall(data, len, true);
        }
    }
#endif
} 

usbMsgLen_t usbFunctionSetup(uchar reportIn[8])
{
    static uint8_t sIdleRate = 0;

    lcd.print('S');

    usbRequest_t* rq = (usbRequest_t *)reportIn;

    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {

        lcd.print(rq->bRequest, 16);

        if(rq->bRequest == USBRQ_HID_GET_REPORT) {
            if(rq->wValue.bytes[0] == 0x42) {
                prepareInputReport();
                usbMsgPtr = (typeof(usbMsgPtr))(&sReports[sCurrentReport]);
                return sizeof(*sReports);
            }
        } else if(rq->bRequest == USBRQ_HID_GET_IDLE) {
            usbMsgPtr = &sIdleRate;
            return 1;
        } else if(rq->bRequest == USBRQ_HID_SET_IDLE) {
            sIdleRate = rq->wValue.bytes[1];
        }
    }

    return 0; /* default for not implemented requests: return no reportIn back to host */
}

static void sendReportBlocking()
{
PORTC |= 1<<5;

    const uint8_t reportIndex = sCurrentReport;

    // Toggle the current report.
    sCurrentReport = reportIndex == 0 ? 1 : 0;

    // The next report can be filled in while we send this one.
    // This doesn't seem to happen very much.
    sReportPending = false;

    const uint8_t reportSize = sReportSize;
    uint8_t *report = sReports[reportIndex];
    uint8_t reportCursor = 0;
    do {
        while(!usbInterruptIsReady()) {
            usbPoll();
        }
        uint8_t bytesToSend = min(8, reportSize - reportCursor);
        usbSetInterrupt(&report[reportCursor], bytesToSend);
        reportCursor += bytesToSend;
    } while(reportCursor < reportSize);    

PORTC &= ~(1<<5); 
}

// Call regularly to blink the LED every 1 second.
static void ledHeartbeat()
{
    static unsigned long lastBeat = 0;

    const unsigned long timeNow = millis();
    if(timeNow - lastBeat >= 1000) {
        lastBeat += 1000;
        sLedIsOn = !sLedIsOn;
        digitalWrite(LED_BUILTIN, sLedIsOn);
    }
}

void loop()
{
    ledHeartbeat();
    usbPoll();

    static uchar lastAddress = 0;
    if(lastAddress != usbDeviceAddr) {
        lastAddress = usbDeviceAddr;
        lcd.print("\r\n\nController Online - USB Address:");
        lcd.print(lastAddress, 16);
        lcd.print("\r\n\n");
    }

    if(usbDeviceAddr != 0 && usbInterruptIsReady()) {
        if(sReportPending) {
            sendReportBlocking();
        } else if(!sInputReportsSuspended) {
            prepareInputReport();
            sendReportBlocking();
        }
    }
}