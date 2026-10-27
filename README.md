# NRL SDK
### National Robotics League · for the Hexa Command Hub

The NRL SDK is the software kit teams use to program their robots: the HexaNRL runtime, the
Hexa* hardware libraries, the project and OpMode wizards, and the Blocks editor. It runs on the
Hexa Command Hub (robot) and the NRL controller, both built on ESP32-S3. Students write only
their game logic in `opmodes/`; the SDK handles hardware, wireless comms, timing, and display.

---

## Hardware Required

| Part | Quantity | Used For |
|------|----------|----------|
| ESP32-S3 DevKit | 2 | One for Robot, one for Controller |
| Servo motors | Up to 4 | Robot joints/arms |
| DC motors | Up to 4 | Drive system |
| SSD1306 OLED (128×64, I2C) | 1 | Robot status display |
| 240×320 TFT LCD (SPI) | 1 | Controller UI |
| LSM6DSOX IMU | 1 | Controller orientation |
| MCP3008 8-ch SPI ADC | 1 | Robot battery-voltage + current sensing |
| ACS712 current sensor | 2 | DC-motor rail + servo rail current |

---

## Power & Current Sensing (MCP3008)

The robot board carries an **MCP3008** (8-channel, 10-bit SPI ADC) that reads the battery
voltage and two ACS712 current sensors. There are two ways you read the data:

**Battery voltage** — the always-available `power` object (board-level, no declaration needed):

```cpp
power.getBatteryVoltage();   // robot pack voltage (V)
power.isBatteryLow();        // true when the pack is low
```

**Rail current** — read it directly from `power` (board-level, no declaration needed):

```cpp
power.getMainCurrent();      // total battery current (A) — CH1
power.getMotorCurrent();     // DC-motor branch current (A) — CH2
power.getServoCurrent();     // servo rail current (A) — derived: main − motor
```

