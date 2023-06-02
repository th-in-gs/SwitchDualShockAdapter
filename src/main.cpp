#include "serial.h"
#include "timer.h"
#include "packedStrings.h"

#include "descriptors.h"
#include "rumble.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

extern "C" {
    #include <usbdrv/usbdrv.h>

    // We declare this to be used by V-USB's 'osccal.h' oscilator calibration
    // routine.
    uint8_t lastTimer0Value = 0;

    void usbFunctionRxHook(const uchar *data, const uchar len);
}

#define DEBUG_PRINT_ON 1
#if DEBUG_PRINT_ON
#define debugPrint(...) serialPrint(__VA_ARGS__)
#define debugPrintStr6(...) serialPrintStr6(__VA_ARGS__)
#define debugPrintHex(...) serialPrintHex(__VA_ARGS__)
#define debugPrintDec(...) serialPrintDec(__VA_ARGS__)
#else
#define debugPrint(...)
#define debugPrintStr6(...)
#define debugPrintHex(...)
#define debugPrintDec(...)
#endif

void setup()
{
    // The oscilator is calibrated to 12.8MHz based on 1ms timing of USB frames
    // by the routines in osctune.h. This seems to be around where things
    // settle, so let's give ourselves a head start on reaching equilibrium
    // by starting here.
    // As well as helping USB communication to start up faster, this also helps
    // to get serial debug output working earlier.
    OSCCAL = 240;

    // Set up sleep mode. This doesn't actually put the device to sleep yet -
    // It just sets the mode that will be used when `sleep_cpu()` is called.
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();

    // Initialize our utility functions.
    timerInit();
    serialInit(266667);

    // Set port 2 outputs. Most of these pins are prescribed by the ATmega's
    // built in SPI communication hardware.
    DDRB |=
        1 << 5 | // SCK (PB5) - Serial data clock.
        1 << 3 | // PICO (PB3) - Serial data from ATmega to controller.
        1 << 2 | // Use PB2 for the controller's 'Chip Select' line
        1 << 0   // PB0 for our blinking debug LED.
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

    // Need to set the controller's 'Chip Select', which is active-low, to high
    // so we can pull it low for each transaction.
    // PICO and SCK should rest at high too.
    // Also set the debug LED pin high (which will switch it off).
    PORTB |= 1 << 5 | 1 << 3 | 1 << 2 | 1 << 0;

    // Set up Interrupt 1 to fire when the the Dual Shock 'Acknowledge' line
    // goes low.
#if __AVR_ATmega8__
    MCUCR |= 1 << ISC11 | 1 << ISC10;
    GICR |= 1 << INT1;
#else
    EICRA |= 1 << ISC11 | 1 << ISC10;
    EIMSK |= 1 << INT1;
#endif

    // Disable interrupts for USB reset.
    cli();

    // Initialize V-USB.
    usbInit();

    // V-USB wiki (http://vusb.wikidot.com/driver-api) says:
    //      "In theory, you don't need this, but it prevents inconsistencies
    //      between host and device after hardware or watchdog resets."
    usbDeviceDisconnect();
    _delay_ms(250);
    usbDeviceConnect();

    // Enable interrupts again.
    sei();
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

static bool sRumbleEnabled = false;
static uint8_t sLowRumbleAmplitude = 0;
static uint8_t sHighRumbleAmplitude = 0;

static void haltStr6(uint8_t i, const uint8_t *messageStr6 = NULL)
{
    serialPrintStr6(STR6("HALT: 0x"), true);
    serialPrintHex(i, true);
    if(messageStr6) {
        serialPrint(' ', true);
        serialPrintStr6(messageStr6, true);
    }
    serialPrint('\n', true);
    for(uint8_t i = 0; i < sCommandHistoryLength; ++i) {
        serialPrint('\t', true);
        serialPrintHex(sCommandHistory[sCommandHistoryCursor][0], true);
        serialPrint(',', true);
        serialPrintHex(sCommandHistory[sCommandHistoryCursor][1], true);
        serialPrint(',', true);
        serialPrintHex(sCommandHistory[sCommandHistoryCursor][2], true);
        serialPrint('\n', true);
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
    // Set the flag so that the main code can know about the acknowledgement.
    sDualShockAcknowledgeReceived = true;
}

static uint8_t dualShockCommand(const uint8_t *command, const uint8_t commandLength,
    uint8_t *toReceive, const uint8_t toReceiveLength)
{
    // Pull-down the ~CS ('Attention') line.
    PORTB &= ~(1 << 2);

    // Give the controller a little time to notice (this seems to be necessary
    // for reliable communication).
    _delay_us(20);

    // Loop using SPI hardware to send/receive each byte in the command.
    uint8_t byteIndex = 0;
    uint8_t reportedTransactionLength = 2;
    bool byteAcknowledged = false;
    bool errored = false;
    do {
        // This will be set to true by the interrupt routine, above,
        // when the Dual Shock sends its acknowledge signal.
        sDualShockAcknowledgeReceived = false;

        // Put what we want to send into the SPI Data Register.
        if(byteIndex == 0) {
            // All transactions start with a 1 byte.
            SPDR = 0x01;
        } else if(byteIndex <= commandLength) {
            // If we still have [part of] a command to transmit.
            SPDR = command[byteIndex - 1];
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
        } else if(byteIndex == 2) {
            // Online docs suggest this is _always_ 0x5a, but in reality it's
            // 0x00 after the 'ANALOG' button has been pressed (unfortunately
            // not _while_ it's being pressed).
            if(received != 0x5a && received != 0x00) {
                    errored = true;
                    break;
            }
        }

        if(byteIndex < toReceiveLength) {
            // Store the byte in the buffer we were passed, if there's room.
            toReceive[byteIndex] = received;
        }

        ++byteIndex;

        byteAcknowledged = sDualShockAcknowledgeReceived;

        // Wait for the Dual Shock to acknowledge the byte.
        // All bytes except the last one(?!) are acknowledged.
        if(byteIndex < reportedTransactionLength) {
            uint8_t microsWaited = 0;
            while(!byteAcknowledged) {
                if(microsWaited > 100) {
                    // Sometimes, especially when it's in command mode, the Dual
                    // Shock seems to get into a bad state and 'give up'.
                    // Detect this and bail.
                    // We'll return failure from this function, and it's up to
                    // the caller to try again if they want to.
                    errored = true;
                    break;
                }
                _delay_us(2);
                microsWaited += 2;
                byteAcknowledged = sDualShockAcknowledgeReceived;
            }
        }
    } while(!errored && byteIndex < reportedTransactionLength);

    // Despite the fact that the controller doens't raise the acknowledge line
    // for the last byte, we still seem to need to wait a bit for communication
    // to be reliable :-|.
    _delay_us(20);

    // ~CS line ('Attention') needs to be raised to its inactive state between
    // each transaction.
    PORTB |= 1 << 2;

    return errored ? 0 : byteIndex;
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

    // second-to-last and last byte are small motor (on/off?),
    // large motor (~0x40-0xff?)
    static const PROGMEM DualShockCommand pollCommand[] = { { 2, { 0x42, 0x00 } } };
    static const PROGMEM DualShockCommand enterConfigCommand[] = { { 3, { 0x43, 0x00, 0x01 } } };
    static const PROGMEM DualShockCommand exitConfigCommand[] = { { 3, { 0x43, 0x00, 0x00 } } };
    static const PROGMEM DualShockCommand switchToAnalogCommand[] = { { 3, { 0x44, 0x00, 0x01 } } };
    static const PROGMEM DualShockCommand setUpMotorsCommand[] = { { 8, { 0x4D, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF } } };

    static const PROGMEM DualShockCommand *const enterAnalogCommandSequence[] = {
        enterConfigCommand,
        switchToAnalogCommand,
        setUpMotorsCommand,
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
    const DualShockCommand *commandToExecute_P;

    if(!executingCommandQueue) {
        // If there's no command queue (the usual case), we just poll
        // controller state.
        commandToExecute_P = pollCommand;
    } else {
        commandToExecute_P = (DualShockCommand *)pgm_read_ptr(commandQueue + commandQueueCursor);
    }

    uint8_t commandLength = pgm_read_byte((uint8_t *)commandToExecute_P + offsetof(DualShockCommand, length));
    uint8_t command[commandLength + 4];
    memcpy_P(&command, commandToExecute_P + offsetof(DualShockCommand, commandSequence), commandLength);

    // Ick to this special-casing...
    if(commandToExecute_P == pollCommand && sRumbleEnabled) {
        const uint8_t lowRumbleAmplitude = sLowRumbleAmplitude;// ? sLowRumbleAmplitude : sHighRumbleAmplitude;
        const uint8_t highRumbleAmplitude = sHighRumbleAmplitude;// ? sHighRumbleAmplitude : sLowRumbleAmplitude;

        bool highRumbleOn = false;
        if(highRumbleAmplitude) {
            static uint8_t activationCount = 0;
            const uint8_t duty_cycle = 0x0f / (highRumbleAmplitude >> 4);
            activationCount = (activationCount + 1) % duty_cycle;
            highRumbleOn = (activationCount == 0);
        }

        commandLength += 2;
        command[2] = highRumbleOn ? 0xff : 0; // Small motor. On = 0xff, Off = anything else. High
        command[3] = lowRumbleAmplitude; // Big motor. Practical range is 0x40 - 0xff. Low
    }

    replyLength = dualShockCommand(command,
                                   commandLength,
                                   (uint8_t *)&dualShockReports[thisDualShockReportIndex],
                                   sizeof(DualShockReport));

    if(!executingCommandQueue) {
        if(replyLength >= 2) {
            const uint8_t mode = dualShockReports[thisDualShockReportIndex].deviceMode;

            if(mode != 0x7) {
                // If we're _not_ in analog mode, initiate the sequence of
                // commands that will cause the controller to switch to analog
                // mode. One command is performed every time this function
                // is called.
                commandQueue = enterAnalogCommandSequence;
                commandQueueCursor = 0;
                commandQueueLength = 4;

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
        haltStr6(0, STR6("Report Clash"));
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
        haltStr6(0, STR6("Report Clash"));
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
            haltStr6(0, STR6("Report Too Big"));
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
        debugPrintHex(address);
        debugPrintHex(length);
        debugPrintHex(replyDataLength);
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

uint8_t highestByteFromNBytes(int count, ...)
{
    va_list args;
    va_start(args, count);

    uint8_t highestByte = 0;

    for (int i = 0; i < count; i++)
    {
        uint8_t value = uint8_t(va_arg(args, int));
        if (value > highestByte)
        {
            highestByte = value;
        }
    }

    va_end(args);

    return highestByte;
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
        debugPrintStr6(STR6("\n/ ")) ;
    } else {
        debugPrintStr6(STR6("\n| ")) ;
    }
    for(uint8_t i = 0; i < len; ++i) {
        debugPrintHex(data[i]);
    }

    // USB signifies end-of-transfer as either a 'short' packet (not 8 bytes), or
    //  reaching the maximum size.
    uint8_t reportLength = accumulatedReportBytes + len;
    bool reportComplete = len != 8 ||  reportLength == sReportSize;

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
        accumulatedReportBytes = reportLength;
        if(reportComplete) {
            reportIn = reportAccumulationBuffer;
        } else {
            // We need more data to complete the report.
            return;
        }
    }


    // Deal with the report!
    const uint8_t commandOrSequenceNumber = reportIn[1];
    uint8_t uartCommand = 0;
    debugPrintStr6(STR6("\n\\ ")) ;
    debugPrintHex(reportId);
    debugPrint(':');
    debugPrintHex(commandOrSequenceNumber);

    switch(reportId) {
    case 0x80: {
        // A 'Regular' command.

        const uint8_t command = commandOrSequenceNumber;
        switch(command) {
        case 0x05: {
            // Stop HID Reports
            sInputReportsSuspended = true;
        } break;
        case 0x04: {
            // Start HID Reports
            sInputReportsSuspended = false;
        } break;
        case 0x01: {
            // Request controller info inc. MAC address
            static const PROGMEM uint8_t reply[] = { 0x00, 0x03, 0x43, 0x23, 0x53, 0x22, 0xa3, 0xc7 };
            prepareRegularReplyReport_P(0x81, command, reply, sizeof(reply));
        } break;
        case 0x02: {
            // "Sends handshaking packets over UART to the Joy-Con or Pro Controller"
            prepareRegularReplyReport_P(0x81, command, NULL, 0);
        } break;
        case 0x03: {
            // "Switches baudrate to 3Mbit, needed for improved Joy-Con latency.
            //  This command can only be called following 80 02, but allows another 80 02 packet to be sent
            //  following it. A second handshake is required for the baud switch to work."
            prepareRegularReplyReport_P(0x81, command, NULL, 0);
        } break;
        default: {
            haltStr6(uartCommand, STR6("Unexpected 'regular' subcommand")) ;
            prepareRegularReplyReport_P(0x81, command, NULL, 0);
        } break;
        }
    } break;
    case 0x01: {
        // A 'UART' request.

        uartCommand = reportIn[10];
        debugPrint('|');
        debugPrintHex(uartCommand);

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
            debugPrintHex(address);
            debugPrint('-');
            debugPrintHex(length);

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
                static const PROGMEM uint8_t reply[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x32, 0x32, 0x32, 0xff, 0xff, 0xff };
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
                haltStr6(address & 0xff, STR6("Unexpected SPI read subcommand")) ;
                break;
            }
            prepareUartSpiReplyReport_P(address, length, spiReply, spiReplyLength);
        } break;
        case 0x04: // Trigger buttons elapsed time (?)
            prepareUartReplyReport_P(0x83, uartCommand, NULL, 0);
            break;
        case 0x48: // Set vibration enabled state
            sRumbleEnabled = reportIn[11];
            debugPrintStr6(STR6(" Rumble")) ;
            debugPrintStr6(sRumbleEnabled ? STR6(" enabled") : STR6(" disabled"));
            prepareUartReplyReport_P(0x80, uartCommand, NULL, 0);
            break;
        case 0x21: {// Set NFC/IR MCU config
            static const PROGMEM uint8_t reply[] = { 0x01, 0x00, 0xFF, 0x00, 0x08, 0x00, 0x1B, 0x01 };
            prepareUartReplyReport_P(0xa0, uartCommand, reply, sizeof(reply));
        } break;
        case 0x00: // Do nothing (return report)
        case 0x03: // Set input report mode
        case 0x06: // Set power state
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
            haltStr6(uartCommand, STR6("Unexpected UART subcommand")) ;
            break;
        }
    }
    // No break here! We fall through to the 0x10 decoder because 0x01 reports
    // also contain rumble data in the same place.
    case 0x10: {
        // Rumble data
        // Example: O: 100E 0045 4052 0040 4052
        if(sRumbleEnabled) {
            if(reportLength < 10) {
                haltStr6(reportLength, STR6("Unexpected rumble data length"));
            }

            SwitchRumbleState leftRumbleState;
            SwitchRumbleState rightRumbleState;

            decodeSwitchRumbleState(reportIn + 2, &leftRumbleState);
            decodeSwitchRumbleState(reportIn + 6, &rightRumbleState);

            sLowRumbleAmplitude = highestByteFromNBytes(10, leftRumbleState.lowChannelAmplitude, rightRumbleState.lowChannelAmplitude,
                                                        leftRumbleState.pulse1Amplitude, leftRumbleState.pulse2Amplitude, leftRumbleState.pulse3Amplitude, leftRumbleState.pulse1Amplitude,
                                                        rightRumbleState.pulse1Amplitude, rightRumbleState.pulse2Amplitude, rightRumbleState.pulse3Amplitude, leftRumbleState.pulse1Amplitude);
            sHighRumbleAmplitude = highestByteFromNBytes(10, leftRumbleState.highChannelAmplitude, rightRumbleState.highChannelAmplitude,
                                                         leftRumbleState.pulse1Amplitude, leftRumbleState.pulse2Amplitude, leftRumbleState.pulse3Amplitude, leftRumbleState.pulse1Amplitude,
                                                         rightRumbleState.pulse1Amplitude, rightRumbleState.pulse2Amplitude, rightRumbleState.pulse3Amplitude, leftRumbleState.pulse1Amplitude);

            debugPrintStr6(STR6(" Rumble: ("));
            debugPrintDec(sLowRumbleAmplitude);
            debugPrint(',');
            debugPrintDec(sHighRumbleAmplitude);
            debugPrint(')');
        }
    } break;
    case 0x00:
        // Not quite sure why we sometimes get reports with a 0 id.
        // The switch seems to stop talking to us if we don't reply with an
        // input report though, even if they're suspended (see
        // sInputReportsSuspended)
        prepareInputReport();
        break;
    default:
        // We should never reach here because we should've halted above.
        haltStr6(reportId, STR6("Unexpected report ID")) ;
        break;
    }

    // We store a small history of received commands to aid with debugging.
    if(reportId == 0x10) {
        // Because we get so many of these, coalesce runs of them in the history
        // buffer. We use the second and third bytes to store how many we've
        // seen in a row.
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
    debugPrintStr6(STR6(" [OSC: ")) ;
    debugPrintDec(OSCCAL);
    debugPrint(']');

    // We're ready for another report.
    accumulatedReportBytes = 0;
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
            debugPrintStr6(STR6("\n!CLR HALT ")) ;
            debugPrintHex(request->wIndex.bytes[0]);
            debugPrint('\n') ;
            usbFunctionWriteOutOrAbandon(NULL, 0, true);
        }
    }

    if(usbCrc16(data, len + 2) != 0x4FFE) {
        haltStr6(0, STR6("CRC error!")) ;
    }
}

usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    serialPrintStr6(STR6("\n\nUnexpected setup transaction - data is: "));
    for(uint8_t i = 0; i < 8; ++i) {
        serialPrintHex(data[i]);
    }

    // I've never seen this called when connected to a Switch or a Mac.
    // It might be necessary to implement the USB spec, but I really don't
    // like having untested code that's never actually used.
    // Let's just return 0 (signified 'not handled'), and worry about this if it
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
        if((PORTB & (1 << 0)) == 0) {
            PORTB |= (1 << 0);
        } else {
            PORTB &= ~(1 << 0);
        }

#if DEBUG_PRINT_ON
        debugPrintStr6(STR6(" [FPS: ")) ;
        debugPrintDec(transmittedReportsCount);
        debugPrint(']');
        transmittedReportsCount = 0;
#endif
    }
}

