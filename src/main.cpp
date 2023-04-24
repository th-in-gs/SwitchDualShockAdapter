#include <Arduino.h>

extern "C" {
    #include <usbdrv/usbdrv.h>
    #include "descriptors.h"

    // We declare this to be used by V-USB's 'osccal.h' oscilator calibration
    // routine.
    uint8_t lastTimer0Value = 0;
}

void setup()
{
    // We will use Port B bits 1-4 (pins 15-18) as 'debug' output, and
    // bit 5 (pin 19) as our one-second-blinking 'status' LED.
    // Configure them as output, and ensure they start as zero.
    DDRB  |= 0b00111110;
    PORTB &= 0b11000001;

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

static void halt(uint8_t i)
{
    // Panic!
    // You can tell if this is called becuse the LED will stop blinking.
    // Would be good to report the actual error somehow...
    while(true);
}

static uint8_t sReports[2][64] = { 0 };
static const uint8_t sReportSize = sizeof(sReports[0]);
static uint8_t sCurrentReport = 0;

static boolean sReportPending = false;
static boolean sInputReportsSuspended = false;
static boolean sLedIsOn = true;

static uint8_t prepareInputSubReportInBuffer(uint8_t *buffer)
{
    buffer[0] = 0x81;
    memset(&buffer[1], 0, 10);

    // We'll alternately press the left and right dpad buttons for testing.
    if(sLedIsOn) {
        buffer[3] |= 0b100;
    } else {
        buffer[3] |= 0b1000;
    }

    // Return how much of the buffer we've filled.
    return 11;
}

static void prepareInputReport()
{
    uint8_t *report = sReports[sCurrentReport];
    report[0] = 0x30;
    report[1] = usbSofCount;

    // Prepare the input report in the Pro Controller's format.
    const uint8_t innerReportLength = prepareInputSubReportInBuffer(&report[2]);

    // Fill the remainder of the buffer with 0.
    memset(&report[2 + innerReportLength], 0, sReportSize - (2 + innerReportLength));
}

static void report_P(uint8_t reportId, uint8_t reportCommand, const uint8_t *reportIn, uint8_t reportInLen)
{
    if(sReportPending) {
        halt(0);
        return;
    }

    const uint8_t reportSize = sReportSize;
    if(reportInLen > reportSize - 2) {
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

static void uartReport_F(bool ack, byte subCommand, const uint8_t *reportIn, uint8_t reportInLen,  void *(*copyFunction)(void *, const void *, size_t))
{
    // '_F' - means pass in function to use for memcpying.

    if(sReportPending) {
        halt(0);
        return;
    }

    const uint8_t reportSize = sReportSize;
    if(reportInLen > reportSize - 2) {
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
    return uartReport_F(ack, subCommand, reportIn, reportInLen, memcpy_P);
}

static void uartReport(bool ack, byte subCommand, const uint8_t *reportIn, uint8_t reportInLen)
{
    return uartReport_F(ack, subCommand, reportIn, reportInLen, memcpy);
}

static void spiReport_P(uint16_t address, uint8_t length, const uint8_t *replyData, uint8_t replyDataLength)
{
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

static void usbFunctionWriteOutInternal(uchar *data, uchar len)
{
    static uint8_t reportId;
    static uint8_t reportAccumulationBuffer[sReportSize];
    static uint8_t accumulatedReportBytes = 0;

    if(accumulatedReportBytes == 0) {
        // Debug signal that we've started accumulating and processing a report.
        // This will remain set over multiple calls to this function, until
        // we've fully processed the report.
        PORTB |= (1 << 4);

        // We read the report ID from the first packet.
        reportId = data[0];
    }

    // Different reports are different lengths!
    // Work out if this one is complete.
    bool reportComplete = false;

    switch(reportId) {
    case 0x00: // Unknown.
    case 0x80: // Regular commands.
        if(len != 2 || accumulatedReportBytes != 0) { // Always only 2 in length?
            halt(1 | reportId);
        }
        reportComplete = true;
        break;
    case 0x01: // 'UART' commands.
    case 0x10: // Unknown. Status? Keep-alive?
        if(accumulatedReportBytes == 8) {
            reportComplete = true;
        }
        break;
    default:
        halt(2 | reportId);
        return;
    }

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
            break;
        }
    } break;
    case 0x01: {
        // A 'UART' request.

        const uint8_t subCommand = reportIn[10];

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

    PORTB &= ~(1 << 4); // Debug signal that we've stopped processing a report.
}

void usbFunctionWriteOut(uchar *data, uchar len)
{
    PORTB |= (1 << 3);
    usbFunctionWriteOutInternal(data, len);
    PORTB &= ~(1 << 3);
}

usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    // I've never seen this called when connected to a Switch or a Mac.
    // It might be necessary to implement the USB spec, but I really don't
    // like having untested code that's never actually used.
    // Let's just return 0 (signified 'not handled), and worry about this if it
    // turns out to be needed later.
    return 0;
}

static void sendReportBlocking()
{
    const uint8_t reportIndex = sCurrentReport;
    sCurrentReport = reportIndex == 0 ? 1 : 0;

    // The next report can be filled in while we send this one.
    // This doesn't seem to happen very much.
    sReportPending = false;

    const uint8_t reportSize = sReportSize;
    uint8_t *report = sReports[reportIndex];
    uint8_t reportCursor = 0;
    do {
        usbPoll();
        if(usbInterruptIsReady()) {
            PORTB |= (1 << 2);

            uint8_t bytesToSend = min(8, reportSize - reportCursor);
            usbSetInterrupt(&report[reportCursor], bytesToSend);
            reportCursor += bytesToSend;

            PORTB &= ~(1 << 2);
        }
    } while(reportCursor < reportSize);
}

// Call regularly to blink the LED every 1 second.
static void ledHeartbeat()
{
    static unsigned long lastBeat = 0;

    unsigned long timeNow = millis();
    if(timeNow - lastBeat >= 1000) {
        lastBeat = timeNow;
        sLedIsOn = !sLedIsOn;
        if(sLedIsOn) {
            PORTB &= ~(1 << 5);
        } else {
            PORTB |= (1 << 5);
        }
    }
}

void loop()
{
    ledHeartbeat();
    usbPoll();

    if(usbInterruptIsReady()) {
        PORTB |= (1 << 1);

        if(sReportPending) {
            sendReportBlocking();
        } else if(!sInputReportsSuspended) {
            prepareInputReport();
            sendReportBlocking();
        }

        PORTB &= ~(1 << 1);
    }
}
