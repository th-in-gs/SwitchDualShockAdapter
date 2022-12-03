
In this series of posts, I'm going to attempt to make a [Dual Shock](https://en.wikipedia.org/wiki/DualShock) to [Nintendo Switch](https://www.nintendo.com/switch/) controller adapter. It will plug into the Switch Dock's USB port.

## Game Controllers

One of my low-key annoyances with the modern world is that game controllers, which have _essentially_ been the same since the Sony introduced the Playstation [Dual Analog Controller](https://en.wikipedia.org/wiki/Dual_Analog_Controller) - or arguably the [Dual Shock](https://en.wikipedia.org/wiki/DualShock) (which was the same, but with vibration) in 1997, are all incompatible with each other. There is a [whole](https://www.google.com/search?q=Logitech+F310&tbm=isch) [world](https://www.google.com/search?q=Macally+iShock+gamepad&tbm=isch) of [unique](https://www.google.com/search?q=Gravis+Eliminator+Aftershock&tbm=isch), [crazy](https://www.google.com/search?q=Microsoft+Sidewinder+Dual+Strike&tbm=isch) [game](https://www.google.com/search?q=Logitech+Wingman+Rumblepad&tbm=isch) [controllers](https://www.google.com/search?q=MadCatz+Lynx3&tbm=isch) [out](https://www.google.com/search?q=thrustmaster+eswap&tbm=isch) [there](https://www.google.com/search?q=SteelSeries+3GC&tbm=isch), almost all of which boil down to eight buttons (d-pad and four face buttons), four sholder buttons/triggers, two analog sticks and two small face buttons (clasically, start and select). Maybe the shoulder buttons are analog. Wouldn't it be great if they were all compatible and you could choose from any of them to suit your unique hands or playstyle? And it just seems wasteful to keep making new ones...

To be honest, I'd kind of assumed they _were_ all compatible. I got into gaming again in the pandemic, and played all of Tomb Raider 2013 on Stadia on my Mac with a twenty year old Dual Shock and a twenty year old Playstation controller to USB adapter. I assumed all USB controllers nowadays were just USB HID devices, and would work anywhere, but, no - and worse than that Microsoft and Sony actually have cryptographic authentication built into the Xbox and Playstation so only officially licenced controllers can work! Nintendo doesn't do the authentication thing, at least. They do use their own non-standard USB-based protocol, but that's been throroughly reverse-engineered by now.

So! I'm going to try to make something that will allow me to use my old Dual Shock with my Switch.

## Microcontrollers

There are many, many ways to make USB accessories. I'm using an actually-pretty-long-in-the-tooth one: the 8-bit [ATmega8A microcontroller](https://www.microchip.com/en-us/product/ATmega8A). It's been around for twenty years - and has been _extremely_ sucessful. There are probably some of them in use in devices near you right now! The Arduino is based on its bigger sibling, the ATmega328 - essentially a slightly upgraded version of the ATmega8 with 32K of program space and 2K of RAM.

The ATmega8A pretty tiny as microcontrollers go - only 8K of program memory and 1K of RAM, but that should be enough for this project. More modern chips (even more modern AVR chips) have built-in hardware to talk USB, but the lowly ATmega8A doea not. There are, though, software[^firmware] libraries that implement USB at the cost of using up some CPU cycles that could otherwise be used running our code - I'll be using the popular [V-USB](https://www.obdev.at/products/vusb/index.html). 

An ATmega8A is a _slightly_ quixotic choice for 2022. Why am I using it instead of something like an [RP2040](https://www.raspberrypi.com/products/rp2040/) or an ARM SOC - or a board like a [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) or a [Teensy](https://www.pjrc.com/teensy/)? Mainly because I have a stash of them. And they're still cheap and abundant - here are [10 probably-recycled ones for $18 shipped on AliExpress](https://www.aliexpress.us/item/3256804797261223.html) - or [DigiKey has 14000 in stock for $3.28 each](https://www.digikey.com/en/products/detail/microchip-technology/ATMEGA8A-PU/1914639). I also like that they're easy to work with on breadboards (though you could say the same for a board-based system like the Pi Pico or Teensy).

You might assume that the small amount of program space and relatively slow processor will be problematic - or that it won't be easy to get going writing a USB accessory with an old microcontroller running a USB stack in software. I think that will not be the case, however. I'm pretty sure that the USB stuff will be easy to set up. I'm less sure that the 8K won't bite me later... I guess we'll see!

I'll be using Visual Studio Code as my development environment, with [PlatformIO](https://platformio.org/platformio-ide) to manage the project and compiler toolchain, and generally do the heavy lifting. PlatformIO is an amazing development environment for almost every microcontroller you could imagine - and other embedded systems. [PlatformIO](https://platformio.org/platformio-ide) web site has full installation instructions.

## Setting up the development environment

PlatformIO has wizards to help with this, but I find it easiest to use the command line to set up the project. I open a new VSCode windown on an empty folder, and run pio init to set up the project. You can get to a terminal that has all the PATH etc. by opening" the PlatformIO sidebar (the icon looks like an ant head) and choosing "New Terminal" (in the bottom of the sizdebar, "Quick Access" -> "Miscellaneous" -> "New Terminal").

Here we go! Just run `pio init -b ATmega8`

```
jamie@Jamies-Air ~/D/SwitchControllerAdapter> pio init -b ATmega8

The current working directory /Users/jamie/Development/SwitchControllerAdapter will be used for the project.

The next files/directories have been created in /Users/jamie/Development/SwitchControllerAdapter
include - Put project header files here
lib - Put here project specific (private) libraries
src - Put project source files here
platformio.ini - Project Configuration File
Resolving ATmega8 dependencies...
Already up-to-date.

Project has been successfully initialized! Useful commands:
`pio run` - process/build project from the current directory
`pio run --target upload` or `pio run -t upload` - upload firmware to a target
`pio run --target clean` - clean project (remove compiled files)
`pio run --help` - additional information
```

This creates a project set up for ATmega8, with the Arduino framework. If I want to change it later to another microcontroller (like a 'larger' ATmega one), it'll be easy to do so. I might also want to remove the Arduino framework later to save spave, but the build system is pretty good about only including what's actualy used in the project.

I'll _not_ be using a bootloader on the chip. This will reqire it to be programmed in a programmer every time rather than over USB. I use an Arduino with Evil Mad Scientist's [ISP Shield](https://shop.evilmadscientist.com/productsmenu/253), but you can also use [an Arduino and some jumper wires](https://www.brennantymrak.com/articles/programming-avr-with-arduino), or a 'real' USB programmer. I suppose it would be _nice_ if the finished project could be upgraded over USB (like you can program an Arduino over USB), which will mean it will need a bootloader - but I'll think about that again later.

I'd like to use the 8MHz internal clock on the ATmega - this will eliminate the need for an external timing crystal and its associated circuitry - and also give us the clock pins back to use as general IO should we need them later.

We'll actually be abusing the chip's oscilator tuning later to make it run at 12.8MHz - this enables some clever stuff related to how the USB libraries work - more on that later. But an uncalibrated 8MHz internal oscilator is the base state we need to set up to be able to do this.

To set all these stuff we'll need to 'burn the fuses' of the ATmega. This isn't as hard (or irreprable) as it sounds - it just means sending two eight bit settings values to the chip that it will store and use to set itself up when we power it on. The two values are referred to as the "low fuse" and "high fuse" - but they're just bytes that encode the settings. 

The settings are encoded as bit combinations. Each bit or group of bits has an ALL CAPS NAME. You can encode and decode them by reading the data sheet - or using an [online caculator](https://eleccelerator.com/fusecalc/fusecalc.php?chip=atmega8a) like this. Each of the bits have names. Confusingly to software folks like me, there are counted as "active" if the bit is set to `0` and "not active" if the bit is set to `1`.

I'm setting, for the low fuse:

- `BODLEVEL=1`: Brownout detection on
- `BODEN=0`: Brownout level 2.7V
- `SUT=10`, `CKSEL=0100`: 8MHz internal oscilattor, 64ms startup time (this is probably more startup time than necessary, but means the rest of the circuit has time to stabalize before  processing starts)

All together, this is `010100100`, or `A4` in hex.

And for the high fuse:
- `RSTDISBL=1`: Reset enabled (you could set this to 0 and get the reset pin back as a GPIO pin - but then you'd you wouldn't be able to reprogram the chip without a high voltage programming rig!)
- `WTDON=1`: Watchdog timer off. We may come back to this later!
- `SPIEN=0:` SPI programming enabled. Again, we don't want to switch this off becaues it would make it impossible to reprogram the chip easily.
- `CKOPT=1`: The data sheet tells us this should be switched off (which, remember, is done by setting it to `1`) if we're using the internal oscilator.
- `EESAVE=0`: Preserves the EEPROM when the chip is erased - aso any settings our program stores there will be preserved. This of course means that we better make sure newer firmware understands what the older firmware might've written there.
- `BOOTSZ=00`: These relate to if the chip runs a bootloader when powered on; we are not using a bootloader, so they don't mean anything for us.
- `BOOTRST=1`: 'Boot Reset Vector' disabled (fancy word for bootloader)

which comes to `11010001`, or `0xD1`.

I put these fuse settings into the `platformio.ini` file, and also add some settings to allow us to use the programmer to upload, and tell the compiler/libraries (the software side of thigns) about the speed we'll be running the chip at, which when we're uip and running will be 12.8MHz (again, more on that later).

My platformio.ini file (ommitting the comments at the top) now looks like this:

```ini
[env:ATmega8]
platform = atmelavr
board = ATmega8
framework = arduino


; Chip configuration:

; BODLEVEL=1, BODEN=0, SUT=10, CKSEL=0100
board_fuses.lfuse = 0xA4

; RSTDISBL=1, WTDON=0, SPIEN=0, CKOPT=1, EESAVE=0, BOOTSZ=00, BOOTRST=1
board_fuses.hfuse = 0xD1

; This configures the _software_ to assume a 12.8 MKz clock - it does not affect
; the chip itself.
; We configure the chip with fuses, above, to use the built-in 8MHz internal 
; oscilator. We'll then calibrate it in software to run 'too fast' at 12.8MHz.
board_build.f_cpu = 12800000UL


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

To set the fuses, put the chip into the programmer and hit 'Set Fuses' in VS Code's PlatformIO menu, or run `platformio run --target fuses --environment ATmega8`. Watch out, because by setting the fuses like this ourselves, we're actually overriding some automatic fuse calculations and confusing PlatformIO a bit. It will emit something like this:

```
TARGET CONFIGURATION:
---------------------
Target = atmega8
Clock speed = 12800000UL
Oscillator = external
BOD level = 2.7v
Save EEPROM = yes
UART port = uart0
---------------------

Selected fuses: [lfuse = 0xA4, hfuse = 0xD1]
Setting fuses
```

Note that it says we're using the external oscialtor (`Oscillator = external`), which is incorrect. It's okay though - you can look at the `Selected fuses: [lfuse = 0xA4, hfuse = 0xD1]` line to see that we've overridden things and the correct values for what we want are _actually_ the ones being programmed, it's the human-readable summary above that's confused.


## Checking our work

Okay - the next step is to start coding! Let's take a bit of a diversion first, though, and check that everything we've done so far is working. We'll program the microcontroller with a simple 'blink an LED' program and check it works on a breadboard.

For that, we'll need some code. In the 'src' folder, create a 'main.cpp' file with the traditional Arduino 'blink' example:

```cpp
#include <Arduino.h>

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
}
```

And hit the 'build' button PlatformIO has added to VSCode - it's the checkmark icon in the bottom status bar, or 'Project Tasks -> Default -> General -> Build' in the PlatformIO sidebar. You should see a bunch of output in the terminal window at the bottom of the screen, ending in something glike:

```
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [          ]   0.9% (used 9 bytes from 1024 bytes)
Flash: [=         ]   9.8% (used 754 bytes from 7680 bytes)
==================================================== [SUCCESS] Took 0.25 seconds ====================================================
```

We're already using 9.8% of the program space! Let's keep an eye on that as we go... One thing to notice is that the 7680 figure is actually wrong - PlatformIO is assuming a 512 byte bootloader, and we're not using one. We'll fix that later if we really need those 512 bytes.


## Putting the code into the hardware

Let's program the chip! Hit 'Upload' (it's the left-facing-arrow icon in the bottom status bar, or 'Project Tasks -> Default -> General -> Upload' in the PlatformIO sidebar. It will build the project again if necessary, and upload it.

Now, remove the chip from the programmer, and put it in a breadboard. Wire a 5V supply to pins 7 and 20, and ground to pins 8 and 22, then add a resistor and an LED. The `LED_BUILTIN` pin on Arduino is pin 13 - but we're not using an Arduino, we're using a bare microcontroller, so it's _actually pin 19_ on the chip. (If you want to do this mapping yourself, [this graphic] is very useful. Look at the "Arduino" ovals, and read across to the pin number on the chip legs). I've also added a 10K resistor to pull the reset pin up, and a couple of 0.1uf decoupling capacitors.

Power it on, and, woo! Blinking!



But wait - why's it going so slowly? Remember that we set the fuses to run the internal oscilator at 8MHz, but we told the complier that we're running it at 12.8MHz. We did this by delaying by 1000ms between the ons and offs. At 12.8MHz, that's 12800000 CPU cycles, and it's (simplyfying a bit) numbers of cycles that the compiler encodes into the program. Because we're _actually_ running the chip at 8MHz, 12800000 cycles lasts for 12800000 / 8000000 = 1.6 seconds.

So, everything is actually working perfectly! Again, we'll deal with making the chip _actually_ run at 12800000 MHz a bit later (building the anticipation!)

A blinking LED. I feel like that's the traditional place to stop a first post in a series. See you next time, when we'll get V-USB, tune the oscilator, and set up our circuit to be USB-powered.

[^firmware]: Firmware? The distinction between what 'software' and 'firmware' is is fuzzy at this level of the stack!