static void transmitPacket()
{
    bool stopTransmission = false;

    // It takes multiple interrupts to send one report, so we keep track
    // of the report we're sending, and our position within it, in these
    // static variables.
    static uint8_t transmittingReportIndex = 0;
    static uint8_t *transmittingReport = NULL;
    static uint8_t transmittingReportLength = 0;
    static uint8_t transmittingReportInputReportPosition = 0;
    static uint8_t transmittingReportTransmissionCursor = 0;

    if(!stopTransmission) {
        // It's time to provide a packet to V-USB.
        if(transmittingReport == NULL) {
            if(!sReportPending && !sInputReportsSuspended) {
                // If there's no report already pending (i.e. no prepared
                // reply to a command sent by the Switch), prepare
                // a plain input report.
                prepareInputReport();
            }

            if(sReportPending) {
                transmittingReportIndex = sCurrentReport;
                transmittingReport = sReports[transmittingReportIndex];
                transmittingReportLength = sReportLengths[transmittingReportIndex];
                transmittingReportInputReportPosition = sReportsInputReportPosition[transmittingReportIndex];
                transmittingReportTransmissionCursor = 0;

                // If the Switch sends a command while we're sending this
                // report, the reply to it can be prepared while we're sending
                // this report.
                // This doesn't seem to happen very much.
                sCurrentReport = (uint8_t)(transmittingReportIndex + 1) % 2;
                sReportPending = false;
            }
        }

        if(transmittingReport != NULL) {
            const uint8_t remainingLength = transmittingReportLength - transmittingReportTransmissionCursor;
            const uint8_t packetSize = remainingLength <= 8 ? remainingLength : 8;

            const uint8_t nextReportTransmissionCursor = transmittingReportTransmissionCursor + packetSize;

            if(transmittingReportInputReportPosition != 0 && transmittingReportInputReportPosition >= transmittingReportTransmissionCursor && transmittingReportInputReportPosition < nextReportTransmissionCursor) {
                // We prepare the actual input part of the report - i.e. the
                // state of the Dual Shock's controls - at the last minute
                // in an attempt to get the lowest possible latency.
                prepareInputSubReportInBuffer(transmittingReport + transmittingReportInputReportPosition);
            }

            // Actually provide the packet to V-USB to be sent when the next
            // interrupt arrives.
            usbSetInterrupt(&transmittingReport[transmittingReportTransmissionCursor], packetSize);

            transmittingReportTransmissionCursor = nextReportTransmissionCursor;

            if(packetSize < 8 || transmittingReportTransmissionCursor == sReportSize) {
                // We've reached the end of this report.
                stopTransmission = true;
            }
        }
    }

    if(stopTransmission) {
        sReportLengths[transmittingReportIndex] = 0;
        sReportsInputReportPosition[transmittingReportIndex] = 0;
        transmittingReport = NULL;

#if DEBUG_PRINT_ON
        ++transmittedReportsCount;
#endif
    }
}

