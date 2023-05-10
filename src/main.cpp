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

    MCUCR |= 1 << ISC11 | 1 << ISC10;
    GICR |= 1 << INT1;

    // Need to set the controller's CS, which is active-low, to high so we can
    // pull it low for each transaction - and PICO and SCK should rest at high
    // too.
    // Also set the LED pin high (which will switch it off).
    PORTB |= 1 << 5 | 1 << 3 | 1 << 2 | 1 << 0;

    Serial.begin(266667);

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

static const uint8_t sReportSize = 64;

static uint8_t sReports[2][sReportSize];
static uint8_t sReportsInputReportPosition[2] = { 0 };
static uint8_t sReportLengths[2] = { 0 };
static uint8_t sCurrentReport = 0;
static bool sReportPending = false;

static bool sInputReportsSuspended = false;

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

// The Dual Shock's ACK line is connected to pin 5, INT1.
static volatile bool sDualShockAcknowledgeReceived = false;
ISR(INT1_vect, ISR_NOBLOCK)
{
    // Set the flag so that the main code can know it's done.
    sDualShockAcknowledgeReceived = true;
}

static uint8_t dualShockCommand_P(const uint8_t *command, const uint8_t commandLength,
    uint8_t *toReceive, const uint8_t toReceiveLength)
{
    // Pull-down the ~CS ('Attention') line.
    PORTB &= ~(1 << 2);

    // Give the controller a little time to notice (this seems to be necessary
    // for reliable communication).
    delayMicroseconds(20);

    // Loop using SPI to send/receive each byte in the command.
    uint8_t byteIndex = 0;
    uint8_t reportedTransactionLength = 2;
    bool byteAcknowledged = false;
    do {
        sDualShockAcknowledgeReceived = false;

        // Put what we want to send into the SPI Data Register.
        if(byteIndex == 0) {
            // All transactions start with a 1 byte.
            SPDR = 0x01;
        } else if(byteIndex <= commandLength) {
            // If we still have [part of] a command to transmit.
            SPDR = pgm_read_byte(&command[byteIndex - 1]);
        } else {
            // Otherwise, pad with 0s like a PS1 would.
            SPDR = 0x00;
        }

        // Wait for the SPI hardware do its thing:
        // Loop until SPIF, the SPI Interrupt Flag in the SPI State Register,
        // is set, signifying the SPI transaction is complete.
        while(!(SPSR & (1 << SPIF)));

        // Grab the received byte from the SPI Data register.
        // This has the side-effect of clearing the SPIF flag (see above).
        const uint8_t received = SPDR;

        // Process what we've received.
        if(byteIndex == 1) {
            // The byte in position 1 contains the length to expect _after
            // the header_ in its lower nybble.
            reportedTransactionLength = ((received & 0xf) * 2) + 3;
        }

        if(byteIndex < toReceiveLength) {
            // Store thebyte in the buffer we were passed, if there's room.
            toReceive[byteIndex] = received;
        }

        ++byteIndex;

        byteAcknowledged = sDualShockAcknowledgeReceived;
        if(byteIndex < reportedTransactionLength) {
            // Wait for the Dual Shock to acknowledge the byte.
            // All bytes except the last one are acknowledged.
            // sDualShockAcknowledgeReceived will be set to true when the
            // acknowledge line is raised high and the interrupt handler for
            // ISR1 is called - see above.
            uint8_t microsBefore = micros();
            while(!byteAcknowledged) {
                if((uint8_t)(micros() - microsBefore) > 100) {
                    // Sometimes, especially when it's in command mode, the Dual
                    // Shock seems to get into a bad state and 'give up' on
                    // some packets. Detect this and bail.
                    // We'll return failure from this function, and it's up to
                    // the caller to try again if they want to.
                    byteIndex = 0;
                    break;
                }
                byteAcknowledged = sDualShockAcknowledgeReceived;
            }
        }
    } while(byteAcknowledged);

    // Despite the fact that the controller doens't raise the acknowledge line
    // for the last byte, we still seem to need to wait a bit for communication
    // to be reliable :-|.
    delayMicroseconds(20);

    // ~CS line ('Attention') needs to be raised to its inactive state between
    // each transaction.
    PORTB |= 1 << 2;

    return byteIndex;
}

