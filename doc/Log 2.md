In this part, we'll get [V-USB](https://www.obdev.at/products/vusb/index.html) set up.

As mentioned previously, the Atmega8A doesn't have any USB hardware - but there's a well respected software library called [V-USB](https://www.obdev.at/products/vusb/index.html) that implements the USB protocol in software, at the expense of a taking up some CPU time, and taking away a little timing flexibilty in timing (it needs to be able to run to service the USB bus at specific times). For most uses, neither of these are issues.

It's licensed under GPL 2 - meaning that if you distribute anything based on it you also have to offer the source code - but if you're making a commercial product and _don't_ want to distribute your source you can get a commercial licence from Objective Development. I suspect there are a lot of people violating the lincense out there...

## Getting V-USB

Let's get V-USB. To us software engineers, the way embedded folks distribute libraries is a little odd. The traditional way is to download a [release from the web site](https://www.obdev.at/products/vusb/download.html), unzip it, and just copy the `usbdrv` folder into your project!

There's been a bit of development and bug fixing since the last official release (in 2012!), so I'm going to use the latest code from [the V-USB GitHub repo](https://github.com/obdev/v-usb). We'll still need to embed it in our project and edit some config files manually though.

In a PlatformIO project, you can just put libraries into the 'lib' folder that PlatformIO already created for you, so what we need to do is copy the `usbdrv` folder from the V-USB repo into `lib`. Note that's the `usbdrv` subfolder not the top-level of the V-USB repo!

I'm already using Git to manage my prohject, so I'm going to use `git subtree` in a slightly convoluted manner:

```
jamie@Jamies-Air ~/D/SwitchControllerAdapter (main)> git remote add -f v-usb https://github.com/obdev/v-usb
Updating v-usb
From https://github.com/obdev/v-usb
 * [new branch]          master     -> v-usb/master

jamie@Jamies-Air ~/D/SwitchControllerAdapter (main)> git checkout -b v-usb-staging v-usb/master
branch 'v-usb-staging' set up to track 'v-usb/master'.
Switched to a new branch 'v-usb-staging'

jamie@Jamies-Air ~/D/SwitchControllerAdapter (v-usb-staging)> git subtree split --prefix=usbdrv -b v-usb-usbdrv-staging
Created branch 'v-usb-usbdrv-staging'
71993703f7e3e9d66d34d0cfe6c35f8285e5c070

jamie@Jamies-Air ~/D/SwitchControllerAdapter (v-usb-staging)> git checkout main
Switched to branch 'main'
Your branch is up to date with 'origin/main'.
```

I'm not going to go into a git tutorial here, that's rather off-topic (and I know there are _opinions_ out there on subtree management...). Whatever you do, you just need to ensure that the `usbdrv` folder, whith the contents as listed above, ends up inside the `libs` folder:

```
jamie@Jamies-Air ~/D/SwitchControllerAdapter (main)> ls lib/usbdrv/
Changelog.txt          oddebug.h              usbdrvasm15.inc
CommercialLicense.txt  usbconfig-prototype.h  usbdrvasm16.inc
License.txt            usbdrv.c               usbdrvasm165.inc
Readme.txt             usbdrv.h               usbdrvasm18-crc.inc
USB-ID-FAQ.txt         usbdrvasm.S            usbdrvasm18.inc
USB-IDs-for-free.txt   usbdrvasm.asm          usbdrvasm20.inc
asmcommon.inc          usbdrvasm12.inc        usbportability.h
oddebug.c              usbdrvasm128.inc
```

## Getting V-USB to compile

Okay! Let's see how we're doing - is V-USB working now? Hit 'Build'!

```
...
lib/usbdrv/usbdrvasm.asm: Assembler messages:
lib/usbdrv/usbdrvasm.asm:20: Error: unknown opcode `end'
...
In file included from lib/usbdrv/usbdrv.c:10:0:
lib/usbdrv/usbdrv.h:127:10: fatal error: usbconfig.h: No such file or directory
...
```

Hmm. Two errors. Let's take them one at a time.

For the first one, if you take a look at `lib/usbdrv/usbdrvasm.asm`, you will see it says that it's for the "IAR compiler/assembler system". That's not what we're using. And all it does is include `usbdrvasm.S`, which PlatformIO is already compiling for us. We can actually just not compile it. We'll comr backl to this after we deal with the second error.

The second error should not really be surprising. We still need to configure V-USB, and that's done by making a file called `usbconfig.h`. So the reason compilation is failins is that we haven't done that yet. Making this file is not as arduous as it sounds - it's generally done by copying `usbconfig-prototype.h`, in the `usbdrv` folder, to `usbconfig.h`, and making changes to it. 

I don't really want to make my own `usbconfig.h` in the otherwise pristine copy of V-USB in the `lib` folder though. I'd like to keep that as a 'clean' copy of V-USB. So I will put my configuraiton in the top-level `include` folder. 

Create a copy of `usbconfig-prototype.h` in the `include` folder PlatformIO at the top-level of our project, called `usbconfig.h`.

We have two things we need to do now. We need to somehow let V-USB know we've put its config file in another folder, and we also need to make PlatformIO skip the compilation of `usbdrvasm.asm`.

Unfortunately, it's not quite possible ot do this _just_ by editing the `platformio.ini` file as you might expect. But that's okay - the makers of PlatformIO have made it possible to configure it further by writing python code! Don't be too nervous, we're just going to need a little.

Create a `v-usb_platformio_helper.py` file at the top level of the folder, beside `platformio.ini`, and put this in it:

``` python
Import("env")

# V-USB has a '.asm' file in the usbdrv folder that PlatformIO will try to 
# compile, but it's really just for "The IAR compiler/assembler system".
# We'll make it be skipped.
def skip_file(node):
    return None
env.AddBuildMiddleware(skip_file, "*/lib/usbdrv/*.asm")

# Make sure V-USB can find its config file, which we've placed inside the 
# top-level include folder.
def add_usbdrv_include(node):
    return env.Object(
        node,
        CFLAGS=env["CFLAGS"] + ["-Iinclude"],
        ASFLAGS=env["ASFLAGS"] + ["-Iinclude"],
    )
env.AddBuildMiddleware(add_usbdrv_include, "*/lib/usbdrv/*.[chS]")
```

All this is doing is adding two hooks to the PlatformIO build process. The first will cause it to skip any 'asm' files in the `usbdrv` folder, the second will cause it to add `-Iinclude` to the compiler flags for C or assembly files in the `usbdrv` folder.

The only thing remaining to do is to tell PlatformIO to actually use our Python file.

Go back to `platformio.ini` and add `extra_scripts = pre:v-usb_platformio_helper.py`[^fullplatformioinifile].

Okay, with this done, let's hit 'Build' again!

```
...
Linking .pio/build/ATmega8/firmware.elf
Checking size .pio/build/ATmega8/firmware.elf
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [          ]   0.9% (used 9 bytes from 1024 bytes)
Flash: [=         ]   9.8% (used 754 bytes from 7680 bytes)
Building .pio/build/ATmega8/firmware.hex
===================================================================== [SUCCESS] Took 0.72 seconds =====================================================================
```

Woohoo! It all compiled - and, look, we're still using only 754 bytes of program space!

Wait, though - isn't that the same amount as we were using before we added V-USB? Yes. Although it _does_ compile it all, the build system cleverly doesn't include any code we're not actually using in the final binary. We can expect this to grow as we actually _use_ V-USB.


# Using V-USB

Great, now we're ready to use V-USB! Or are we? Unfortunately, no. We still need to _actually_ configure it. All we've done so far is copy the 'prototype' configuration. Things from now on out aren't nearly so involved though - we've basically reached the end of our configuration of PlatformIO, and are ready to start actually coding things that do stuff.


# Configuring V-USB

Open the `include/usbconfig.h` file we made a littel while ago. It's actually really well documented in comments. If you want to do a deep dive yourself, feel free to go and have a read.





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