#if 0
static void usbResume()
{
    // This doesn't seem to work, but I leave it here for potential future use.
    // Reportedly the Switch does not respond to USB resume requests.
    cli();
    uint8_t outWas = USBOUT;
    uint8_t ddrWas = USBDDR;

    // Signal a 'K State' (DATA+ High, DATA- low)
    USBOUT = (USBOUT & ~USBMASK) | (1 << USBPLUS);
    USBDDR |= USBMASK;
    debugPrintHex(USBOUT);
    debugPrintHex(USBDDR);

    // Wait. Spec says min 1 ms, max 15 ms.
    _delay_ms(7);

    // Set the line back to its previous idle state.
    USBDDR = ddrWas;
    USBOUT = outWas;
    GIFR &= ~(1 << INTF0);
    sei();
}
#endif

void loop()
{
    ledHeartbeat();
    serialPoll();
    usbPoll();

    const uint8_t sofCountNow = usbSofCount;
    const uint8_t millisNow = timerMillis();

    static bool sUsbSuspended = true;
    static uint8_t lastSofCount = 0xff;
    static uint8_t lastSofTime = 0xff;
    if(sofCountNow != lastSofCount) {
        lastSofCount = sofCountNow;
        lastSofTime = millisNow;
        if(sUsbSuspended) {
            sUsbSuspended = false;
            debugPrintStr6(STR6("\nUSB Active\n")) ;
        }
    }

    if(!sUsbSuspended && (uint8_t)(millisNow - lastSofTime) > 4) {
        // No USB activity for over 3ms - we've detected a USB sleep.
        // (USB 2.0 spec: "7.1.7.6 Suspending")
        sUsbSuspended = true;
        debugPrintStr6(STR6("\nUSB Suspended\n")) ;
    }

    if(!sUsbSuspended ) {
        if(usbInterruptIsReady()) {
            // Although V-USB is ready for us to give it the packet to transmit
            // on the next interrupt, we need less than 1ms to prepare, so we
            // can wait until the next 1ms SOF is received before preparing the
            // packet for a _little_ bit lower latency.

            static uint8_t preparePacketAtSofCount = 0;
            if(!((uint8_t)(preparePacketAtSofCount - sofCountNow) <= 1)) {
                // We haven't already scheduled packet preparation, so schedule
                // it for the next SOF.
                preparePacketAtSofCount = sofCountNow + 1;
            }
            if(sofCountNow == preparePacketAtSofCount) {
                transmitPacket();
            }
        }
    } else {
        // Switch off the debug LED to save power.
        PORTB |= (1 << 0);

        sleep_cpu();

        // USB traffic firing INT0 will wake us up.
        debugPrintStr6(STR6("Awake\n")) ;
    }
}

int main() {
    setup();
    do {
        loop();
    } while(true);
}
