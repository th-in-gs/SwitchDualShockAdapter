---
title: "Switch Dual Shock adapter part 3: A fake USB HID controller"
date: 2023-01-11
tags: [switch-dual-shock-adapter-series, electronics, programming, hardware, atmega, atmega8a, avr, v-usb, usb, hid, usb-hid, platformio, arduino, nintendo-switch, dual-shock, project-log]
---
*In this [series of posts](https://www.blog.montgomerie.net/tags/switch-dual-shock-adapter-series/), I'm attempting to make a [Dual Shock](https://en.wikipedia.org/wiki/DualShock) to [Switch](https://www.nintendo.com/switch/) controller adapter. It will plug into the Switch Dock's USB port.*

[Last time](https://www.blog.montgomerie.net/posts/2022-12-24-switch-dual-shock-adapter-part-2-connecting-to-usb/), I ended with a real USB device that would communicate with a host computer - but all it would do is blink a light on and off. 

![A breadboard with a USB cable attached to it. A 3.3v regulator is attached to the +5V and GND lines of the USB cable and powering the breadboards power lines. In the middle is an ATmega8A and an LED. There are two small resistors leading from the USB D+ and D- lines to the ATMega8A. There are also the various associated resistors and capacitors necessary for things to work.](USBLED.jpeg)

<!--more-->

This time, I'm going to make it _control_ the computer. I'm going to make it into a fake controller that pretends to send input from sixteen non-existent buttons and two non-existent analog sticks to my computer[^buttons].

I know that at the end of the last post I promised that "next time [...] I will try to actually communicate with a Nintendo Switch!" - but I actually took a bit of a detour to learn more about USB HID devices first - so that's what I'm going to talk about in this post.

What's a "USB HID device"? It's a "*USB* *H*uman *I*nterface *D*evice", uh, "device"[^devicedevice]. USB HID is an entire specification _on top_ of the USB specification describing how to make human interface devices[^specifications]. It specifies how devices like keyboards and mice and *joysticks and gamepads* (and more esoteric things like water cooling devices!) communicate.

The Switch uses the USB HID specification to communicate with the Switch Pro Controller. Well, sort of... It arguably doesn't really. But that's a discussion for next time[^newtonian]. Learning about USB HID communication will be helpful in understanding what the Switch expects in a gamepad, and how Switch <-> Pro Controller USB communication works.


## Descriptors

I did a bunch of research before and while doing this - in particular, [this USB HID tutorial by Frank Zhao](https://eleccelerator.com/tutorial-about-usb-hid-report-descriptors/) was really helpful, as was [this video about USB keyboards](https://www.youtube.com/watch?v=wdgULBpRoXk) by Ben Eater. 

USB HID is sort of magic compared to what came before in how communication and device state is encoded. The USB keyboard I'm typing on now, for example, probably doesn't encode keystrokes, at a binary level, in the same way as yours does - yet they both work! 

The magic is accomplished with a data structure called a 'descriptor'.

I can pretty much decide how _I_ would like to store the controller state, rather than doing what a spec requires, which is pretty nice.

This will not be true, of course, when we come to try to simulate a Switch Pro Controller. We'll need to do things the way a real controller does in order to fool the Switch. But I expect to be able to use what I learn now when trying to understand that.


I added a `descriptors.h` file to my `src` folder. Here is its entire contents:

```c++
#ifndef __descriptors_h_included__
#define __descriptors_h_included__

#include <stdint.h>

typedef struct {
    uint8_t reportId;

    uint8_t buttons1to8;
    uint8_t buttons9to16;

    int8_t leftStickX;
    int8_t leftStickY;
    int8_t rightStickX;
    int8_t rightStickY;
} GameControllerInputReport;

#endif

```

I'll use a `GameControllerInputReport` to store - and send - the state of our fake buttons and analog sticks. Here's how it breaks down:

`reportId` is the only part mandated by USB. It needs to come first, and contains an eight-bit value that will be used by the host to understand which part of the descriptor to use to decode the data. I'll talk about this more when we get to looking at our descriptor in a bit.

Next come another two `uint8_t` (so, just unsigned 8-bit values), `buttons1to8` and `buttons9to16`. Each of the sixteen bits of these two eight bit values will hold the state of one controller button (`1` = currently pressed, `0` = not currently pressed).

Next, four `int8_t`s (so _signed_ 8-bit values), that will encode the state of the four axes of the two analog controls (-127 to 127 for each axis, with 0 in the center).

When we want to communicate the controller's state to the computer all we'll need to do is fill in a `GameControllerInputReport` and give it to V-USB to send!

Next, the magic descriptor to describe our `GameControllerInputReport` to the computer. I put this in a new `descriptors.c` file:


```c++
PROGMEM const char usbDescriptorHidReport[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x04,        // Usage (Joystick)
    0xA1, 0x01,        // Collection (Application)
    0xA1, 0x00,        //   Collection (Physical)
    0x85, 0x42,        //     Report ID (0x42)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x10,        //     Usage Maximum (0x10)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x10,        //     Report Count (16)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x32,        //     Usage (Z)
    0x09, 0x33,        //     Usage (Rx)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x04,        //     Report Count (4)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0xC0,              //   End Collection
    0xC0,              // End Collection
    
    // 48 bytes
};

```

Woah, that's a lot of hexadecimal! It's basically, though, just a way of encoding "we'll be reporting 16 bits of button state and 4 bytes of analog stick state".

Without reading the specs[^specifications], the comments[^hidparser] reveal a bit about what it all means - let's break it down further.

Each line is an 'item'. Where there are two hex numbers on one line, the first number usually describes what kind of thing the item 'is', sort of like specifying type in C, and the second number is the value of the item.

Let's go one item at a time. 

`0x05` means "the next number is a 'usage page'", and `0x01` refers to the 'Generic Desktop' usage page. A 'usage page' is really just a list of number-to-meaning mappings defined in the USB HID specification documents. This item is saying "from now on, the codes we'll use are those defined in the 'Generic Desktop' usage page".

On the next line, `0x09` means 'Usage', and `0x04` is 'Joystick'. That's saying "from now on, we're describing a joystick"[^joystick].

Next, we get to some meat - we're starting a 'collection'. The  HID spec has a lot to say about collections, but for now it's enough to say that an 'application' collection containing a 'physical' collection is telling that host that we're going to describe how the physical state of our joystick is encoded, down to the bit level - in more concrete terms, we'll describe the layout of the struct we defined above. 

Every 'collection' `0xA1` pair has to be matched by with a single 'end collection' `0xC0` byte to mark the end of the collection. Jumping to the end, you can see that these are the last two bytes in our descriptor. The comments on the right of the descriptor are formatted with indentation to make nesting of collections more obvious.

Next, `0x85, 0x42`: our collection will use report ID `0x42`. This is a number we make up. When we use our struct, we'll need to set its `reportId` field to `0x42` so that the host knows that the rest of it is of the format we're describing here.

The next chunk all goes together:


```c++
0x05, 0x09,   // Usage Page (Button)
0x19, 0x01,   // Usage Minimum (1)
0x29, 0x10,   // Usage Maximum (16)
0x15, 0x00,   // Logical Minimum (0)
0x25, 0x01,   // Logical Maximum (1)
0x75, 0x01,   // Report Size (1)
0x95, 0x10,   // Report Count (16)
0x81, 0x02,   // Input (Data,Var,Abs)
```

Believe it or not, this is just a verbose way of describing our 16 bits of button state - `buttons1to8` and `buttons9to16` in our struct! 

It's saying: Here will be some buttons, numbered from 1 (minimum) to 16 (maximum). Each button will have a state of 0 (minimum) to 1 (maximum). Each button will be reported as 1 bit, and there will be sixteen reports. 

It seems a bit redundant to say that each button has values from 0-1 and also that the value takes up one bit of storage. It also seems a bit redundant to say that we'll be describing buttons 1-16, and also that there will be 16 reports. But that's how we have to do it. USB HID descriptors can describe things _way_ more complex than on/off buttons, so the flexibility this format allows is sometimes useful.

The last two bytes - the 'input' item - `0x81, 0x02` - are more 'what this data is' description. They indicate that this is input data ('input' means 'sent from the device to the host'), and that it's made up of discrete, absolute values. The USB HID spec goes into more detail on what can be encoded in the value byte of an 'input' item, but suffice to say this is right for our buttons.


That's followed by another chunk, which all together describes our two analog sticks:


```c++
0x05, 0x01,   // Usage Page (Generic Desktop Ctrls)
0x09, 0x30,   // Usage (X)
0x09, 0x31,   // Usage (Y)
0x09, 0x32,   // Usage (Z)
0x09, 0x33,   // Usage (Rx)
0x15, 0x81,   // Logical Minimum (-127)
0x25, 0x7F,   // Logical Maximum (127)
0x75, 0x08,   // Report Size (8)
0x95, 0x04,   // Report Count (4)
0x81, 0x02,   // Input (Data,Var,Abs)
```

Here we're saying we have things to describe named `X`, `Y`, `Z` and `Rx` (`X` and `Y` are the X and Y axes of the left analog stick, `Z` and `Rx`, for some reason, are the standard names for the X and Y axes of the right stick). They'll have values from -127 to 127. Each value will have a size of 8 bits, and there will be 4 values reported. Then we have our standard 'input' item, like for the buttons.

And that's it! It's pretty verbose, but it's a complete description of the struct we defined above down to the bit level, along with a bit of more semantic information. With a bit of imagination, you can see how this could stretch to describing the sort of structure you'd need to store the state of a keyboard or mouse, or other more fancy human interface devices.


## Interruptions

Okay, so I now have a descriptor that V-USB can send to the host to tell it how our reports of controller state will be formatted, and what that state means. I also need to update V-USB's configuration, to tell it what kind of device we're implementing, and write the code to generate and send the reports.

First, the configuration. In `usbconfig.h` I set `USB_CFG_HAVE_INTRIN_ENDPOINT` to `1`. This tells V-USB that we need an 'interrupt endpoint'. By default it will be endpoint number 1.

An 'endpoint' is sort of like a port, if you're familiar with internet protocols. At a low level, it's a tag that the host includes in any communication to the device to tell it how to interpret the packets it's sending. USB 'control' requests - mostly handled by V-USB for us - are sent to the default endpoint 0. We will use endpoint 1 to send our controller state to the host.

What's an '_interrupt_' endpoint? Oh boy...

Let's back up. In traditional tech jargon, an 'interrupt' is a signal a device sends to a host that means 'Check me out now! I have new data for you!'.  Before USB, it was common, for example, for devices with buttons to just send an interrupt to a computer whenever their state changed. Often, they'd do this by literally changing an output from 0V to +5V. Hardware in the computer would notice the +5V interrupt signal, and cause the CPU to stop what it was doing, read the state - then go back to what it was doing previously. The CPU was literally 'interrupted' by the device when it had state to send to it. This works really well for timely reading of keypads and the like.

In USB, though, there is *no way* for a device to signal to a host! *All* communication is initiated by the host. Obviously, this is not great if you have a device that needs to tell its host when its state changes - like a human interface device often does. So the USB folks came up with a way to _simulate_ interrupts by initiating communication _from the host to the device_ regularly - basically asking 'You got anything for me?' on a regular cadence. The host sends an 'interrupt' message tagged with an endpoint number to the device on a regular schedule, giving the device the opportunity to send information back. This is what's known as an 'interrupt endpoint'. 

If you are thinking "wait, this is the opposite of an interrupt - it's polling!" you are absolutely correct. But it's still called an 'interrupt' in USB parlance. ¯\\\_(ツ)\_/¯.

A USB HID device can specify (in its 'device descriptor' - a cousin of the HID descriptor that V-USB takes care of for us) how many endpoints it has, and, for interrupt endpoints, how often it wants the 'interrupt' to happen. 

The device descriptor also specifies the 'direction' of the endpoint: `IN` for data being sent from the device _in_ to the host; or `OUT` for data that the host might want to send _out_ to the device to control it. 

V-USB has the ability to create up to two interrupt endpoints (though USB allows many more!). What my change to `USB_CFG_HAVE_INTRIN_ENDPOINT` does is configure V-USB to set up one `INT`e`R`upt `IN` endpoint. It has a default 10ms polling rate. That rate can be configured, but I'm not going to change it.[^windows]

I also updated `USB_CFG_DEVICE_CLASS` to `0` (this is used when V-USB builds our device descriptor - '0' means 'defer to the interface descriptor') and  `USB_CFG_INTERFACE_CLASS` to `3` (this is used when V-USB builds the interface descriptor - `3` means 'HID'), and `USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH` to `48` (to tell V-USB the size of our `usbDescriptorHidReport` - note that this is the length of the _descriptor_, not the structure it describes).

Lastly, I updated `USB_CFG_DEVICE_ID`, and finally the vendor name and device names to make them more personal. I followed the rules in [V-USB's `USB-IDs-for-free.txt` document](https://github.com/obdev/v-usb/blob/master/usbdrv/USB-IDs-for-free.txt).

For completeness, [here's the entire config file now](https://github.com/th-in-gs/SwitchDualShockAdapter/blob/blog_post_3/include/usbconfig.h).


## Writing the code

In `main.cpp` I `#include`d our new `descriptors.h`, and added two new static globals:

```c++
static GameControllerInputReport sReports[2] = { 0 };
volatile static uint8_t sReportReadIndex = 0;
```

The idea behind having two buffers is that I can be preparing one buffer while the other is being sent. In retrospect, I don't think that's actually necessary because V-USB will copy the data whenever I supply it, and it's not possible for my code itself to be dealing with more than one report at the same time. But since this is really just a test, I've left it like this for now.

I also added a new `prepareReport` function:

```c++
static void prepareReport()
{
    static uint16_t buttonCounter = 1;
    uint8_t nextReportIndex = sReportReadIndex == 1 ? 0 : 1;
    GameControllerInputReport *report = &sReports[nextReportIndex];
    report->reportId = 0x42;

    uint8_t fakeButtons = (uint8_t)(buttonCounter >> 2);
    report->buttons1to7 = fakeButtons;
    report->buttons8to16 = 0xff - fakeButtons;
    report->leftStickX = fakeButtons;
    report->leftStickY = 0xff - fakeButtons;
    report->rightStickX = 0xff - fakeButtons;
    report->rightStickY = fakeButtons;

    ++buttonCounter;
    sReportReadIndex = nextReportIndex;
}

```

This just sets the report's ID to `0x42` (the ID our descriptor describes, remember), and puts some data into the report that changes a little over time.

I changed the main loop to:

```c++
void loop()
{
    ledHeartbeat();
    usbPoll();
    if(usbInterruptIsReady()) {
        prepareReport();
        usbSetInterrupt((unsigned char*)&sReports[sReportReadIndex], 
                        sizeof(*sReports));
    }
}
```

`usbInterruptIsReady()` returns `true` whenever V-USB is ready to send more data on interrupt endpoint 1. The call to `usbSetInterrupt(...)` supplies V-USB with the data to send the next time an interrupt-IN message arrives from the host for endpoint 1 - or in non-USB-jargon terms, the next time the host polls us for input state.


And last of all, because all the examples do it, I made some changes to the `usbFunctionSetup` function. It's the function that V-USB calls when it receives a 'setup' message to endpoint 0. The host _can_ use these messages to ask for an update on the current state (by sending a `USBRQ_HID_GET_REPORT` message), or to set our 'idle rate'. I don't think the 'idle rate' means anything important for a controller. For a keyboard, I've read that it's the key repeat rate. 

```c++
usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    usbRequest_t* rq = (usbRequest_t*)data;

    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {
        if(rq->bRequest == USBRQ_HID_GET_REPORT) {
            if(rq->wValue.bytes[0] == 0x42) {
                usbMsgPtr = (typeof(usbMsgPtr))(&sReports[sReportReadIndex]);
                return sizeof(*sReports);
            }
        } else if(rq->bRequest == USBRQ_HID_GET_IDLE) {
            usbMsgPtr = &sIdleRate;
            return 1;
        } else if(rq->bRequest == USBRQ_HID_SET_IDLE) {
            sIdleRate = rq->wValue.bytes[1];
        }
    }

    return 0; /* default for not implemented requests: return no data back */
}
```

Apparently we need to deal with messages like this to meet the USB HID spec. I will say that, in testing all this out, I've never actually seen this code being run, so I suspect it's not necessary in practice, at least when plugged into a Mac.

[Here's the entire main.cpp file now](https://github.com/th-in-gs/SwitchDualShockAdapter/blob/blog_post_3/src/main.cpp) - there's still not much to it!

And that's it! our fake controller is done. Let's build...
```
...
RAM:   [=         ]   8.8% (used 90 bytes from 1024 bytes)
Flash: [====      ]  37.1% (used 2848 bytes from 7680 bytes)
Building .pio/build/ATmega8/firmware.hex
======================================================= [SUCCESS] Took 0.79 seconds =======================================================
```


...and upload it to the ATmega8 and test it out! 

I downloaded an app called [Joystick Monitor](https://apps.apple.com/us/app/joystick-monitor/id1361339902) on my Mac to help with testing. Here's the result:

{{< video src="JoystickMonitor" controls="false" playsinline="true" loop="true" autoplay="true" muted="true" >}}

Looks like it's working! A fake controller, pressing its buttons and moving its sticks.


## Conclusion

If I wanted to make a Dual Shock to USB adapter (to make the Dual Shock usable on my Mac, for example), 'all' I'd have left to do would be interface with the Dual Shock, and use its state to fill out the report structure.

That's not what I want to do, of course. Unfortunately Nintendo didn't make the Switch compatible with standard USB controllers - but they do sort-of use the USB HID spec as a backbone for communication. Next time, I'm (for real this time!) get this breadboard attached to, and controlling, a Switch.


*You can follow the code for this series [on GitHub](https://github.com/th-in-gs/SwitchDualShockAdapter). The `main` branch there will grow as this series does. The [`blog_post_3`](https://github.com/th-in-gs/SwitchDualShockAdapter/tree/blog_post_3) tag corresponds to the code as it is (was) at the end of this post.*

*All the posts in this series will be [listed here as they appear](https://www.blog.montgomerie.net/tags/switch-dual-shock-adapter-series).*


[^buttons]: There's no reason to limit myself to sixteen buttons here. That just seemed like enough for a fake controller. Those sixteen buttons could be four 'ABXY' buttons, four digital d-pad directions, four shoulder and trigger buttons, two face buttons ('start' and 'select' or their descendants), and two 'press the analog stick down' buttons. You might also want a share or capture button, a home button, a power button - the list is potentially endless...

[^devicedevice]: Like an ATM machine.

[^specifications]: Unlike frustratingly many industry standards, USB specs are free to download! Thank you USB-IF! You can get the [USB 2 spec here](https://www.usb.org/document-library/usb-20-specification), and the additional [USB HID spec here](https://www.usb.org/document-library/device-class-definition-hid-111).

[^newtonian]: This is one of those things like when you learn Newtonian physics and then the next year they tell you about relativity and how all that Newtonian stuff you just learned is all wrong.

[^host]: A host is something like a computer - or a game console - something a device would plug into.

[^extraendpoints]: If you were making some fancy keyboard/mouse combo device, you might specify in its descriptor that it had two interrupt endpoints, one for the keyboard and one for the mouse.


[^hidparser]: The nice formatting here is courtesy of [Frank Zhao's excellent USB HID parsing tool](https://eleccelerator.com/usbdescreqparser/)

[^joystick]: I'm using `0x04`, which corresponds to 'joystick'. The HID spec actually also allows `0x05` for 'game controller'. For some reason it seems that the world has settled on specifying gamepads as joysticks though. Perhaps it's because in distant olden days, when HID was being defined, most gamepads were digital, and joysticks (think for a light sim) were analog - but now all gamepads have analog sticks, so are closer to what joysticks were back then?

[^windows]: Technically the 10ms is a _recommendation_. Reportedly Windows, for example, will ignore our request and poll at 8ms intervals.