> ⚠️ There is **one ACS712 per rail**, not one per motor/servo, and there is no dedicated
> servo-current sensor at all — servo current is *derived* as (main total − motor branch).
> `HexaDCMotor`/`HexaServo` intentionally have no `getCurrent()` of their own (it used to
> exist and just proxied to the same shared reading per motor/servo, which looked like
> individual per-device data but wasn't) — read the rail directly through `power` instead.

The robot's battery voltage is also sent to the Controller and shown in its header.

**MCP3008 channel map / pins** (also in `RobotFirmware/lib/HexaNRL/src/BoardPins.h`):

| Channel | Signal | SPI pin | GPIO |
|---------|--------|---------|------|
| CH0 | Battery voltage sensor | CS | 8 |
| CH1 | ACS712 #1 — MAIN / total battery current | SCK | 14 |
| CH2 | ACS712 #2 — DC-motor branch current | MISO | 9 |
| CH3–CH7 | External ADC breakout connectors | MOSI | 13 |

**Calibration:** VREF = **5.0 V** on this board (not 3.3 V). Defaults come from the verified
manufacturer batch logs (CH0 941 counts → 11.35 V ⇒ `vbattScale ≈ 2.468`). For trustworthy amps,
put a DMM in series and adjust `HexaPowerConfig.acsSensitivity`; for trustworthy volts, compare CH0
against a DMM and adjust `vbattScale`. Keep the rails idle at power-up so `begin()` captures a clean
ACS712 zero offset. Run the **"Power Test"** OpMode to stream live V / motor-A / servo-A values.

---

## External I/O & Bench Test ("IO Test")

The board also breaks out **3 digital-I/O headers** and **5 external ADC ports**, named in
`RobotFirmware/lib/HexaNRL/src/BoardPins.h`:

| Name | GPIO / Channel | Header |
|------|----------------|--------|
| `DIGITAL_D1` / `DIGITAL_D2` / `DIGITAL_D3` | IO35 / IO36 / IO37 | J14 / J15 / J16 (digital I/O) |
| `ADC_EXT_1` … `ADC_EXT_5` | MCP3008 CH3 … CH7 | external ADC connectors |

Read an external ADC port with the global `power` object: `power.readChannelVoltage(ADC_EXT_1)`.

The **"IO Test"** OpMode exercises all of these:
- **Digital:** drives `D1/D2/D3` as outputs, blinking together at ~1 Hz — plug an LED (+resistor)
  into each header and watch them blink in unison.
- **Analog:** ramps a PWM signal 0→100→0 % on `DIGITAL_1` (IO1) and streams `adc3…adc7` + `pwm%`
  to telemetry. Jumper `DIGITAL_1` into an ADC connector and that channel tracks the ramp.

> ⚠️ The ESP32-S3 has **no DAC**, so the ramp is a real PWM square wave. For the ADC to read a
> *smooth* voltage, low-pass it: `DIGITAL_1 ──[~10 kΩ]──┬── ADC connector`, with `[1 µF]` from that
> node to GND. An LED (+resistor) on `DIGITAL_1` dims visibly as the ramp falls.

---

## Software Required

1. [VSCode](https://code.visualstudio.com/)
2. [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) for VSCode

That's it. PlatformIO automatically downloads all library dependencies on first build.

---

## Getting Started

Every team project starts from the **project wizard**. It stamps your team number into the
project (which picks your radio channel), writes your `<ProjectName>.code-workspace`, and opens
it. Don't open or build the downloaded kit folder directly; run the wizard first.

**1. Get the kit.** Download the latest `.zip` from [Releases](../../releases) and extract it,
or click **"Use this template"** on GitHub and clone your copy.

**2. Run the project wizard** (nothing needs to be open first):
- Windows: double-click **`tools\new-nrl-project.bat`**
- macOS: double-click **`tools/new-nrl-project.command`**
- Linux: run **`bash tools/new-nrl-project.sh`**

It asks for **project name**, **team number**, **team name** and **location**, then creates the
project.

**3. Start coding.** Your new project opens in VS Code. Create OpModes with **`NRL: New OpMode`**
(see *Writing Your Code* below) and Build/Upload. PlatformIO installs dependencies on the first
build.

Non-interactive use:
```
py -3 tools/nrl_new_project.py --name MyBot --team 7539 --dir C:\Projects --yes
```
Requires **Python 3** (PlatformIO already ships one; on Windows the `py` launcher is used).
See [tools/README.md](tools/README.md) for all options.

#### What the team number does
NRL has no roboRIO-style deploy target, so the team number instead selects the **ESP-NOW radio
channel**, round-robin over the non-overlapping channels **1, 6 and 11** (team 1 → 1, team 2 → 6,
team 3 → 11, team 4 → 1, …). The robot firmware generated for a project is pinned to that channel
via `-DNRL_WIFI_CHANNEL`, which spreads kits out when several run in one room. **Set the
controller's WiFi Channel screen to the same channel.** (Channel is a soft RF split — the button +
4-digit pairing still keeps kits logically separate.) At a competition, every robot on one field
must share that field's channel instead; set it with `py -3 tools/set-competition-channel.py`.

> **Maintainers:** this source repo is opened with `NRL_Update_1.code-workspace` at its root.
> The student kit doesn't ship that file; students always start from the wizard.

---

## Flashing the Firmware

### Robot (Bot)
1. Open the `RobotFirmware` folder in VSCode
2. Connect the Robot ESP32-S3 via USB
3. Click **Upload** in PlatformIO (or `Ctrl+Alt+U`)

### Controller (Handheld)
The controller ships as a **prebuilt binary** — there is nothing to compile.
1. Connect the Controller ESP32-S3 via USB (use a **data** cable, not charge-only)
2. **Windows:** double-click `ControllerFirmware\flash-controller.bat`
   **macOS / Linux:** run `bash ControllerFirmware/flash-controller.sh`
   (or in VS Code: `Ctrl+Shift+P` → `Tasks: Run Task` → **`NRL: Flash Controller`**)
3. Everything else is automatic: Python and esptool are installed if missing, and
   the controller's serial port is detected by its USB chip (phantom system COM
   ports like Intel AMT "SOL" are skipped). If several candidate ports exist,
   you'll be asked to pick from a short list.
4. Afterwards, pick your team's channel on the controller's **WiFi Channel** screen

Both devices communicate wirelessly via **ESP-NOW** — no WiFi router needed.

---

## Troubleshooting

**PlatformIO: `UnknownPlatform ... espressif32 ... version=None`**
This means the local `espressif32` platform package is corrupted or half-installed
(usually from an interrupted download), not a problem with this repo. Fix it with:
```
pio platform uninstall espressif32
pio platform install espressif32
```
If that doesn't help, delete `~/.platformio/platforms/espressif32` (Windows:
`%USERPROFILE%\.platformio\platforms\espressif32`) and rebuild — PlatformIO
re-fetches it automatically.

**`new-nrl-project.bat` / `new-nrl-opmode.bat` say Python wasn't found, or open the
Microsoft Store**
A fresh Windows install has no real Python, so `python`/`py` resolve to the Store's
placeholder stub. Just re-run the launcher — it calls `tools/ensure-python.ps1`,
which detects this and installs a real Python 3 automatically (via winget or the
official python.org installer), then continues. This needs internet access on
first run only.

