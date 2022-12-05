In this part, we'll get [V-USB](https://www.obdev.at/products/vusb/index.html) set up.

As mentioned previously, the Atmega8A doesn't have any USB hardware - but there's a well respected software library called [V-USB](https://www.obdev.at/products/vusb/index.html) that implements the USB protocol in software, at the expense of a taking up some CPU time, and taking away a little timing flexibilty in timing (it needs to be able to run to service the USB bus at specific times). For most uses, neither of these are issues.

It's licensed under GPL 2 - meaning that if you distribute anything based on it you also have to offer the source code - but if you're making a commercial product and _don't_ want to distribute your source you can get a commercial licence from Objective Development. I suspect there are a lot of people violating the lincense out there...

## Getting V-USB

Let's get V-USB. There's been a bit of development and bug fixing since the last official release (in 2012!), so I'm going to use the latest code from [the V-USB GitHub repo](https://github.com/obdev/v-usb).

To us software engineers, the way embedded folks distribute libraries is a little odd. The traditional way is to download a [release from the web site](https://www.obdev.at/products/vusb/download.html), unzip it, and just copy the `usbdrv` folder into your project! Worse, if you want to use the automatic clock calibration (as we are for that 12.8MHz clock we haven't yet got working), yuou need to move some files around.

In a PlatformIO project, you can just put libraries into the 'lib' folder that PlatformIO already created for you. Unfortunatly we can't just add V-USB as-is as a subdirectory there, because most of the files we need are in the `usbdrv` directory, and there's a bunch of stuff we _don't_ want in the V-USB repo too.

So, what I am going to do is put the V-USB directory at the top-level of my project, and symlink the files we really need into place in our lib directory. That way, we can get things compiling cleanly without changing the V-USB folders in any way.

I'm already using Git to manage my project, so I'm going to use `git subtree` to make the directory:

```
jamie@Jamies-Air ~/D/SwitchControllerAdapter (main)> git remote add -f v-usb https://github.com/obdev/v-usb
Updating v-usb
From https://github.com/obdev/v-usb
 * [new branch]          master     -> v-usb/master

jamie@Jamies-Air ~/D/SwitchControllerAdapter (main)> git subtree add -P v-usb v-usb/master --squash
Added dir 'v-usb'

jamie@Jamies-Air ~/D/SwitchControllerAdapter (main)> ls v-usb
Makefile         circuits/        libs-host/       usbdrv/
README.md        examples/        mkdist.sh*       v-usb.xcodeproj/
Readme.txt       libs-device/     tests/
```

Now, we'll symlink the actual bits of V-USB we need into place:

```
jamie@Jamies-Air ~/D/SwitchControllerAdapter (main)> mkdir lib/v-usb
jamie@Jamies-Air ~/D/SwitchControllerAdapter (main)> cd lib/v-usb
jamie@Jamies-Air ~/D/S/l/v-usb (main)> ln -s ../../v-usb/usbdrv .
jamie@Jamies-Air ~/D/S/l/v-usb (main)> ln -s ../../v-usb/libs-device/osctune.h .
jamie@Jamies-Air ~/D/S/l/v-usb (main)> cd ..
jamie@Jamies-Air ~/D/S/lib (main)> cd ..
jamie@Jamies-Air ~/D/SwitchControllerAdapter (main)> 
```

What this does is make a [symbolic link](https://en.wikipedia.org/wiki/Symbolic_link) from the files in the V-USB folder to PlatformIO's `lib` folder, so it sees only the files necessary to compile V-USB[^entirelibhierarchy].

## Getting V-USB to compile

Okay! Let's see how we're doing - is V-USB working now?

First, we'll need to hint to PlatformIO that we're actually _using_ V-USB. Add this to your `main.cpp` file, just under the existing Arduino `#include`:

```
#include <usbdrv/usbdrv.h>
```

Now, let's go! Hit 'Build'!

```
...
lib/v-usb/usbdrv/usbdrvasm.asm: Assembler messages:
lib/v-usb/usbdrv/usbdrvasm.asm:20: Error: unknown opcode `end'
*** [.pio/build/ATmega8/libecd/v-usb/usbdrv/usbdrvasm.asm.o] Error 1
...
In file included from lib/v-usb/usbdrv/usbdrv.c:10:0:
lib/v-usb/usbdrv/usbdrv.h:127:10: fatal error: usbconfig.h: No such file or directory
...
```

Hmm. Two errors (or maybe more - but if there are more it'll be the same two problems multiple times). Let's take them one at a time.

For the first one, if you take a look at `lib/v-usb/usbdrv/usbdrvasm.asm`, you will see it says that it's for the "IAR compiler/assembler system". That's not what we're using. And all it does is include `usbdrvasm.S`, which PlatformIO is already compiling for us. We can actually just not compile it. We'll come back to this after we deal with the second error.

The second error should not really be surprising. We still need to configure V-USB, and that's done by making a file called `usbconfig.h`. So the reason compilation is failing is that we haven't done that yet. Making this file is not as arduous as it sounds - it's generally done by copying the provided `usbconfig-prototype.h` header file to `usbconfig.h` and making small changes to it. 

I don't really want to make my own `usbconfig.h` in the otherwise pristine copy of V-USB though. I'd like to keep that as a 'clean' copy of V-USB. So I will put my configuration header in the top-level `include` folder. 

So, create a copy of `usbconfig-prototype.h` in the `include` folder PlatformIO at the top-level of our project, called `usbconfig.h`.

We have two things we need to do now. We need to somehow let V-USB know we've put its config file in another folder, and we also need to make PlatformIO skip the needless, erroring compilation of `usbdrvasm.asm`.

Unfortunately, it's not quite possible ot do this _just_ by editing the `platformio.ini` file as you might expect. But that's okay - the makers of PlatformIO have made it possible to configure it further with Python code! Don't be too nervous, we're just going to need a little.

Create a `v-usb_platformio_helper.py` file at the top level of the folder, beside `platformio.ini`, and put this in it:

``` python
Import("env")

# V-USB has a '.asm' file in the usbdrv folder that PlatformIO will try to 
# compile - but it's really just for the "IAR compiler/assembler system" and 
# will just cause errors. We'll make it be skipped.
def skip_file(node):
    return None
env.AddBuildMiddleware(skip_file, "*/lib/v-usb/*.asm")

# Make sure V-USB can find its config file, which we've placed inside the 
# top-level include folder, and header files in the v-usb library directory.
def add_usbdrv_include(node):
    return env.Object(
        node,
        CFLAGS=env["CFLAGS"] + ["-Iinclude", "-Ilib/v-usb"],
        ASFLAGS=env["ASFLAGS"] + ["-Iinclude", "-Ilib/v-usb"],
    )
env.AddBuildMiddleware(add_usbdrv_include, "*/lib/v-usb/*.[chS]")
```

What this is doing is adding two hooks to the PlatformIO build process. The first will cause it to skip any 'asm' files in the `usbdrv` folder, the second will cause it to add `"-Iinclude"` to the compiler flags for C or assembly files in the `usbdrv` folder.

You might notice that I've also sneaked in an exter `-I` argument there: `"-Ilib/v-usb"`. That's going to be necessary later when we're trying to use the `osctune.h` header later.

The only thing remaining to do is to tell PlatformIO to actually use our Python file.

Go back to `platformio.ini` and add `extra_scripts = pre:v-usb_platformio_helper.py` below the `framework = arduino` line[^fullplatformioinifile].

Okay, with this done, let's hit 'Build' again!

```
...<Lots of output>...
Linking .pio/build/ATmega8/firmware.elf
Checking size .pio/build/ATmega8/firmware.elf
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [          ]   0.9% (used 9 bytes from 1024 bytes)
Flash: [=         ]   9.8% (used 754 bytes from 7680 bytes)
Building .pio/build/ATmega8/firmware.hex
===================================================================== [SUCCESS] Took 0.72 seconds =====================================================================
```

Woohoo! It all compiled - and, look, we're still using only 754 bytes of program space! Amazing!

Wait, though - isn't that the same amount as we were using before we added V-USB? Yes. Although PlatfomrIO's build system _does_ compile it all, the it cleverly doesn't include any code we're not actually _using_ in the final binary. We can expect this to grow as we actually _use_ V-USB.


# Using V-USB

So, great, now we're ready to use V-USB! 

Or are we? 

Unfortunately, no. We still need to _actually_ configure it. All we've done so far is copy the 'prototype' configuration. 

Things from now on out aren't nearly so involved though - we've basically reached the end of our configuration of PlatformIO, and are ready to start actually coding things that do stuff on the chip.


# Configuring V-USB

Open the `include/usbconfig.h` file we made a littel while ago. It's actually really well documented in comments. If you want to do a deep dive yourself, feel free to go and have a read.

Now we'll get to the part where we set up to run the oscilator at 12.8MHz. USB requires very precise timing. The V-USB wiki (slightly out of date :-/) describes why:

> V-USB requires a precise clock because it synchronizes to the host's data stream at the beginning of each packet and then samples the bits in constant intervals. The longest data packet for low speed USB is 11 bytes. Since we don't need the CRC, we read 9 bytes at maximum. Including stuffed bits, that's a maximum of 84 bits. Bit sampling must not drift more than 1/4 bit during these 84 bits, resulting in a requirement of 0.3% clock precision.

The V-USB code is _very carefully_ written. It wakes up on an interrupt when data starts arriving on the data lines of the USB connection, and receives data in very cleverly written assembly code that hits the USB timing precisely. There are various versions of the assembly routines for various clock timings (12 MHz, 12.8 MHz, 15 MHz, 16 MHz, 16.5 MHz, 18 MHz or 20 MHz) and they're all different - you can see them in the `usbdrvasmXX[X].inc` files in V-USB's source code. They only work when the chip is clocked by a source that ensures the frequency is pretty much exactly correct.

The ATmega8A's internal '8Mhz' internal oscilator actually _can_ be tuned by writing values to 'OSCCAL' byte (a special memory location). 'OSCCAL' is _intended_ to be used to calibrate the oscillator to run at 8MHz reliably. The characteristics of each individual ATmega8A differ, and it's not guaranteed that it can reach 12.8MHz - but that's in theory - almost every real ATMega8 out there can reach 12.8MHz (possibly even 16MHz).

Here's a chart of the typical OSCCAL to MHz ratio form [Micorchip's (ne Atmel's) AVR053 application note](https://ww1.microchip.com/downloads/en/Appnotes/Atmel-2555-Internal-RC-Oscillator-Calibration-for-tinyAVR-and-megaAVR-Devices_ApplicationNote_AVR053.pdf).

[image of chart]

There's no calibration value that's guaranteed to produce 12.8MHz thoguh - it will be diferent for every individual chip - and even vary over time with temperature or voltage.

It sure sounds like the internal oscilator is not suitable as a clock source that has a "a requirement of 0.3% clock precision". How do we know what value to set `OSCCAL` to? And if it varies with temperature that just sounds terrible! We'd need to constantly recalibrate it. And to do that, we'd need a 'good' timer to compare it against.

So, no, it's not a suitable clock source. But it can be made to be one! Back in 2008, Henrik Haftmann had the idea that you could calibrate the clock using pulses that the USB host (i.e. the computer) sends to the USB device! The USB device could measure the time between each USB 'Start Of Frame' message - which is required to be exactly 1ms. If the device thinks there's longer than 1ms between each SOF, it can speed up its own timing clock down a bit. If it thinks there's less than 1ms between each frame, it can slow it down a bit.

This clever idea is implemented in the 'osctune.h' file in V-USB. V-USB is not configured to use it, though, so we'll need to configure it to do so.

Backing up a bit, there are two connections that need to be made from the USB bus to the ATMega - these two connections are called 'D+' and 'D-' (D for "Data") - if you want to know more about what's actually transmitted on these lines, I'd recommend [this excellend video by Ben Eater](https://youtu.be/wdgULBpRoXk).

We can't just connect them to any pin we want. One of them is required, by how V-USB works, to be connected to the pin that can trigger interrupt 0. On the ATMega8A, that's pin 4, or bit two of Port B[^Ports]. V-USB also requires that the other data line be connected to a pin that corresponds to a bit on the same port.

There's another restriction though - the 'osctune.h' routines require that the 'D-' line be connected to the interrupt pin. So, we need to connect 'D-' to pin 4. Let's connect D+ to the pin right next to it, pin 5, which is bit 3 on port 2. You can see this in the pinout diagram in the datasheet:

[Iamge of pinout diagram]

Note that pin 4 is labeled '(INT0) PD2' and pin 5 is labeled '(INT1) PD3'. We might come to regret using pin 5 later, if we find we want to use Interrupt 1 for something - but we if that happens we can easily re-configure and use one of the other Port D pins. Let's just use these for now because they're next to each other.

We'll need to tell V-USB this is what we're planning, so crack open the `usbconfig.h` file you made in the `include` folder! 

The things we need to change are pretty near the top. `USB_CFG_IOPORTNAME` is probably already set to `D`. Change `USB_CFG_DMINUS_BIT` to `2`, and `USB_CFG_DPLUS_BIT` to `3`.

We then need to set things up to use the dynamic clock tuning implemented in `osctune.h`, so head down to the _very bottom_ of the file and, just _before_ the `#endif`, add a line saying `#include "osctune.h"`.

TODO:  DO WE ALSO NEED #define USB_INTR_CFG_SET        ((0 << ISC00) | (1 << ISC01)) ?

In the `osctune.h` file itselfm there are instructions saying that:

> You must declare the global character variable "lastTimer0Value" in your main code.

so let's also do that. Head back to `main.cpp` and add a new global variable:

char lastTimer0Value = 0;

Hit 'Build'!

With any luck, things should work. But we're still not _doing_ anything. Now that V-USB is configured, we'll have to add some code to initalise it.

## Actually writing some code!

Okay, let's change `main.cpp` to initialize V-USB! There are two things we need to do for a simple 'does nothing' device:

- initialize the library;
- implement a `usbMsgLen_t usbFunctionSetup(uchar data[8])` function that V-USB will call with incoming dataa (we'll leave it emppty for now);
- regularly call 'usbPoll()' (according to the documentation, no longer than 50ms must pass between calls).

Let's add this all to `main.cpp`. We'll also leave in the 'blinking' - but we will need to change how it works so that we're not delaying for a second - we need to call 'usbPoll()' every 50ms, remember. Instead, we'll keep track of when the last toggle of the LED was, and toggle it every time we find we've been running for more than a second since the last toggle.

Here's my `main.cpp` now:

```C++
#include <Arduino.h>

#include <usbdrv/usbdrv.h>
uint8_t lastTimer0Value = 0;

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

void loop()
{
    static unsigned long lastBlink = 0;

    unsigned long timeNow = millis();
    if(timeNow - lastBlink >= 1000) {
        lastBlink = 0;
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }

    usbPoll();
}
```

Why's that 250ms delay necessary during initialization? To be honest, I'm not entirely sure. the V-USB Wiki says that "In theory, you don't need this, but it prevents inconsistencies between host and device after hardware resets". So, uh, let's just leave it? 250ms isn't _that_ long to wait after a plug-in.

Lets-a go! Hit build!

```
/var/folders/6p/89bzfdg51_73bct78x16tgy80000gn/T//cccI9Rdo.ltrans0.ltrans.o: In function `main':
<artificial>:(.text.startup+0x6c): undefined reference to `usbInit()'
<artificial>:(.text.startup+0x17e): undefined reference to `usbPoll()'
collect2: error: ld returned 1 exit status
*** [.pio/build/ATmega8/firmware.elf] Error 1
```

Uh-oh. It looks like the functions we're calling are missing! This stumped me for a bit. What's actually going on is that C-USB is in *C*, but we're writing in *C++* (as is standard for Arduino-framework code). We just need to tell the compiler this in our main.cpp file. We need to enclose the V-USB include - and the lastTimer0Value decvlaration - in an `extern "C"` block:

```C++
extern "C" {
    #include <usbdrv/usbdrv.h>
    uint8_t lastTimer0Value = 0;
}
```

Okay! let's build again!

```
Linking .pio/build/ATmega8/firmware.elf
Checking size .pio/build/ATmega8/firmware.elf
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [=         ]   5.8% (used 59 bytes from 1024 bytes)
Flash: [===       ]  31.9% (used 2450 bytes from 7680 bytes)
=================================================== [SUCCESS] Took 0.41 seconds ===================================================
```

Look! It built! And, at last, the amount of flash our code is taking up has gone up - to 2,450 bytes.

That's our does-nothing software basically done!

I'm going to do one last thing, which is to refctor a little to put the LED flashing into its own 'heartbeat' function and leave out main loop nice and clean. That, plus a few more comments, leaves us with this:

```C++
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
```

Building this still leaves us at 2,450 bytes - I guess the compiler is smart enough to know that my refactoring doean't really change what the progrem does at all.

Now, we need to set up the hardware to run it! Lets head back to our breadboard.


## Breadboarding the USB connection



[^entirelibhierarchy]The entire structure should now look like this:

```
jamie@Jamies-Air ~/D/SwitchControllerAdapter (main)> find -L ./lib
./lib
./lib/README
./lib/v-usb
./lib/v-usb/osctune.h
./lib/v-usb/usbdrv
./lib/v-usb/usbdrv/usbdrvasm18.inc
./lib/v-usb/usbdrv/oddebug.h
./lib/v-usb/usbdrv/usbconfig-prototype.h
./lib/v-usb/usbdrv/usbdrvasm.asm
./lib/v-usb/usbdrv/usbdrv.c
./lib/v-usb/usbdrv/usbportability.h
./lib/v-usb/usbdrv/usbdrvasm20.inc
./lib/v-usb/usbdrv/usbdrvasm128.inc
./lib/v-usb/usbdrv/asmcommon.inc
./lib/v-usb/usbdrv/usbdrvasm12.inc
./lib/v-usb/usbdrv/usbdrvasm16.inc
./lib/v-usb/usbdrv/usbdrvasm15.inc
./lib/v-usb/usbdrv/usbdrvasm18-crc.inc
./lib/v-usb/usbdrv/Changelog.txt
./lib/v-usb/usbdrv/Readme.txt
./lib/v-usb/usbdrv/CommercialLicense.txt
./lib/v-usb/usbdrv/USB-ID-FAQ.txt
./lib/v-usb/usbdrv/usbdrvasm.S
./lib/v-usb/usbdrv/oddebug.c
./lib/v-usb/usbdrv/License.txt
./lib/v-usb/usbdrv/usbdrvasm165.inc
./lib/v-usb/usbdrv/USB-IDs-for-free.txt
./lib/v-usb/usbdrv/usbdrv.h
```


[^fullplatformioinifile] For reference, it should now look something like this all together:

```ini
; PlatformIO Project Configuration File
;
;   Build options: build flags, source filter
;   Upload options: custom upload port, speed and extra flags
;   Library options: dependencies, extra library storages
;   Advanced options: extra scripting
;
; Please visit documentation for the other options and examples
; https://docs.platformio.org/page/projectconf.html

[env:ATmega8]
platform = atmelavr
board = ATmega8
framework = arduino

; Some python help to build V-USB without modifying its source.
extra_scripts = pre:v-usb_platformio_helper.py


; Chip configuration:

; BODLEVEL=1, BODEN=0, SUT=10, CKSEL=0100
board_fuses.lfuse = 0xA4

; RSTDISBL=1, WTDON=1, SPIEN=0, CKOPT=1, EESAVE=0, BOOTSZ=00, BOOTRST=1
board_fuses.hfuse = 0xD1

; This configures the _software_ to assume a 12.8 MKz clock - it does not affect
; the chip itself.
; We configure the chip with fuses, above, to use the built-in 8MHz internal 
; oscilator. We'll then calibrate it in software to run 'too fast' at 12.8MHz.
board_build.f_cpu = 12800000


; PlatformIO programmer settings:

; Settings to use an Arduino-as-ISP-powered programmer.
; On Windows, `upload_port` might be e.g. 'COM1' rather than a path.
; On Mac and Linux, the path might differ depending on the Arduino and/or
; serial connection.
upload_port = /dev/cu.usbserial-110
upload_protocol = stk500v1
upload_speed = 19200

; Sadly-needed workarounds:
; -u: 
;   Work around the same issue relating to a  "fake" efuse described as
;   affecting the Windows Arduino IDE here:
;   https://github.com/arduino/arduino-cli/issues/844
; -P and -b:
;   PlatfomIO's '--target fuses' doesn't work without it - the 'upload_XXX' 
;   settings are not used even though they need to be.
upload_flags =
    -u
    -P${UPLOAD_PORT}
    -b${UPLOAD_SPEED}
```