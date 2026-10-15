# EnviroNode — STM32 Environmental Monitoring System

EnviroNode is an embedded environmental monitoring system built around the **STM32 NUCLEO-F401RE** development board.

I built the project to gain practical experience with embedded C, microcontrollers, hardware/software integration, interrupts, digital communication protocols, debugging and real-world sensor input.

The current version reads temperature and humidity from a DHT22 sensor, displays the values on an SSD1306 OLED, allows the user to switch display modes with a physical interrupt-driven button, and exposes live diagnostic output over UART.

---

## Features

- Temperature and humidity monitoring using a DHT22
- Custom DHT22 communication code with microsecond timing
- DHT22 checksum validation and failed-read handling
- 0.96" 128×64 SSD1306 OLED display over I²C
- Custom SSD1306 framebuffer and 5×7 text rendering
- Three display modes:
  - temperature and humidity
  - temperature only
  - humidity only
- External push button using a GPIO interrupt
- Software debounce for the external button
- Onboard Nucleo user button support
- Onboard LD2 LED control
- UART diagnostic output through the ST-LINK virtual COM port
- I²C device detection and debugging
- Hardware flashing and debugging through onboard ST-LINK
- CMake/Ninja-based embedded build system
- PowerShell scripts for one-command build/flash and serial monitoring

---

## Hardware

The current build uses:

- **STM32 NUCLEO-F401RE**
  - STM32F401RE microcontroller
  - ARM Cortex-M4
  - up to 84 MHz
  - 512 KB Flash
  - 96 KB SRAM
- DHT22 temperature and humidity sensor
- SSD1306 0.96" 128×64 OLED display
- External tactile push button
- 10 kΩ resistor for the DHT22 data-line pull-up
- Breadboard
- Jumper wires
- Onboard Nucleo user button
- Onboard Nucleo LD2 LED

A passive buzzer is available for a later alert-system stage but is not part of the current firmware.

---

## Current Pin Configuration

| Function | Nucleo / STM32 pin | Notes |
| --- | --- | --- |
| DHT22 data | D7 / PA8 | Bidirectional GPIO during DHT22 communication |
| External button | D2 / PA10 | Falling-edge EXTI interrupt |
| OLED SCL | D15 / PB8 | I2C1 clock |
| OLED SDA | D14 / PB9 | I2C1 data |
| Onboard user button B1 | PC13 | Falling-edge EXTI interrupt |
| Onboard LED LD2 | PA5 | GPIO output |

The DHT22 data line uses an external **10 kΩ pull-up resistor to 3.3 V**.

---

## Technologies

### Firmware

- C
- STM32 HAL
- ARM Cortex-M4
- GPIO
- EXTI interrupts
- I²C
- UART
- DWT cycle counter for microsecond timing
- Fixed-width integer types from `stdint.h`

### Development

- STM32CubeMX
- STM32CubeCLI
- STM32CubeProgrammer
- ST-LINK
- CMake
- Ninja
- GNU tools for STM32
- Visual Studio Code
- PowerShell
- Git / GitHub

---

## System Overview

EnviroNode continuously samples environmental conditions and keeps the most recent valid reading available to the display and UART debug output.

```text
                    +----------------+
                    |     DHT22      |
                    | Temp/Humidity  |
                    +-------+--------+
                            |
                            | PA8
                            v
+----------------+   +-------------------+   I2C1    +----------------+
| External Button|-->| STM32F401RE       |---------->| SSD1306 OLED   |
| D2 / PA10      |   |                   | PB8/PB9   | 128x64         |
+----------------+   | Sensor + Display  |           +----------------+
                     | Logic             |
                     |                   |----------> UART diagnostics
                     +-------------------+
                              |
                              +----------> Onboard LD2
```

The default OLED view shows both readings. Each press of the external button cycles through the display modes:

```text
BOTH -> TEMPERATURE -> HUMIDITY -> BOTH -> ...
```

---

## DHT22 Communication

The DHT22 does not use I²C or UART. Its data is transferred over a single digital line using pulse timings measured in microseconds.

The firmware therefore controls PA8 directly:

```text
STM32 pulls DATA low
        |
        v
DHT22 recognises start signal
        |
        v
DHT22 sends acknowledgement
        |
        v
40 timed data bits
        |
        v
Humidity + Temperature + Checksum
```

The pin is switched between output mode and input mode during each transaction.

The Cortex-M4 DWT cycle counter is used to create microsecond-scale delays so the firmware can distinguish the short and long pulses used to encode binary `0` and `1` values.

The sensor sends five bytes. The fifth byte is a checksum of the first four, allowing corrupted reads to be rejected instead of treated as valid data.

Readings are stored internally in tenths rather than floating-point values. For example:

```text
257 -> 25.7 C
548 -> 54.8 % RH
```

The DHT22 is sampled approximately once every 2.2 seconds. If an individual read fails, the firmware reports the failure over UART and keeps the last known good values on the OLED when available.

---

## OLED Display

The SSD1306 display communicates with the STM32 over **I²C1** using:

```text
PB8 -> SCL
PB9 -> SDA
```

The OLED was detected on the I²C bus at address:

```text
0x3C
```

The display driver is implemented in `ssd1306.c` rather than relying on an Arduino-style display library.

It maintains a 1024-byte framebuffer in STM32 RAM:

```text
128 x 64 pixels = 8192 pixels
8192 bits / 8 = 1024 bytes
```

Text is drawn into this framebuffer using a small custom 5×7 bitmap font. The finished framebuffer is then transferred to the SSD1306 over I²C.

### Default view

```text
TEMP
25.7 C

HUMIDITY
54.8 %
```

### Temperature-only view

```text
      TEMPERATURE

        25.7 C
```

### Humidity-only view

```text
       HUMIDITY

        54.8 %
```

If no valid DHT22 reading has been obtained, the OLED displays a `DHT22 / NO DATA` message instead.

---

## GPIO and Interrupt Handling

Both the onboard button and the external breadboard button use external interrupts.

For the external button:

```text
Button press
    |
    v
PA10 falling edge
    |
    v
EXTI10 interrupt
    |
    v
HAL_GPIO_EXTI_Callback()
    |
    v
Change display mode
    |
    v
Mark display as needing redraw
```

The interrupt handler deliberately does not perform an OLED I²C transfer itself. Instead, it changes the display state and sets a flag. The normal main loop notices that flag and performs the redraw outside the interrupt context.

This keeps the interrupt handler short and avoids doing relatively slow peripheral communication inside an ISR.

### Button debounce

Mechanical switches can generate several rapid electrical transitions during a single physical press. A 200 ms debounce interval is used so one press is treated as one display-mode change.

The onboard B1 button remains configured to toggle the onboard LD2 LED.

---

## UART Diagnostics

USART2 is used for diagnostic output through the Nucleo's ST-LINK virtual COM port at **115200 baud**.

Example output:

```text
Temperature: 25.7 C | Humidity: 54.9 %
Temperature: 25.8 C | Humidity: 54.8 %
DHT22 read failed
Temperature: 25.8 C | Humidity: 54.8 %
```

UART is not required for normal operation. Once flashed, EnviroNode runs independently and displays its readings directly on the OLED.

---

## Project Structure

```text
stm32-environment-monitor/
|
+-- README.md
|
+-- firmware/
    +-- EnviroNode/
        |
        +-- Core/
        |   +-- Inc/
        |   |   +-- main.h
        |   |   +-- ssd1306.h
        |   |
        |   +-- Src/
        |       +-- main.c
        |       +-- ssd1306.c
        |
        +-- Drivers/
        +-- cmake/
        +-- CMakeLists.txt
        +-- CMakePresets.json
        +-- EnviroNode.ioc
        +-- STM32F401xx_FLASH.ld
        +-- startup_stm32f401xe.s
        +-- run.ps1
        +-- monitor.ps1
```

`main.c` contains the application behaviour and DHT22 interface logic.

`ssd1306.c` contains the OLED driver, framebuffer and text-rendering code.

`ssd1306.h` exposes the OLED functions required by the rest of the application.

STM32CubeMX-generated peripheral configuration remains in the project alongside code placed in `USER CODE` sections where appropriate.

---

## Building