**`flash_controller.py` says `No module named esptool`**
It now installs esptool automatically into whichever Python runs it the first
time you flash — just re-run it. If that fails (no internet, or pip unavailable
for that interpreter), install it manually with `pip install esptool` and try again.

**Controller flash fails with `Failed to connect to ESP32-S3: No serial data received`**
Old versions of the flasher let esptool guess the port, and on many school/office
laptops it guessed a phantom system port (Intel AMT "SOL") instead of the
controller. The flasher now picks the port by its USB bridge chip and skips
phantom ports. If it reports **no USB serial device at all**: swap in a known-good
**data** USB cable (many are charge-only), plug directly into the laptop (no
hub/dock), and if the board still never appears in Device Manager under
"Ports (COM & LPT)", install the CP210x driver:
https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers

---

## Writing Your Code

Your robot code lives in **`RobotFirmware/opmodes/`**, which is empty in the student kit
(this source repo keeps maintainer test and sample OpModes there). Each file there is
one OpMode, a program you pick from the Controller menu. Create one with
`Ctrl+Shift+P` → `Tasks: Run Task` → **`NRL: New OpMode`** (or run
`tools\new-nrl-opmode.bat` / `bash tools/new-nrl-opmode.sh`). That writes the skeleton below,
with the `REGISTER_OPMODE` line already in place.

Every OpMode inherits from `NRLOpMode` and overrides only the hooks it needs (all four are
optional):

| Hook | When it runs |
|------|--------------|
| `init()` | Once, when INIT is pressed. `begin()` your hardware here. Keep it short: never block or `delay()`. |
| `start()` | Once, when the match starts, right before the first `loop()`. Queue an AUTO routine here. |
| `loop()` | **100 Hz**. TELEOP runs for 2:30, AUTO for 60 s. |
| `stop()` | Once, on STOP. Stop your motors and servos here. |

A small TeleOp (tank drive plus a gripper on a button):

```cpp
#include "NRL.h"

// Declare hardware at file scope. Pin names come from BoardPins.h.
static HexaDCMotor leftMotor {{ .dirPin = MOTOR_L_DIR, .pwmPin = MOTOR_L_PWM }};
static HexaDCMotor rightMotor{{ .dirPin = MOTOR_R_DIR, .pwmPin = MOTOR_R_PWM, .flipped = true }};
static HexaServo   gripper   {{ .signalPin = SERVO_1, .startAngle = 15.0f }};
static TankDrive   drive(leftMotor, rightMotor);

class MyTeleOp : public NRLOpMode {
public:
    void init() override {
        gripper.begin();      // servos first, then motors
        leftMotor.begin();
        rightMotor.begin();
    }

    void loop() override {
        drive.drive(gamepad1.leftY(), gamepad1.rightX());   // forward/back + turn

        if (gamepad1.justPressed(BTN_X)) gripper.setPosition(15);   // open
        if (gamepad1.justPressed(BTN_A)) gripper.setPosition(90);   // close

        telemetry.addData("left Y", gamepad1.leftY());
    }

    void stop() override {
        drive.stop();
        gripper.detach();
    }
};

REGISTER_OPMODE(MyTeleOp, "My TeleOp", TELEOP);
```

### Registering an OpMode

The `REGISTER_OPMODE(ClassName, "Display Name", TELEOP);` line (or `AUTO`) at the bottom of
the file is what puts the OpMode on the Controller menu. Without it, the code compiles but
never appears.

### Rules of thumb

- **No `delay()`.** Use actions (`runAction(sequential({ ... }))`, `sleep_ms()`) for timed
  sequences. In AUTO, queue the routine once in `start()`. In TELEOP, queue it on a button
  press with `justPressed()`, never every tick.
- **Control each part in one place**, either `loop()` or an action, not both.
- **Telemetry:** use `telemetry.addData("key", value)`. It's sent to the Controller automatically.
- **Battery and current:** the global `power` object (see *Power & Current Sensing* above).

`NRLOpMode.h` documents the full lifecycle and the action helpers (`instant`, `sequential`,
`parallel`, `sleep_ms`, `wait_until`, `cancelActions`, `isActionRunning`).
`NRLGamepad.h` lists every button and stick. The board has X, A, Y and the D-pad but **no B
button**.

---

## Hardware Diagnostics OpMode

The **`HW Diagnostics`** TELEOP mode is available from the Controller menu for bench and pit checks:

