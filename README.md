# A ESP32 driven light switch for LIFX devices
Because now, apparently, we need a WiFi connection and a microprocessor to switch on and off a light.

**Current project state: Experimental.** "It works for me" but there are noted issues and it is not in a stable state — YMMV.

**License: MIT.** Do what you want but do the right thing and share your own project.

## Setup
- An ESP32 device. [Tested with the Sparkfun ESP32 Thing.](https://www.sparkfun.com/products/13907)
- Arduino IDE and ability to build to the ESP32.
- Follow the instructions in `variables.h.example` to build your own `variables.h`

### Getting the LIFX device MAC address
Use [lifx-cmd](https://github.com/MichaelAquilina/lifx-cmd) to discover LIFX devices on the LAN along with their MAC addresses.

## Operation
- The ESP32 will run setup then loop will await changes a button connected to the ESP32.
- After the button debounces, the ESP32 will connect to the WiFi network and change the power state of the LIFX device.
- The ESP32 will then enter back into the loop state noting the new set power state of the LIFX device.

## Issues
- The UDP payload struct has always been a bit unclear. With both official and unofficial examples conflicting. An example of an issue is that a LIFX device wont respond to a payload with a fade duration defined.
- The handling of the WiFi state is basic. There should be improvements to maintianing the connection so that there is little to no delay from button press to the light state changing.
