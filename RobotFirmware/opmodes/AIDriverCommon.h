#pragma once
#include "NRL.h"
#include <cmath>
#include <memory>

// ============================================================================
//  NRL 2026 HYPERDRIVE — AI Driver Common Navigation & Collision Avoidance
// ============================================================================
//  Includes:
//    - Multi-modal Obstacle Detection (Ultrasonic, IR, Current Stall, IMU Impact)
//    - Smart Self-Rerouting Navigation (Dynamic bypass of robots/obstacles)
//    - Space Station Active Closed-Loop Auto-Balancing (+15 pts)
//    - Mechanism presets for Core Gripper, Arm, Cargo Bay, and Vault
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

// ============================================================================
//  Obstacle Detector Subsystem
// ============================================================================
class ObstacleDetector {
public:
    uint8_t trigPin = DIGITAL_D1;  // GPIO 35 - Ultrasonic Trigger
    uint8_t echoPin = DIGITAL_D2;  // GPIO 36 - Ultrasonic Echo
    uint8_t irAdcCh = ADC_EXT_1;   // MCP3008 CH3 - Analog Sharp IR sensor
    float   obstacleThresholdCm = 30.0f; // Detection radius (~12 inches)
    float   stallCurrentThresholdA = 2.85f; // Current spike indicating physical contact

    void begin() {
        pinMode(trigPin, OUTPUT);
        digitalWrite(trigPin, LOW);
        pinMode(echoPin, INPUT);
    }

    float getUltrasonicDistanceCm() const {
        digitalWrite(trigPin, LOW);
        delayMicroseconds(2);
        digitalWrite(trigPin, HIGH);
        delayMicroseconds(10);
        digitalWrite(trigPin, LOW);
        long duration = pulseIn(echoPin, HIGH, 16000); // 16ms cutoff (~2.7m)
        if (duration <= 0) return 999.0f;
        return (duration * 0.0343f) / 2.0f;
    }

    float getIrDistanceCm() const {
        float v = power.readChannelVoltage(irAdcCh);
        if (v < 0.40f) return 999.0f;
        return 27.0f / v; // standard 10-80cm Sharp curve
    }

    float getFrontDistanceCm() const {
        float us = getUltrasonicDistanceCm();
        if (us > 2.0f && us < 150.0f) return us;
        return getIrDistanceCm();
    }

    bool isObstacleAhead() const {
        // 1. Distance sensor proximity (< 30 cm)
        float d = getFrontDistanceCm();
        if (d > 2.5f && d <= obstacleThresholdCm) {
            return true;
        }

        // 2. Motor stall current spike (contact detection)
        if (power.getMotorCurrent() > stallCurrentThresholdA) {
            return true;
        }

        // 3. Accelerometer sudden deceleration/jolt (> 18 m/s^2)
        if (imu.read() && fabs(imu.getAccelY()) > 18.0f) {
            return true;
        }

        return false;
    }
};