| Control | Test |
|---------|------|
| `X` | Run the left motor forward for 1 second, then reverse for 1 second. |
| `A` | Run the right motor forward for 1 second, then reverse for 1 second. |
| D-pad `LEFT` / `RIGHT` | Select the active servo: arm, gripper, Cargo Bay, or Vault gate. |
| Hold D-pad `UP` / `DOWN` | Sweep the selected servo between its configured limits and back. |
| `Y` | Play three short buzzer beeps. |
| `LB` | Cycle the user LED through red, green, blue, and off. |

The robot OLED displays ultrasonic distance, external ADC voltage, battery voltage, motor and servo rail current, and IMU pitch, roll, and heading. The same readings and current test status are also sent as Controller telemetry.

The BaseBot construction guide shows four drive motors arranged as left/right sides, while this firmware exposes two production motor channels. The motor tests address those two channels; front and rear motors on each side must be wired to the corresponding channel to test them together. The guide includes the arm and gripper servos. Cargo Bay and Vault gate servos are optional team-built mechanisms, not part of the documented BaseBot assembly. Servo sweeps are limited to the configured RobotCalibration target ranges rather than the servos' full electrical travel. Confirm all moving parts are clear before running tests; ultrasonic and IR readings require those optional sensors to be installed.

## Driver TeleOp and Star Map Setup

The **`Driver TeleOp`** mode provides tank drive (left stick Y / right stick X), hold `LB` for
35% precision speed, `X` to close the gripper and lift to carry, D-pad up/down for High Pole/floor,
`RB` for Apex Pole, `A` to toggle the gripper, `Y` to toggle the Cargo Bay, and `RT` to toggle
the Vault gate. Its telemetry includes battery voltage with a warning below 11 V, motor current,
arm angle, gripper state, and speed mode. Cargo Bay and Vault controls require those optional
mechanisms to be installed.

To select an autonomous Star Map, run **`Star Map Setup`** as TELEOP, use D-pad up/down to select
one of the three patterns, and press `A` to confirm. Stop that mode and then select **`AI Driver:
Red`** or **`AI Driver: Blue`**; the selected pattern is shared with both autonomous modes until
the firmware restarts. The SDK has no repeated pre-start/init hook, so selection cannot be
confirmed before INIT as requested in the task; the setup mode is a separate, explicit step
before starting AUTO. The autonomous pattern routines use the existing route distances as initial
field assumptions; rehearse and tune them on the robot before competition.

---

## Library Overview

| Library | Purpose |
|---------|---------|
| **HexaNRL** | The robot runtime: OpMode lifecycle, match timing, wireless link, gamepad, telemetry, actions, `TankDrive`. Ships precompiled in the student kit. |
| **HexaServos** | `HexaServo` (PWM servos) and `HexaDCMotor` (DC motors) |
| **HexaIMU** | LSM6DSOX 6-DOF IMU driver with non-blocking heading (wraps Adafruit_LSM6DSOX) |
| **HexaOLED** | SSD1306 OLED display driver (I²C) |
| **HexaPower** | MCP3008 ADC: battery voltage and ACS712 rail current (HexaNRL provides the global `power` instance) |
| **HexaLED** | NeoPixel status and user LED |
| **HexaHAL** | Hardware abstraction interfaces |

Third-party dependencies (fetched by PlatformIO on first build): Adafruit NeoPixel,
Adafruit LSM6DS and Adafruit Unified Sensor.

---

## Project Structure

```
RobotFirmware/              ← Flash this to the robot
├── opmodes/                ← YOUR CODE: one .cpp per OpMode
├── src/RobotMain.ino       ← Entry point (do not edit)
├── lib/HexaNRL/            ← Robot runtime (do not edit)
└── boards/                 ← Custom board definition (reference only; platformio.ini
                               still targets the stock esp32-s3-devkitc-1 profile)
ControllerFirmware/         ← Controller firmware (the student kit ships it as a
                               prebuilt image + flasher; nothing to build there)
lib/                        ← Hardware drivers (HexaServos, HexaIMU, HexaOLED, HexaPower, HexaLED, HexaHAL)
tools/                      ← Project and OpMode generators, Blocks editor
```
---

## Git pull and push commands 

git pull origin main

 Commit and push your changes:
 git status
git add <files>
git commit -m "Describe your changes"
git push origin main

To push an existing commit, just run:
git push origin main


---

---

## AI Features & Autonomous Systems

This project includes two advanced AI-driven features designed for high precision and competition excellence:

### 1. Teach & Repeat AI (`TeachAndRepeatOpMode.cpp`)
Registered in the Controller menu as **`Teach & Repeat AI`** (TELEOP).