From `firmware/EnviroNode`:

```powershell
cube cmake --build --preset Debug
```

The resulting firmware is generated at:

```text
build/Debug/EnviroNode.elf
```

---

## Build and Flash in One Command

The project includes `run.ps1`, which:

1. builds the Debug firmware
2. invokes STM32CubeProgrammer CLI
3. flashes `EnviroNode.elf` over SWD
4. resets the board

Run:

```powershell
.\run.ps1
```

If the local PowerShell execution policy blocks scripts:

```powershell
powershell -ExecutionPolicy Bypass -File .\run.ps1
```

The script currently expects STM32CubeProgrammer at:

```text
C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe
```

After the firmware has been flashed, the serial monitor is not required. The board starts EnviroNode automatically whenever it is powered.

---

## Serial Monitor

For UART diagnostics, run:

```powershell
.\monitor.ps1
```

The script automatically searches for the ST-LINK virtual COM port and opens it at 115200 baud, avoiding the need to manually create a PowerShell `SerialPort` object or hard-code a COM number each session.

Press `Ctrl+C` to stop monitoring.

---

## Development Process

The project was developed incrementally so that hardware and firmware problems could be isolated rather than debugged as one large system.

```text
STM32 project setup
        |
        v
Onboard LED output
        |
        v
Onboard button interrupt
        |
        v
External breadboard button
        |
        v
DHT22 communication
        |
        v
UART sensor diagnostics
        |
        v
I2C configuration and bus scan
        |
        v
SSD1306 detection at 0x3C
        |
        v
Custom OLED driver + framebuffer
        |
        v
Interrupt-driven display mode switching
        |
        v
Automated build / flash / serial scripts
```

Several issues required debugging across both hardware and firmware, including incorrect I²C pin mapping, verifying the OLED address, configuring GPIO interrupt modes, handling DHT22 timing and validating physical breadboard connections.

---

## What I Learned

This project provided practical experience with areas that are difficult to reproduce in software-only work, including:

- embedded C development
- ARM Cortex-M microcontrollers
- GPIO input and output configuration
- exact-width integer types
- pointers and memory addresses
- bitwise operations
- `static` and `volatile` in embedded C
- active-high and active-low signals
- external pull-up resistors
- GPIO external interrupts
- NVIC interrupt configuration
- interrupt service routines
- button debouncing
- microsecond timing using the Cortex-M DWT cycle counter
- implementing a simple digital sensor protocol directly
- checksum validation
- I²C peripheral communication
- I²C device addressing and bus scanning
- UART diagnostics
- framebuffers and bitmap text rendering
- separating a peripheral driver into `.c` and `.h` files
- reading microcontroller pin mappings
- flashing and debugging through SWD/ST-LINK
- working with STM32CubeMX-generated code
- cross-compilation with the GNU Arm toolchain
- CMake/Ninja embedded builds
- debugging the complete path from physical wiring to firmware behaviour

One of the most useful lessons was seeing how different embedded debugging is from ordinary application development: code can compile correctly while the system still fails because of pin mappings, electrical connections, timing, peripheral configuration or communication protocol behaviour.

---

## Future Improvements

Possible next stages include:

- integrate the passive buzzer
- generate warning tones using a hardware timer and PWM
- configurable temperature and humidity alert thresholds
- explicit `NORMAL`, `WARNING` and `FAULT` application states
- historical minimum and maximum readings
- configurable values stored in Flash
- watchdog support
- additional environmental sensors
- non-blocking or timer-assisted DHT22 acquisition
- DMA-based peripheral transfers where useful
- lower-power operating modes
- FreeRTOS task-based architecture
- host-side data logging and visualisation

---

## Motivation

I built EnviroNode to move beyond desktop and higher-level software and gain practical experience with software that directly controls physical hardware.

The project gave me exposure to the complete embedded development workflow:

```text
hardware wiring
-> peripheral configuration
-> embedded C development
-> cross-compilation
-> flashing
-> serial diagnostics
-> hardware testing
-> debugging
-> iteration
```

It also gave me a practical introduction to firmware architecture and a foundation for further work in embedded software, low-level systems, robotics and other hardware-focused software engineering areas.
