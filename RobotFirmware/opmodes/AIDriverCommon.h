#pragma once
#include "NRL.h"
#include <cmath>
#include <memory>

// ============================================================================
//  NRL 2026 HYPERDRIVE — AI Driver Common Navigation & Mechanism Controller
// ============================================================================
//  Shared motion primitives and mechanism presets for fully autonomous
//  self-driving on the 12x12ft HYPERDRIVE arena.
// ============================================================================

// Hardware declarations
static HexaDCMotor leftMotor  {{ .dirPin = MOTOR_L_DIR, .pwmPin = MOTOR_L_PWM }};
static HexaDCMotor rightMotor {{ .dirPin = MOTOR_R_DIR, .pwmPin = MOTOR_R_PWM, .flipped = true }};
static HexaServo   arm        {{ .signalPin = SERVO_1,  .startAngle = 10.0f }};  // Arm elevation
static HexaServo   gripper    {{ .signalPin = SERVO_2,  .startAngle = 20.0f }};  // Core clamp
static HexaServo   cargoBay   {{ .signalPin = SERVO_3,  .startAngle = 0.0f  }};  // Payload mechanism (R9.4)
static HexaServo   vaultGate  {{ .signalPin = SERVO_4,  .startAngle = 0.0f  }};  // Vault intake gate

static TankDrive    drive(leftMotor, rightMotor);
static DriveActions driveActions(drive);
static HexaIMU      imu(hexaImuConfig());

namespace AIDriver {

    // ── Mechanism Action Primitives ───────────────────────────────────────────
    inline ActionPtr openGripper() {
        return instant([] { gripper.setPosition(20.0f); });
    }

    inline ActionPtr closeGripper() {
        return instant([] { gripper.setPosition(95.0f); });
    }

    inline ActionPtr armFloor() {
        return instant([] { arm.setPosition(10.0f); });
    }

    inline ActionPtr armCarry() {
        return instant([] { arm.setPosition(35.0f); });
    }

    inline ActionPtr armHighPole() {
        // High Pole holds up to 4 Cores, 130mm height
        return instant([] { arm.setPosition(90.0f); });
    }

    inline ActionPtr armApexPole() {
        // Apex Pole is the highest scoring element on the Space Station (365mm height)
        return instant([] { arm.setPosition(160.0f); });
    }

    inline ActionPtr deployPayload() {
        // Cargo Bay opens to release custom Payload in Drop Zone (R9.4)
        return sequential({
            instant([] { cargoBay.setPosition(90.0f); }),
            sleep_ms(600),
            instant([] { cargoBay.setPosition(0.0f);  })
        });
    }

    inline ActionPtr openVaultGate() {
        return instant([] { vaultGate.setPosition(90.0f); });
    }

    inline ActionPtr closeVaultGate() {
        return instant([] { vaultGate.setPosition(0.0f); });
    }

    // ── Space Station Autonomous Active Balancer ──────────────────────────────
    // The Space Station is a tilting platform. Balance scores 15 points!
    // Reads pitch angle from LSM6DSOX accelerometer and nudges robot forward/back
    // until platform is level (pitch within +/- 2.5 degrees).
    class AutoBalanceAction : public Action {
    private:
        uint32_t _startMs = 0;
        uint32_t _durationMs;
        bool     _started = false;

    public:
        explicit AutoBalanceAction(uint32_t durationMs = 6000)
            : _durationMs(durationMs) {}

        bool run() override {
            uint32_t now = millis();
            if (!_started) {
                _startMs = now;
                _started = true;
                drive.stop();
                userLed.setBlink(0, 0, 255, 200); // Fast blue blink: auto-balancing
            }

            if (now - _startMs >= _durationMs) {
                drive.stop();
                userLed.setSolid(0, 255, 0); // Solid green: balanced!
                return true; // complete
            }

            if (imu.read()) {
                float ax = imu.getAccelX();
                float ay = imu.getAccelY();
                float az = imu.getAccelZ();
                float pitchDeg = atan2(-ax, sqrt(ay * ay + az * az)) * (180.0f / 3.14159f);

                if (pitchDeg > 2.5f) {
                    drive.drive(0.22f, 0.0f);  // creep forward
                } else if (pitchDeg < -2.5f) {
                    drive.drive(-0.22f, 0.0f); // creep backward
                } else {
                    drive.stop();             // level, hold position
                }
            }
            return false;
        }

        void reset() override {
            _started = false;
        }
    };

    inline ActionPtr autoBalanceSpaceStation(uint32_t durationMs = 6000) {
        return std::make_shared<AutoBalanceAction>(durationMs);
    }

    // ── Hardware Initialization ───────────────────────────────────────────────
    inline void initAllHardware() {
        arm.begin();
        gripper.begin();
        cargoBay.begin();
        vaultGate.begin();
        leftMotor.begin();
        rightMotor.begin();
        imu.begin();

        drive.setRampRate(0.18f); // Snappy, responsive acceleration
        userLed.setSolid(0, 150, 255);
    }

    inline void stopAllHardware() {
        drive.stop();
        arm.detach();
        gripper.detach();
        cargoBay.detach();
        vaultGate.detach();
        userLed.setOff();
    }
}