A demonstration-learning engine that allows a driver to record a routine once and replay it with machine precision and optimized speed.

- **Demonstration Capture:** Records drive sticks, arm elevation, gripper state, and LSM6DSOX gyro heading at 20 Hz (up to 60 seconds).
- **Trajectory Optimization:** Prunes driver hesitation and dead-time from the start and end, accelerates the timeline (default 1.35x, adjustable up to 2.50x), and boosts straightaway speeds.
- **Closed-Loop Gyro Heading Lock:** Replays using active proportional steering against recorded target headings to eliminate carpet drift.
- **Dynamic Voltage Compensation:** Adjusts motor PWM in real time as battery voltage drops to ensure consistent travel distance.
- **Active Collision Avoidance:** During replay, monitors for obstacles/robots using ultrasonic/IR sensors and motor current. If blocked, halts immediately to prevent fouls (Rule G4.8), freezes the playback timeline without losing state, and executes an evasive bypass if blocked for more than 1.5 seconds!

**Controls:**
- `X`: Start / Stop recording (auto-optimizes on stop).
- `Y`: Re-run optimization.
- `A`: Start autonomous replay / Stop replay early.
- `LB`: Toggle gripper (Open 20° / Closed 90°).
- `D-Pad UP / DOWN`: Step arm elevation (+/- 15°).
- `D-Pad RIGHT / LEFT`: Increase / Decrease speedup multiplier.
- `LT`: Emergency abort to manual IDLE.
- Hold `RB + Y` for 1.2 seconds: Save the optimized route to flash.
- Hold `RB + LT` for 1.2 seconds: Clear the saved route and in-memory route.

Teach & Repeat loads a validated saved trajectory during INIT. The OLED reports `SAVED TO FLASH`,
`LOADED FROM FLASH`, or `NO SAVED ROUTE`; Controller telemetry shows the flash state as well.

---

### 2. Autonomous AI Driver with Collision Avoidance (`AIDriver*.cpp`)
Fully autonomous, hands-off self-driving modes tailored specifically to the **NRL 2026 HYPERDRIVE** game arenas:

| Mode on Controller | Source File | Description |
| :--- | :--- | :--- |
| **`AI Driver: Red`** | `AIDriverRedField.cpp` | Autonomous driver for Red Squad (starts in Red Start Zone). |
| **`AI Driver: Blue`** | `AIDriverBlueField.cpp` | Autonomous driver for Blue Squad (starts in Blue Start Zone). |
| **`AI Driver: Skills`** | `AIDriverSkillsChallenge.cpp` | High-yield autonomous cycle for the solo Autonomous Skill Challenge. |

#### Complete Match Gameplay Cycle
1. **Spike Mark & High Pole Scoring:** Drives to pre-placed Spike Marks, intakes Core, and stacks onto the High Pole (130 mm height, **+3 pts**).
2. **Payload Delivery (Rule R9.4):** Retrieves custom team Payload from Source Zone with the independent Cargo Bay mechanism (`SERVO_3`) and scores into Drop Zone (**+10 pts** + reduces Starburst activation threshold from 10 Cores down to 4 Cores).
3. **Vault Access & Star Map:** Approaches Squad Vault (`SERVO_4`), retrieves Golden Core, and scores onto Star Pole according to the Star Map (**+7 pts** + Star Map Bonus).
4. **Space Station Auto-Balance (Rule 3.3.3):** Navigates up the Space Station ramp and runs an active closed-loop balance loop reading accelerometer pitch to level the platform within +/- 2.5 degrees for a **15-point BALANCE**!
5. **Apex Pole Finish (Rule 3.3.4):** Extends arm to the Apex Pole (365 mm) for the final high-value score (**+9 pts**).

#### Real-Time Collision Avoidance & Dynamic Rerouting
Equipped with multi-modal detection via `ObstacleDetector` in `AIDriverCommon.h`:
- **Sensors:** Ultrasonic on `DIGITAL_D1`/`D2` (GPIO 35/36), Sharp IR on `ADC_EXT_1`, motor current stall sensing, and IMU deceleration.
- **Dynamic Rerouting (`smartDriveInches`):**
  - **Immediate Evasive Brake:** Halts within 30 cm of opposing robots to prevent rule G4.8 penalties.
  - **Crossing Detection:** Waits 500 ms for crossing robots to clear before resuming.
  - **Autonomous Bypass:** If permanently blocked, turns out by 35°, drives 14" past the obstacle envelope, counter-turns back to original heading, and finishes the task!

---

## License

MIT — free to use, modify, and distribute for educational and competition purposes.
