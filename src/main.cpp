#include <Arduino.h>

extern "C" {
    #include <usbdrv/usbdrv.h>
    #include "descriptors.h"

    // We declare this to be used by V-USB's 'osccal.h' oscilator calibration
    // routine.
    uint8_t lastTimer0Value = 0;

    void usbFunctionRxHook(const uchar *data, const uchar len);
}

#define DEBUG_PRINT_ON 0
#if DEBUG_PRINT_ON
#define debugPrint(...) Serial.print(__VA_ARGS__)
#define debugPrintOn() false
#else
#define debugPrintOn() true
#define debugPrint(...)
#endif

void setup()
{
    // Set port 2 outputs. Most of these pind are prescribed by the ATmega's
    // built in SPI communication hardware.
    DDRB |=
        1 << 3 | // PICO (PB3) - Serial data from ATmega to controller.
        1 << 5 | // SCK (PB5) - Serial data clock.
        1 << 2 | // Use PB2 for the controller's ^CS line
        1 << 0   // PB0 for our blinking LED.
    ;

    // Set up the SPI Control Register. Form right to left:
    // SPIE = 0 (SPI interrupt disabled - we'll just poll)
    // SPE  = 1 (SPI enabled)
    // DORD = 1 (Data order: LSB of the data word is transmitted first)
    // MSTR = 1 (Controller/Peripheral Select: Controller mode)
    // CPOL = 1 (Clock Polarity: Leading edge = falling)
    // CPHA = 1 (Clock Phase: Leading edge = setup, trailing ecdge = sample)
    // SPR1 SPR0 = 10 (Serial clock rate: 10 means (F_CPU / 64) -
    //                 so: (12.8MHz / 64) = 200kHz - but we'll double that below).
    SPCR = 0b01111110;

    // Double the SPI rate defined above (so 200kHz * 2 = 400kHz)
    SPSR |= 1 << SPI2X;

    // Need to set the controller's CS, which is active-low, to high so we can
    // pull it low for each transaction - and PICO and SCK should rest at high
    // too.
    PORTB |= 1 << 5 | 1 << 3 | 1 << 2;

    Serial.begin(250000);

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

static uint8_t sReports[2][64] = { 0 };
static uint8_t sReportLengths[2] = { 0 };
static const uint8_t sReportSize = sizeof(sReports[0]);
static uint8_t sCurrentReport = 0;

static bool sReportPending = false;
static bool sInputReportsSuspended = false;
static bool sLedIsOn = true;

static unsigned long sLastSwitchPacketMillis = 0;

static const uint8_t sCommandHistoryLength = 32;
static uint8_t sCommandHistory[sCommandHistoryLength][3] = {0};
static uint8_t sCommandHistoryCursor = 0;

static void halt(uint8_t i, const char *message = NULL)
{
    Serial.print("HALT: 0x");
    Serial.print(i, 16);
    if(message) {
        Serial.print(' ');
        Serial.print(message);
    }
    Serial.print('\n');
    for(uint8_t i = 0; i < sCommandHistoryLength; ++i) {
        Serial.print('\t');
        Serial.print(sCommandHistory[sCommandHistoryCursor][0], 16);
        Serial.print(',');
        Serial.print(sCommandHistory[sCommandHistoryCursor][1], 16);
        Serial.print(',');
        Serial.print(sCommandHistory[sCommandHistoryCursor][2], 16);
        Serial.print('\n');
        if(sCommandHistoryCursor == 0) {
            sCommandHistoryCursor = sCommandHistoryLength - 1;
        } else {
            --sCommandHistoryCursor;
        }
    }

    while(true);
}

static uint8_t *sampleDualShock_P(const uint8_t *toTransmit, const uint8_t toTransmitLength)
{
    // This is static because we return it to the caller.
    static uint8_t toReceive[21] = {0};
    uint8_t toReceiveLen = sizeof(toReceive);

    // Pull-down the ~CS ('Attention') line.
    PORTB &= ~(1 << 2);

    // Give the controller time to notice.
    delayMicroseconds(10);

    // Loop using SPI to send/receive one byte at a time.
    uint8_t byteIndex = 0;
    do {
        // Put what we want to send into the SPI Data Register.
        if(byteIndex == 0) {
            // All transactions start with a 1 byte.
            SPDR = 0x01;
        } else if(byteIndex <= toTransmitLength) {
            // If we still have [part of] a command to transmit.
            SPDR = pgm_read_byte(toTransmit + (byteIndex - 1));
        } else {
            // Otherwise, pad with 0s like a PS1 would.
            SPDR = 0x00;
        }

        // Wait for the SPI hardware do its thing:
        // Loop until SPIF, the SPI Interrupt Flag in the SPI State Register,
        // is set, signifying the SPI transaction is complete.
        while(!(SPSR & (1<<SPIF)));

        // Grab the received byte from the SPI Date register.
        // This has the side-effect of clearing the SPIF flag.
        const uint8_t received = SPDR;

        // Process what we've received.
        if(byteIndex == 1) {
            // The byte in position 1 contains the length to expect after
            // the header.
            toReceiveLen = min(toReceiveLen, ((received & 0xf) * 2) + 3);
        }
        if(byteIndex == 2) {
            // This should always be 0x5a.
            if(received != 0x5a) {
                byteIndex = 0;
                memset(toReceive, 0, sizeof(toReceive));
                break;
            }
        }

        toReceive[byteIndex] = received;
        ++byteIndex;

        if(byteIndex < toReceiveLen) {
            // The Dual Shock requires us to wait a bit between packets.
            delayMicroseconds(10);
        }
    } while(byteIndex < toReceiveLen);

    // ~CS line ('Attention') needs to be raised to its inactive state between
    // each transaction.
    PORTB |= 1 << 2;

    DualShockReport *receivedReport = (DualShockReport *)toReceive;
    if(byteIndex <= offsetof(DualShockReport, rightStickX)) {
        // If we didn't get any analog reports (the controller is in digital
        // mode), set the analog sticks to centered.
        receivedReport->rightStickX = 0x80;
        receivedReport->rightStickY = 0x80;
        receivedReport->leftStickX = 0x80;
        receivedReport->leftStickY = 0x80;
    }

    return toReceive;
}

static void deadZoneStickPosition(uint8_t *x, uint8_t *y) {
    const int16_t xDiff = (int16_t)*x - 0x80;
    const int16_t yDiff = (int16_t)*y - 0x80;
    const int16_t distance_squared = (xDiff * xDiff) + (yDiff * yDiff);

    static const int16_t deadZoneRadiusSquared = floor(0xff / 10);

    if (distance_squared < deadZoneRadiusSquared) {
        *x = 0x80;
        *y = 0x80;
    }
}

static void convertDualShockToSwitch(const DualShockReport *dualShockReport, SwitchReport *switchReport)
{
    memset(switchReport, 0, sizeof(SwitchReport));

    // Fake values.
    switchReport->connectionInfo = 0x1;
    switchReport->batteryLevel = 0x8;

    // Dual Shock buttons are 'active low' (0 = on, 1 = off), so we need to
    // invert their value before assigning to the Switch report.

    switchReport->yButton = !dualShockReport->squareButton;
    switchReport->xButton = !dualShockReport->triangleButton;
    switchReport->bButton = !dualShockReport->crossButton;
    switchReport->aButton = !dualShockReport->circleButton;
    switchReport->rShoulderButton = !dualShockReport->r1Button;
    switchReport->zRShoulderButton = !dualShockReport->r2Button;

    switchReport->minusButton = !dualShockReport->selectButton;
    switchReport->plusButton = !dualShockReport->startButton;
    switchReport->rStickButton = !dualShockReport->r3Button;
    switchReport->lStickButton = !dualShockReport->l3Button;

    switchReport->downButton = !dualShockReport->downButton;
    switchReport->upButton = !dualShockReport->upButton;
    switchReport->rightButton = !dualShockReport->rightButton;
    switchReport->leftButton = !dualShockReport->leftButton;
    switchReport->lShoulderButton = !dualShockReport->l1Button;
    switchReport->zLShoulderButton = !dualShockReport->l2Button;


    // The Switch has 12-bit analog sticks. The Dual Shock has 8-bit.
    // We replicate the high 4 bits into the bottom 4 bits of the
    // Switch report, so that e.g. 0xFF maps to 0XFFF and 0x00 maps to 0x000
    // The mid-point of 0x80 maps to 0x808, which is a bit off - but
    // 0x80 is in fact off too: the real midpoint of [0x00 - 0xff] is 0x7f.8
    // (using a hexadecimal point there, like a decimal point).
    //
    // Byte (nybble?) order of the values here strikes me as a bit weird.
    // If the three bytes (with two nybbles each) are AB CD EF,
    // the decoded 12-bit values are DAB, EFC. It makes more sense 'backwards'?

    uint8_t leftStickX = dualShockReport->leftStickX;
    uint8_t leftStickY = 0xff - dualShockReport->leftStickY;
    deadZoneStickPosition(&leftStickX, &leftStickY);
    switchReport->leftStick[2] = leftStickY;
    switchReport->leftStick[1] = (leftStickY & 0xf0) | (leftStickX >> 4);
    switchReport->leftStick[0] = (leftStickX << 4) | (leftStickX >> 4);

    uint8_t rightStickX = dualShockReport->rightStickX;
    uint8_t rightStickY = 0xff - dualShockReport->rightStickY;
    deadZoneStickPosition(&rightStickX, &rightStickY);
    switchReport->rightStick[2] = rightStickY;
    switchReport->rightStick[1] = (rightStickY & 0xf0) | (rightStickX >> 4);
    switchReport->rightStick[0] = (rightStickX << 4) | (rightStickX >> 4);
}

static uint8_t prepareInputSubReportInBuffer(uint8_t *buffer)
{
    static const PROGMEM uint8_t toTransmit[] = { 0x42 };
    const uint8_t *received = sampleDualShock_P(toTransmit, sizeof(toTransmit));

    convertDualShockToSwitch((const DualShockReport *)received, (SwitchReport *)buffer);

    return sizeof(SwitchReport);
}

static void prepareInputReport()
{
    uint8_t *report = sReports[sCurrentReport];
    report[0] = 0x30;
    report[1] = usbSofCount;

    // Prepare the input report in the Pro Controller's format.
    const uint8_t innerReportLength = prepareInputSubReportInBuffer(&report[2]);

    sReportLengths[sCurrentReport] = 2 + innerReportLength;
    sReportPending = true;
}

static void report_P(uint8_t reportId, uint8_t reportCommand, const uint8_t *reportIn, uint8_t reportInLen)
{
    if(sReportPending) {
        halt(0, "Report Clash");
        return;
    }

    uint8_t *report = sReports[sCurrentReport];
    report[0] = reportId;
    report[1] = reportCommand;
    memcpy_P(&report[2], reportIn, reportInLen);

    sReportLengths[sCurrentReport] = 2 + reportInLen;
    sReportPending = true;
}

static void reportUart_F(uint8_t ack, uint8_t subCommand, const uint8_t *reportIn, uint8_t reportInLen,  void *(*copyFunction)(void *, const void *, size_t))
{
    // '_F' - means pass in function to use for memcpying.

    if(sReportPending) {
        halt(0, "Report Clash");
        return;
    }

    uint8_t *report = sReports[sCurrentReport];
    report[0] = 0x21;
    report[1] = usbSofCount;

    const uint8_t inputBufferLength = prepareInputSubReportInBuffer(&report[2]);

    report[2 + inputBufferLength] = ack;
    report[3 + inputBufferLength] = subCommand;

    uint8_t reportSize = 4 + inputBufferLength;

    if(reportInLen == 0) {
        report[reportSize] = 0x00;
        ++reportSize;
    } else {
        const uint8_t reportSizeBeforeCopy = reportSize;
        reportSize += reportInLen;
        if(reportSize > sReportSize) {
            halt(0, "Report Too Big");
            return;
        }
        copyFunction(&report[reportSizeBeforeCopy], reportIn, reportInLen);
    }

    sReportLengths[sCurrentReport] = reportSize;
    sReportPending = true;
}

static void reportUart_P(uint8_t ack, uint8_t subCommand, const uint8_t *reportIn, uint8_t reportInLen)
{
    return reportUart_F(ack, subCommand, reportIn, reportInLen, memcpy_P);
}

static void reportUart(uint8_t ack, uint8_t subCommand, const uint8_t *reportIn, uint8_t reportInLen)
{
    return reportUart_F(ack, subCommand, reportIn, reportInLen, memcpy);
}

static void reportUartSpi_P(uint16_t address, uint8_t length, const uint8_t *replyData, uint8_t replyDataLength)
{
    if(replyDataLength != length) {
        Serial.print(address, 16);
        Serial.print(length, 16);
        Serial.print(replyDataLength, 16);
    }

    const uint8_t bufferLength = replyDataLength + 5;
    uint8_t buffer[bufferLength];
    buffer[0] = (uint8_t)(address & 0xFF);
    buffer[1] = (uint8_t)(address >> 8);
    buffer[2] = 0;
    buffer[3] = 0;
    buffer[4] = (uint8_t)(replyDataLength);
    memcpy_P(&buffer[5], replyData, replyDataLength);

    return reportUart(0x90, 0x10, buffer, bufferLength);
}

static void usbFunctionWriteOutOrAbandon(uchar *data, uchar len, bool shouldAbandonAccumulatedReport)
{
    static uint8_t reportId;
    static uint8_t reportAccumulationBuffer[sReportSize];
    static uint8_t accumulatedReportBytes = 0;

    if(shouldAbandonAccumulatedReport) {
        // The host has told us it's stalling the endpoint.
        // Abandon reception of any in-progress reports - we're not going to
        // get the rest of it :-(
        accumulatedReportBytes = 0;
        return;
    }

    if(accumulatedReportBytes == 0) {
        reportId = data[0];
        debugPrint("\r\n\n");
    }

    // Different reports are different lengths!
    // Work out if this one is complete.
    bool reportComplete = false;
    switch(reportId) {
    case 0x00: // Unknown.
        if(len != 2 || accumulatedReportBytes != 0) { // Always only 2 bytes?
            halt(reportId, "Unexpected report length for 0x00");
        }
        reportComplete = true;
        break;
    case 0x80: // Regular commands.
        if(len != 2 || accumulatedReportBytes != 0) { // Always only 2 bytes?
            halt(reportId, "Unexpected report length for 0x80");
        }
        reportComplete = true;
        break;
    case 0x01: // 'UART' commands.
    case 0x10: // Unknown. They contain an incrementing number. Keep-alive?
        if(accumulatedReportBytes == 8) { // These are always two packets long, but the length varies.
            reportComplete = true;
        }
        break;
    default:
        halt(reportId, "Unexpected report ID");
        return;
    }

    debugPrint('>');

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
    uint8_t uartCommand = 0;
    debugPrint(' ');
    debugPrint(reportId, 16);
    debugPrint(':');
    debugPrint(commandOrSequenceNumber, 16);

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
            debugPrint('?');
            break;
        }
    } break;
    case 0x01: {
        // A 'UART' request.

        uartCommand = reportIn[10];
        debugPrint('|');
        debugPrint(uartCommand, 16);

        switch(uartCommand) {
        case 0x01: {
            // Bluetooth manual pairing (?)
            reportUart_P(0x81, uartCommand, NULL, 0);
        } break;
        case 0x02: {
            // Request device info
            static const PROGMEM uint8_t reply[] = {
                0x03, 0x48, // FW Version
                0x03, // Pro Controller
                0x02, // Unknown (Always 0x02)
                0xc7, 0xa3, 0x22, 0x53, 0x23, 0x43, // 6 bytes of MAC address
                0x03, // Unknown
                0x01, // 0x01 = Use colors from SPI (below).
            };
            reportUart_P(0x82, uartCommand, reply, sizeof(reply));
        } break;
        case 0x10: {
            // NVRAM read
            const uint16_t address = reportIn[11] | reportIn[12] << 8;
            const uint16_t length = reportIn[15];
            debugPrint('<');
            debugPrint(address, 16);
            debugPrint('-');
            debugPrint(length, 16);

            const uint8_t *spiReply = NULL;
            uint8_t spiReplyLength = 0;
            switch(address) {
            case 0x6000: { // Serial number in non-extended ASCII. If first byte is >= x80, no S/N.
                static const PROGMEM uint8_t reply[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
                spiReply = reply;
                spiReplyLength = sizeof(reply);
            } break;
            case 0x6020: { // Sixaxis config
                static const PROGMEM uint8_t reply[] = { 0xD3, 0xFF, 0xD5, 0xFF, 0x55, 0x01, 0x00, 0x40, 0x00, 0x40, 0x00, 0x40, 0x19, 0x00, 0xDD, 0xFF, 0xDC, 0xFF, 0x3B, 0x34, 0x3B, 0x34, 0x3B, 0x34 };
                spiReply = reply;
                spiReplyLength = sizeof(reply);
            } break;
            case 0x603d: { // Left/right stick calibration (9 values each), body, buttons (this overlaps with the range for the next address, below)
                static const PROGMEM uint8_t reply[] = { 0xba, 0x15, 0x62, 0x11, 0xb8, 0x7f, 0x29, 0x06, 0x5b, 0xff, 0xe7, 0x7e, 0x0e, 0x36, 0x56, 0x9e, 0x85, 0x60, 0xff, 0x32, 0x32, 0x32, 0xff, 0xff, 0xff };
                spiReply = reply;
                spiReplyLength = sizeof(reply);
            } break;
            case 0x6050: { // 24-bit (3 byte) RGB colors - body, buttons, left grip, right grip (and an extra 0xff? Maybe it signifies whether the grips are colored?)
                static const PROGMEM uint8_t reply[] = { 0x32, 0x32, 0x32, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
                spiReply = reply;
                spiReplyLength = sizeof(reply);
            } break;
            case 0x6080: { // "Factory Sensor and Stick device parameters"
                static const PROGMEM uint8_t reply[] = { 0x50, 0xfd, 0x00, 0x00, 0xc6, 0x0f, 0x0f, 0x30, 0x61, 0x96, 0x30, 0xf3, 0xd4, 0x14, 0x54, 0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63 };
                spiReply = reply;
                spiReplyLength = sizeof(reply);
            } break;
            case 0x6098: { // "Factory Stick device parameters 2, normally the same as 1, even in Pro Controller" [note this is indeed the same as the stick parameters above]
                static const PROGMEM uint8_t reply[] = { 0x0f, 0x30, 0x61, 0x96, 0x30, 0xf3, 0xd4, 0x14, 0x54, 0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63 };
                spiReply = reply;
                spiReplyLength = sizeof(reply);
            } break;
            case 0x8010: { // User stick calibration
                static const PROGMEM uint8_t reply[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xb2, 0xa1 };
                spiReply = reply;
                spiReplyLength = sizeof(reply);
            } break;
            case 0x8028: { // Six axis calibration.
                static const PROGMEM uint8_t reply[] = { 0xbe, 0xff, 0x3e, 0x00, 0xf0, 0x01, 0x00, 0x40, 0x00, 0x40, 0x00, 0x40, 0xfe, 0xff, 0xfe, 0xff, 0x08, 0x00, 0xe7, 0x3b, 0xe7, 0x3b, 0xe7, 0x3b };
                spiReply = reply;
                spiReplyLength = sizeof(reply);
            } break;
            default:
                halt(address & 0xff, "Unexpected SPI read subcommand");
                break;
            }
            reportUartSpi_P(address, length, spiReply, spiReplyLength);
        } break;
        case 0x04: // Trigger buttons elapsed time (?)
            reportUart_P(0x83, uartCommand, NULL, 0);
            break;
        case 0x48: // Set vibration enabled state
            reportUart_P(0x82, uartCommand, NULL, 0);
            break;
        case 0x21: {// Set NFC/IR MCU config
            // Request device info
            static const PROGMEM uint8_t reply[] = { 0x01, 0x00, 0xFF, 0x00, 0x08, 0x00, 0x1B, 0x01 };
            reportUart_P(0xa0, uartCommand, reply, sizeof(reply));
        } break;
        case 0x00: // Do nothing (return report)
        case 0x03: // Set input report mode
        case 0x08: // Set shipment low power state
        case 0x22: // Set NFC/IR MCU state
        case 0x30: // Set player lights
        case 0x38: // Set HOME light
        case 0x40: // Set IMU enabled state
        case 0x41: // Set IMU sesitivity
            // Unhandled, but we'll tell the switch we've handled it...
            reportUart_P(0x80, uartCommand, NULL, 0);
            break;
        default:
            reportUart_P(0x80, uartCommand, NULL, 0);
            halt(uartCommand, "Unexpected UART subcommand");
            break;
        }
    } break;
    case 0x10:
        // Keep-alive?
        break;
    case 0x00:
        // Not quite sure why we sometimes get reports with a 0 id.
        break;
    default:
        // We should never reach here because we should've halted above.
        halt(reportId, "Unexpected report ID");
        break;
    }

    // We store a small history of received commands to aid with debugging.
    if(reportId == 0x10) {
        // Because we get so many of these, coalesce runs of them in the history
        // buffer. We use the second and third btes to store how many we've seen
        // in a row.
        if(sCommandHistory[sCommandHistoryCursor][0] != 0x10) {
            sCommandHistoryCursor = (sCommandHistoryCursor + 1) % sCommandHistoryLength;
            sCommandHistory[sCommandHistoryCursor][0] = 0x10;
            sCommandHistory[sCommandHistoryCursor][1] = 0;
            sCommandHistory[sCommandHistoryCursor][2] = 0;
        }
        uint16_t count = ((uint16_t)(sCommandHistory[sCommandHistoryCursor][1])) << 8 | (uint16_t)(sCommandHistory[sCommandHistoryCursor][2]);
        ++count;
        sCommandHistory[sCommandHistoryCursor][1] = count >> 8;
        sCommandHistory[sCommandHistoryCursor][2] = (count & 0xff);
    } else {
        sCommandHistoryCursor = (sCommandHistoryCursor + 1) % sCommandHistoryLength;
        sCommandHistory[sCommandHistoryCursor][0] = reportId;
        sCommandHistory[sCommandHistoryCursor][1] = commandOrSequenceNumber;
        sCommandHistory[sCommandHistoryCursor][2] = uartCommand;
    }

    // If we don't see packets for a while, something's gone wrong.
    // We check this elsewhere to be sure packets are continuning to be
    // received.
    sLastSwitchPacketMillis = millis();

    // Let's see how the 12.8MHz tuning for the internal oscillator is doing.
    debugPrint(' ');
    debugPrint(OSCCAL, 16);
}