static void deadZoneizeStickPosition(uint8_t *x, uint8_t *y) {
    const int16_t xDiff = (int16_t)*x - 0x80;
    const int16_t yDiff = (int16_t)*y - 0x80;
    const int16_t distance_squared = (xDiff * xDiff) + (yDiff * yDiff);

    static const int16_t deadZoneRadiusSquared = pow(ceil(0xff / 10), 2);

    if (distance_squared <= deadZoneRadiusSquared) {
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
    deadZoneizeStickPosition(&leftStickX, &leftStickY);
    switchReport->leftStick[2] = leftStickY;
    switchReport->leftStick[1] = (leftStickY & 0xf0) | (leftStickX >> 4);
    switchReport->leftStick[0] = (leftStickX << 4) | (leftStickX >> 4);

    uint8_t rightStickX = dualShockReport->rightStickX;
    uint8_t rightStickY = 0xff - dualShockReport->rightStickY;
    deadZoneizeStickPosition(&rightStickX, &rightStickY);
    switchReport->rightStick[2] = rightStickY;
    switchReport->rightStick[1] = (rightStickY & 0xf0) | (rightStickX >> 4);
    switchReport->rightStick[0] = (rightStickX << 4) | (rightStickX >> 4);
}

static void prepareInputSubReportInBuffer(uint8_t *buffer)
{
    SwitchReport *switchReport = (SwitchReport *)buffer;

    struct DualShockCommand {
        const uint8_t length;
        const uint8_t commandSequence[];
    };

    static const PROGMEM DualShockCommand pollCommand[] = { { 1, { 0x42 } } };
    static const PROGMEM DualShockCommand enterConfigCommand[] = { { 3, { 0x43, 0x00, 0x01 } } };
    static const PROGMEM DualShockCommand exitConfigCommand[] = { { 3, { 0x43, 0x00, 0x00 } } };
    static const PROGMEM DualShockCommand switchToAnalogCommand[] = { { 3, { 0x44, 0x00, 0x01 } } };

    static const PROGMEM DualShockCommand *const enterAnalogCommandSequence[] = {
        enterConfigCommand,
        switchToAnalogCommand,
        exitConfigCommand,
    };

    static DualShockReport dualShockReports[2] = { EMPTY_DUAL_SHOCK_REPORT, EMPTY_DUAL_SHOCK_REPORT };
    static uint8_t previousDualShockReportIndex = 0;

    static const DualShockCommand * const *commandQueue;
    static uint8_t commandQueueCursor = 0;
    static uint8_t commandQueueLength = 0;

    static bool analogButtonIsPressed = false;
    static uint8_t analogButtonPressSofCount = 0;

    uint8_t replyLength = 0;
    uint8_t thisDualShockReportIndex = (uint8_t)(previousDualShockReportIndex + 1) % 2;

    const bool executingCommandQueue = (commandQueueCursor < commandQueueLength);
    const uint8_t *commandToExecute_P;

    if(!executingCommandQueue) {
        // If there's no command queue (the usual case), we just poll
        // controller state.
        commandToExecute_P = (const uint8_t *)pollCommand;
    } else {
        commandToExecute_P = (const uint8_t *)pgm_read_ptr(commandQueue + commandQueueCursor);
    }

    replyLength = dualShockCommand_P(commandToExecute_P + offsetof(DualShockCommand, commandSequence),
                                     pgm_read_byte(commandToExecute_P + offsetof(DualShockCommand, length)),
                                     (uint8_t *)&dualShockReports[thisDualShockReportIndex],
                                     sizeof(DualShockReport));

    if(!executingCommandQueue) {
        if(replyLength >= 2) {
            uint8_t mode = dualShockReports[thisDualShockReportIndex].deviceMode;

            if(mode != 0x7) {
                // If we're _not_ in analog mode, initiate the sequence of
                // commands that will cause the controller to switch to analog
                // mode. One command is performed every time this function
                // is called.
                commandQueue = enterAnalogCommandSequence;
                commandQueueCursor = 0;
                commandQueueLength = 3;

                if(mode == 0x4) {
                    // If we're in digital  mode, it means the user pressed the
                    // analog button. We treat this as a home button press.
                    // Because we can't get any information about when the
                    // button is _released_, we record the time (in USB
                    // 1ms SOFs) when the press ocurred and switch it on
                    // for a few ms (see below).
                    analogButtonIsPressed = true;
                    analogButtonPressSofCount = usbSofCount;
                }
            }
        }
    } else {
        if(replyLength >= 2 && dualShockReports[thisDualShockReportIndex].deviceMode == 0xF) {
            // On to the next command!
            ++commandQueueCursor;
        } else {
            // The dual shock seems prome to failure to enter comamnd mode,
            // and to execute commands. If something's gone wrong, just
            // start the queue again.
            commandQueueCursor = 0;
        }
    }

    if(replyLength != sizeof(DualShockReport) || dualShockReports[thisDualShockReportIndex].deviceMode != 0x7) {
        // Not an analog report. We'll just use the previous one until the
        // controller gets back into a good state.
        thisDualShockReportIndex = previousDualShockReportIndex;
    }

    convertDualShockToSwitch(&dualShockReports[thisDualShockReportIndex], switchReport);

    if(analogButtonIsPressed) {
        if((uint8_t)(usbSofCount - analogButtonPressSofCount) < 64) {
            // Because we don't get any information about when the analog button
            // is _released_ (see above) we use the 1ms USB SOF count to pretend
            // the home button was pressed for a few ms.
            switchReport->homeButton = 1;
        } else {
            analogButtonIsPressed = false;
        }
    }

    previousDualShockReportIndex = thisDualShockReportIndex;
}

static void prepareInputReport()
{
    uint8_t *report = sReports[sCurrentReport];
    report[0] = 0x30;
    report[1] = usbSofCount; // This is meant to be an increasing timestamp - the SOF count is just a handy available 1ms counter.

    sReportsInputReportPosition[sCurrentReport] = 2;
    sReportLengths[sCurrentReport] = 2 + sizeof(SwitchReport);
    sReportPending = true;
}

static void prepareRegularReplyReport_P(uint8_t reportId, uint8_t reportCommand, const uint8_t *reportIn, uint8_t reportInLen)
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

static void prepareUartReplyReport_F(uint8_t ack, uint8_t subCommand, const uint8_t *reportIn, uint8_t reportInLen,  void *(*copyFunction)(void *, const void *, size_t))
{
    // '_F' - means pass in function to use for memcpying.

    if(sReportPending) {
        halt(0, "Report Clash");
        return;
    }

    uint8_t reportLength = 0;
    uint8_t *report = sReports[sCurrentReport];
    report[reportLength++] = 0x21;
    report[reportLength++] = usbSofCount;

    sReportsInputReportPosition[sCurrentReport] = reportLength;
    reportLength += sizeof(SwitchReport);

    report[reportLength++] = ack;
    report[reportLength++] = subCommand;

    if(reportInLen == 0) {
        report[reportLength++] = 0x00;
    } else {
        const uint8_t reportSizeBeforeCopy = reportLength;
        reportLength += reportInLen;
        if(reportLength > sReportSize) {
            halt(0, "Report Too Big");
            return;
        }
        copyFunction(&report[reportSizeBeforeCopy], reportIn, reportInLen);
    }

    sReportLengths[sCurrentReport] = reportLength;
    sReportPending = true;
}

static void prepareUartReplyReport_P(uint8_t ack, uint8_t subCommand, const uint8_t *reportIn, uint8_t reportInLen)
{
    return prepareUartReplyReport_F(ack, subCommand, reportIn, reportInLen, memcpy_P);
}

static void prepareUartReplyReport(uint8_t ack, uint8_t subCommand, const uint8_t *reportIn, uint8_t reportInLen)
{
    return prepareUartReplyReport_F(ack, subCommand, reportIn, reportInLen, memcpy);
}

static void prepareUartSpiReplyReport_P(uint16_t address, uint8_t length, const uint8_t *replyData, uint8_t replyDataLength)
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

    return prepareUartReplyReport(0x90, 0x10, buffer, bufferLength);
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
            prepareRegularReplyReport_P(0x81, command, reply, sizeof(reply));
        } break;
        case 0x02:
            prepareRegularReplyReport_P(0x81, command, NULL, 0);
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
            prepareUartReplyReport_P(0x81, uartCommand, NULL, 0);
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
            prepareUartReplyReport_P(0x82, uartCommand, reply, sizeof(reply));
        } break;
        case 0x10: {
            // 'SPI' NVRAM read
            // ('SPI' because it's NVRAM connected by SPI in a real Pro Controller)
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
            prepareUartSpiReplyReport_P(address, length, spiReply, spiReplyLength);
        } break;
        case 0x04: // Trigger buttons elapsed time (?)
            prepareUartReplyReport_P(0x83, uartCommand, NULL, 0);
            break;
        case 0x48: // Set vibration enabled state
            prepareUartReplyReport_P(0x82, uartCommand, NULL, 0);
            break;
        case 0x21: {// Set NFC/IR MCU config
            // Request device info
            static const PROGMEM uint8_t reply[] = { 0x01, 0x00, 0xFF, 0x00, 0x08, 0x00, 0x1B, 0x01 };
            prepareUartReplyReport_P(0xa0, uartCommand, reply, sizeof(reply));
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
            prepareUartReplyReport_P(0x80, uartCommand, NULL, 0);
            break;
        default:
            prepareUartReplyReport_P(0x80, uartCommand, NULL, 0);
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

#if DEBUG_PRINT_ON
static uint8_t transmittedReportsCount = 0;
#endif

// Call regularly to blink the LED every 1 second, if the USB connection is
// active.
static void ledHeartbeat()
{
    static uint8_t quarterSecondCount = 0;
    static uint8_t lastQuarterSofCount = 0;
    static uint8_t lastLedToggleQuarterSecondCount = 0;

    uint8_t timeNow = usbSofCount;
    if((uint8_t)(timeNow - lastQuarterSofCount) == 250) {
        ++quarterSecondCount;
        lastQuarterSofCount = timeNow;
    }

    if((uint8_t)(quarterSecondCount - lastLedToggleQuarterSecondCount) == 4) {
        lastLedToggleQuarterSecondCount = quarterSecondCount;
        bool ledIsOn = ((PORTB & (1 << 0)) != 0);
        if(ledIsOn) {
            PORTB &= ~(1 << 0);
        } else {
            PORTB |= (1 << 0);
        }

#if DEBUG_PRINT_ON
        Serial.println("\nFPS: ");
        Serial.println(transmittedReportsCount, 10);
        transmittedReportsCount = 0;
#endif
    }
}

void loop()
{
    ledHeartbeat();
    usbPoll();

    if(usbInterruptIsReady()) {
        // Although V-USB is ready for us to give it the packet to transmit on
        // the next interrupt, we need less than 1ms to prepare, so we can wait
        // until the next 1ms SOF is received before preparing the packet for
        // a _little_ bit lower latency.

        static uint8_t preparePacketAtSofCount = 0;
        const uint8_t thisSofCount = usbSofCount;
        if(!((uint8_t)(preparePacketAtSofCount - thisSofCount) <= 1)) {
            // We haven't already scheduled packet preparation, so schedule it
            // for the next SOF.
            preparePacketAtSofCount = thisSofCount + 1;
        } else if(thisSofCount == preparePacketAtSofCount) {
            // It's time to provide a packet to V-USB.

            // It takes multiple interrupts to send one report, so we keep track
            // of the report we're sending, and our position within it, in these
            // static variables.
            static uint8_t transmittingReportIndex = 0;
            static uint8_t *transmittingReport = NULL;
            static uint8_t transmittingReportLength = 0;
            static uint8_t transmittingReportInputReportPosition = 0;
            static uint8_t transmittingReportTranmissionCursor = 0;

            if(transmittingReport == NULL) {
                if(!sReportPending) {
                    // If there's no report already pending (i.e. no prepared
                    // reply to a command sent by the Switch), prepare
                    // a plain input report.
                    prepareInputReport();
                }

                transmittingReportIndex = sCurrentReport;
                transmittingReport = sReports[transmittingReportIndex];
                transmittingReportLength = sReportLengths[transmittingReportIndex];
                transmittingReportInputReportPosition = sReportsInputReportPosition[transmittingReportIndex];
                transmittingReportTranmissionCursor = 0;

                // If the Switch sends a command while we're sending this
                // report, the reply to it can be prepared while we're sending
                // this report.
                // This doesn't seem to happen very much.
                sCurrentReport = (transmittingReportIndex + 1) % 2;
                sReportPending = false;
            }

            // Low-speed USB can only sent up to eight bytes at a time.
            const uint8_t packetSize = min(8, transmittingReportLength - transmittingReportTranmissionCursor);
            const uint8_t nextReportTransmissionCursor = transmittingReportTranmissionCursor + packetSize;

            if(transmittingReportInputReportPosition != 0 &&
               transmittingReportInputReportPosition >= transmittingReportTranmissionCursor &&
               transmittingReportInputReportPosition < nextReportTransmissionCursor) {
                // We prepare the actual input part of the report - i.e. the
                // state of the Dual Shock's controls - at the last minute
                // in an attempt to get the lowest possible latency.
                prepareInputSubReportInBuffer(transmittingReport + transmittingReportInputReportPosition);
            }

            // Actually provide the packet to V-USB to be sent when the next
            // interrupt arrives.
            usbSetInterrupt(&transmittingReport[transmittingReportTranmissionCursor], packetSize);

            transmittingReportTranmissionCursor = nextReportTransmissionCursor;

            if(packetSize < 8 || transmittingReportTranmissionCursor == sReportSize) {
                // We've reached the end of this report.
                sReportLengths[transmittingReportIndex] = 0;
                sReportsInputReportPosition[transmittingReportIndex] = 0;
                transmittingReport = NULL;

#if DEBUG_PRINT_ON
                ++transmittedReportsCount;
#endif
            }
        }
    }
}