static ObstacleDetector obstacleDetector;

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
        return instant([] { arm.setPosition(90.0f); });
    }

    inline ActionPtr armApexPole() {
        return instant([] { arm.setPosition(160.0f); });
    }

    inline ActionPtr deployPayload() {
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

    // ── Smart Self-Rerouting Drive Action (Obstacle Avoidance) ─────────────────
    // Replaces blind straight-line driving with an intelligent driver that:
    //   1. Monitors path continuously for other robots or obstacles.
    //   2. If a robot is detected, halts immediately to prevent collision fouls (G4.8).
    //   3. Pauses briefly (500ms) to allow crossing robots to pass.
    //   4. If path remains blocked, automatically calculates an evasive bypass
    //      (turns by evadeAngle, bypasses obstacle, counter-turns, and resumes).
    //   5. Arrives reliably at the target destination without colliding.
    class SmartDriveAction : public Action {
    private:
        float _targetInches;
        float _evadeAngle;
        float _msPerInch = 18.0f; // Approx timing calibration at 0.75 drive power
        float _coveredInches = 0.0f;

        enum Stage {
            STAGE_DRIVE_TARGET,
            STAGE_WAIT_CROSSING,
            STAGE_EVADE_TURN_OUT,
            STAGE_EVADE_PASS,
            STAGE_EVADE_TURN_BACK,
            STAGE_COMPLETE
        };

        Stage    _stage = STAGE_DRIVE_TARGET;
        uint32_t _stageStartMs = 0;
        uint32_t _lastTickMs = 0;
        float    _baseHeading = 0.0f;
        uint8_t  _avoidanceAttempts = 0;

    public:
        SmartDriveAction(float inches, float evadeAngle = 35.0f)
            : _targetInches(inches), _evadeAngle(evadeAngle) {}

        bool run() override {
            uint32_t now = millis();

            if (_stageStartMs == 0) {
                _stageStartMs = now;
                _lastTickMs = now;
                _baseHeading = NRLComms::getHeading();
                _stage = STAGE_DRIVE_TARGET;
            }

            uint32_t dt = now - _lastTickMs;
            _lastTickMs = now;

            switch (_stage) {
                case STAGE_DRIVE_TARGET: {
                    // Check if another robot or obstacle is blocking the path
                    if (obstacleDetector.isObstacleAhead()) {
                        drive.stop();
                        userLed.setBlink(255, 120, 0, 150); // Flash Orange: Obstacle detected!
                        _stage = STAGE_WAIT_CROSSING;
                        _stageStartMs = now;
                        break;
                    }

                    // Gyro-stabilized straight drive
                    float currentHeading = NRLComms::getHeading();
                    float headingErr = _baseHeading - currentHeading;
                    while (headingErr > 180.0f)  headingErr -= 360.0f;
                    while (headingErr < -180.0f) headingErr += 360.0f;
                    float turnCorrection = constrain(headingErr * 0.025f, -0.4f, 0.4f);

                    float forwardDir = (_targetInches >= 0.0f) ? 0.75f : -0.75f;
                    drive.drive(forwardDir, turnCorrection);

                    _coveredInches += (float)dt / _msPerInch;

                    if (_coveredInches >= fabs(_targetInches)) {
                        drive.stop();
                        _stage = STAGE_COMPLETE;
                        return true;
                    }
                    break;
                }

                case STAGE_WAIT_CROSSING: {
                    // Wait up to 500ms to see if the robot moves out of the way
                    if (!obstacleDetector.isObstacleAhead()) {
                        // Path cleared! Resume straight
                        _stage = STAGE_DRIVE_TARGET;
                        userLed.setSolid(0, 180, 255);
                        break;
                    }

                    if (now - _stageStartMs > 500) {
                        // Still blocked -> Initiate dynamic reroute bypass
                        _avoidanceAttempts++;
                        _stage = STAGE_EVADE_TURN_OUT;
                        _stageStartMs = now;
                    }
                    break;
                }

                case STAGE_EVADE_TURN_OUT: {
                    // Turn out by evade angle to skirt around the obstacle
                    float targetAngle = _baseHeading + _evadeAngle;
                    float headingErr = targetAngle - NRLComms::getHeading();
                    while (headingErr > 180.0f)  headingErr -= 360.0f;
                    while (headingErr < -180.0f) headingErr += 360.0f;

                    if (fabs(headingErr) < 3.0f || (now - _stageStartMs > 800)) {
                        drive.stop();
                        _stage = STAGE_EVADE_PASS;
                        _stageStartMs = now;
                    } else {
                        float turnPwr = constrain(headingErr * 0.03f, -0.6f, 0.6f);
                        drive.drive(0.0f, turnPwr);
                    }
                    break;
                }

                case STAGE_EVADE_PASS: {
                    // Drive 14 inches past the obstacle envelope
                    drive.drive(0.65f, 0.0f);
                    if (now - _stageStartMs > (uint32_t)(14.0f * _msPerInch * 1.15f)) {
                        drive.stop();
                        _stage = STAGE_EVADE_TURN_BACK;
                        _stageStartMs = now;
                    }
                    break;
                }

                case STAGE_EVADE_TURN_BACK: {
                    // Counter-turn back toward the original path heading
                    float headingErr = _baseHeading - NRLComms::getHeading();
                    while (headingErr > 180.0f)  headingErr -= 360.0f;
                    while (headingErr < -180.0f) headingErr += 360.0f;

                    if (fabs(headingErr) < 3.0f || (now - _stageStartMs > 800)) {
                        drive.stop();
                        // Resume straight drive for the remainder
                        _stage = STAGE_DRIVE_TARGET;
                        userLed.setSolid(0, 180, 255);
                    } else {
                        float turnPwr = constrain(headingErr * 0.03f, -0.6f, 0.6f);
                        drive.drive(0.0f, turnPwr);
                    }
                    break;
                }

                case STAGE_COMPLETE:
                    drive.stop();
                    return true;
            }

            return false;
        }

        void reset() override {
            _stage = STAGE_DRIVE_TARGET;
            _stageStartMs = 0;
            _coveredInches = 0.0f;
        }
    };

    inline ActionPtr smartDriveInches(float inches, float evadeAngle = 35.0f) {
        return std::make_shared<SmartDriveAction>(inches, evadeAngle);
    }

    // ── Space Station Autonomous Active Balancer ──────────────────────────────
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
                userLed.setBlink(0, 0, 255, 200);
            }

            if (now - _startMs >= _durationMs) {
                drive.stop();
                userLed.setSolid(0, 255, 0);
                return true;
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
        obstacleDetector.begin();

        drive.setRampRate(0.18f);
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