void usbFunctionWriteOut(uchar *data, uchar len)
{
    usbFunctionWriteOutOrAbandon(data, len, false);
}

void usbFunctionRxHook(const uchar *data, const uchar len)
{
    if(usbRxToken == USBPID_SETUP) {
        const usbRequest_t *request = (const usbRequest_t *)data;
        if((request->bmRequestType & USBRQ_RCPT_MASK) == USBRQ_RCPT_ENDPOINT &&
            request->bRequest == USBRQ_CLEAR_FEATURE &&
            request->wIndex.bytes[0] == 1) {
            // This is an clear of ENDPOINT_HALT for OUT endpoint 1
            // (i.e. the one to us from the host).
            // We need to abandon any old in-progress report reception - we
            // won't get the rest of the report from before the stall.
            //
            // We could also check the request->vWalue here for the specific
            // feature that's being cleared - but HALT is the only feature that
            // _can_ be cleared on an interrupt endpoint, so it's not actually
            // necessary to check.
            debugPrint("\n!Clear HALT ");
            debugPrint(request->wIndex.bytes[0], 16);
            debugPrint("!\n");
            usbFunctionWriteOutOrAbandon(NULL, 0, true);
        }
    }

    if(usbCrc16(data, len + 2) != 0x4FFE) {
        halt(0, "CRC error!");
    }
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
    const uint8_t *report = sReports[reportIndex];
    const uint8_t actualReportLength = sReportLengths[reportIndex];

    // The next report can be filled in while we send this one.
    // This doesn't seem to happen very much.
    sCurrentReport = (reportIndex + 1) % 2;
    sReportPending = false;

    uint8_t reportCursor = 0;
    uint8_t packetSize;
    do {
        usbPoll();
        if(usbInterruptIsReady()) {

            packetSize = min(8, actualReportLength - reportCursor);
            usbSetInterrupt(&((uchar *)report)[reportCursor], packetSize);
            reportCursor += packetSize;

        }
    } while(!(packetSize < 8 || reportCursor == sReportSize));

    sReportLengths[reportIndex] = 0;
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
            PORTB &= ~(1 << 0);
        } else {
            PORTB |= (1 << 0);
        }
    }

    if(sLastSwitchPacketMillis != 0 && timeNow - sLastSwitchPacketMillis > 5000) {
        halt(timeNow - sLastSwitchPacketMillis, "Packets stopped!");
    }
}

void loop()
{
    ledHeartbeat();
    usbPoll();
    if(usbInterruptIsReady()) {
        if(!sReportPending) {
            prepareInputReport();
        }
        sendReportBlocking();
    }
}
