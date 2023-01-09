


Now that we've got USB hardware working, I want to try to emulate a Nintendo Switch controller.

If I were _absolutely sure_ this would all work, it might make sense to design and implement the system to read DualShock input first, because then the system would be manually testable, but I'm not, and I really want ot be sure that the idea of interfacing with the Switch will work.

To do this, we'll need to make our system receive and send data like a Nintendo Switch Pro Controller.

USB is based aroud descriptors.


Luckily, we're not the first to attempt to interface with a Switch, and others have reported how to implement a USB device that the Switch will interpret as a Switch Pro Contrtoller.

[This repository on GitHub](https://github.com/progmem/Switch-Fightstick) appears to be what the majority of attempts to do this are based on, and it has a [friendly MIT license](https://github.com/progmem/Switch-Fightstick/blob/master/LICENSE), so it makes sense that we could use it too. [Its ReadMe](https://github.com/progmem/Switch-Fightstick/blob/master/README.md) says it is a:

> Proof-of-Concept Fightstick for the Nintendo Switch. Uses the LUFA library and reverse-engineering of the Pokken Tournament Pro Pad for the Wii U to enable custom fightsticks on the Switch System v3.0.0.

LUFA(http://www.fourwalledcubicle.com/LUFA.php), is a similar library to V-USB, but it requires the microcontroller to have built-in USB hardware (which most ATMegas chips do not).

