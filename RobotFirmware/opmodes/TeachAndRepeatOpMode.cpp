#include "NRL.h"

// ============================================================================
//  Teach & Repeat AI OpMode (Demonstration Learning & Trajectory Optimization)
// ============================================================================
//  Hardware:
//    - Left DC Motor:  DIR = MOTOR_L_DIR, PWM = MOTOR_L_PWM
//    - Right DC Motor: DIR = MOTOR_R_DIR, PWM = MOTOR_R_PWM (flipped)
//    - Arm Servo:      Signal = SERVO_1 (startAngle = 90 deg)
//    - Gripper Servo:  Signal = SERVO_2 (startAngle = 20 deg)
//    - IMU (Heading):  On-board LSM6DSOX (NRLComms::getHeading())
//    - User RGB LED:   userLed
//
//  Controls:
//    - Sticks:             Drive manually (Left Y = fwd/rev, Right X = turn)
//    - BTN_X:              TOGGLE RECORDING (Capture driver movements)
//    - BTN_Y:              RUN AI OPTIMIZATION (Prune dead-time, accelerate trajectory)
//    - BTN_A:              TOGGLE PLAYBACK (Precision replay with Gyro Heading Lock)
//    - BTN_LB:             Toggle Gripper (Open 20 deg / Closed 90 deg)
//    - BTN_DPAD_UP/DOWN:   Step Arm Up / Down (+/- 15 deg)
//    - BTN_DPAD_RIGHT:     Increase Speedup Multiplier (1.0x -> 1.25x -> 1.5x -> 2.0x)
//    - BTN_DPAD_LEFT:      Decrease Speedup Multiplier
//    - BTN_LT:             Emergency Abort / Reset to Idle
// ============================================================================

// Declare hardware at file scope
static HexaDCMotor leftMotor  {{ .dirPin = MOTOR_L_DIR, .pwmPin = MOTOR_L_PWM }};
static HexaDCMotor rightMotor {{ .dirPin = MOTOR_R_DIR, .pwmPin = MOTOR_R_PWM, .flipped = true }};
static HexaServo   arm        {{ .signalPin = SERVO_1,  .startAngle = 90.0f }};
static HexaServo   gripper    {{ .signalPin = SERVO_2,  .startAngle = 20.0f }};
static TankDrive   drive(leftMotor, rightMotor);

// ── Trajectory Point Data Structure ──────────────────────────────────────────
struct TrajectoryPoint {
    float    forward;       // commanded forward velocity (-1.0 to 1.0)
    float    turn;          // commanded turn velocity (-1.0 to 1.0)
    float    armAngle;      // arm angle in degrees (0 to 180)
    float    gripperAngle;  // gripper angle in degrees (0 to 180)
    float    headingDeg;    // target compass heading from IMU
    uint32_t timeMs;        // millisecond timestamp relative to routine start
};

// Maximum recorded trajectory buffer (1200 points @ 20Hz = 60 seconds)
static constexpr size_t MAX_POINTS = 1200;
static constexpr uint32_t SAMPLE_INTERVAL_MS = 50; // 20 Hz capture rate

class TeachAndRepeatOpMode : public NRLOpMode {
private:
    enum State {
        STATE_MANUAL_IDLE,  // Free driving, not recording
        STATE_RECORDING,    // Recording demonstration
        STATE_OPTIMIZED,    // Processed & ready for high-speed replay
        STATE_REPLAYING     // Autonomous closed-loop replay
    };

    State _state = STATE_MANUAL_IDLE;

    TrajectoryPoint _rawTrajectory[MAX_POINTS];
    TrajectoryPoint _optTrajectory[MAX_POINTS];
    size_t _rawCount = 0;
    size_t _optCount = 0;

    uint32_t _recordStartMs = 0;
    uint32_t _lastSampleMs = 0;
    uint32_t _replayStartMs = 0;
    size_t   _replayIdx = 0;

    // Mechanism state
    float _targetArm = 90.0f;
    float _targetGripper = 20.0f;
    bool  _gripperClosed = false;

    // AI Speedup & Tuning Knobs
    float _speedupMultiplier = 1.35f; // Default 35% faster execution
    float _gyroKp = 0.025f;          // Heading lock proportional correction gain
    float _nominalVoltage = 12.0f;   // Reference battery voltage for normalization

public:
    void init() override {
        // Initialize hardware
        arm.begin();
        gripper.begin();
        leftMotor.begin();
        rightMotor.begin();

        _state = STATE_MANUAL_IDLE;
        _rawCount = 0;
        _optCount = 0;

        userLed.setSolid(0, 120, 120); // Cyan: Idle Ready
    }

    void loop() override {
        uint32_t now = millis();

        // ── Emergency Abort ───────────────────────────────────────────────
        if (gamepad1.justPressed(BTN_LT)) {
            abortToIdle();
            return;
        }

        // ── Adjust Speedup Multiplier with D-pad ──────────────────────────
        if (gamepad1.justPressed(BTN_DPAD_RIGHT)) {
            _speedupMultiplier += 0.15f;
            if (_speedupMultiplier > 2.50f) _speedupMultiplier = 2.50f;
            if (_rawCount > 0) runOptimization(); // re-optimize with new speed
        }
        if (gamepad1.justPressed(BTN_DPAD_LEFT)) {
            _speedupMultiplier -= 0.15f;
            if (_speedupMultiplier < 1.00f) _speedupMultiplier = 1.00f;
            if (_rawCount > 0) runOptimization();
        }

        // ── Manual Servo Controls ─────────────────────────────────────────
        if (_state != STATE_REPLAYING) {
            // Gripper toggle
            if (gamepad1.justPressed(BTN_LB)) {
                _gripperClosed = !_gripperClosed;
                _targetGripper = _gripperClosed ? 90.0f : 20.0f;
                gripper.setPosition(_targetGripper);
            }

            // Arm step up / down
            if (gamepad1.justPressed(BTN_DPAD_UP)) {
                _targetArm = constrain(_targetArm + 15.0f, 0.0f, 180.0f);
                arm.setPosition(_targetArm);
            }
            if (gamepad1.justPressed(BTN_DPAD_DOWN)) {
                _targetArm = constrain(_targetArm - 15.0f, 0.0f, 180.0f);
                arm.setPosition(_targetArm);
            }
        }

        // ── State Machine ─────────────────────────────────────────────────
        switch (_state) {
            case STATE_MANUAL_IDLE:
            case STATE_OPTIMIZED:
                handleManualDrive();

                // Button X: Start Recording
                if (gamepad1.justPressed(BTN_X)) {
                    startRecording(now);
                }
                // Button Y: Optimize (if we have recorded points)
                else if (gamepad1.justPressed(BTN_Y) && _rawCount > 0) {
                    runOptimization();
                }
                // Button A: Start Playback (if optimized)
                else if (gamepad1.justPressed(BTN_A) && _optCount > 0) {
                    startReplay(now);
                }
                break;

            case STATE_RECORDING:
                handleManualDrive();
                recordSample(now);

                // Button X: Stop Recording and auto-optimize
                if (gamepad1.justPressed(BTN_X)) {
                    stopRecording();
                    runOptimization();
                }
                break;

            case STATE_REPLAYING:
                // Button A: Stop Replay early
                if (gamepad1.justPressed(BTN_A)) {
                    abortToIdle();
                } else {
                    handleReplayTick(now);
                }
                break;
        }

        // ── Telemetry (Live on Controller LCD) ─────────────────────────────
        sendTelemetryStatus();
    }

    void stop() override {
        drive.stop();
        arm.detach();
        gripper.detach();
        userLed.setOff();
    }

private:
    void handleManualDrive() {
        drive.drive(gamepad1.leftY(), gamepad1.rightX());
    }

    // ── Phase 1: Recording ────────────────────────────────────────────────
    void startRecording(uint32_t now) {
        _rawCount = 0;
        _optCount = 0;
        _recordStartMs = now;
        _lastSampleMs = now;
        _state = STATE_RECORDING;

        // Reset IMU heading zero-reference so record starts at 0 deg
        NRLComms::zeroHeading();

        userLed.setBlink(255, 0, 0, 300); // Blinking Red: Recording
    }

    void recordSample(uint32_t now) {
        if (now - _lastSampleMs < SAMPLE_INTERVAL_MS) return;
        _lastSampleMs = now;

        if (_rawCount < MAX_POINTS) {
            _rawTrajectory[_rawCount] = TrajectoryPoint{
                .forward      = gamepad1.leftY(),
                .turn         = gamepad1.rightX(),
                .armAngle     = _targetArm,
                .gripperAngle = _targetGripper,
                .headingDeg   = NRLComms::getHeading(),
                .timeMs       = now - _recordStartMs
            };
            _rawCount++;
        } else {
            // Buffer full, auto stop
            stopRecording();
            runOptimization();
        }
    }

    void stopRecording() {
        drive.stop();
        _state = STATE_MANUAL_IDLE;
        userLed.setSolid(0, 120, 120);
    }

    // ── Phase 2: AI Trajectory Optimization & Time-Warping ────────────────
    void runOptimization() {
        if (_rawCount == 0) return;

        // Step A: Find leading and trailing dead-time (human hesitation)
        size_t startIdx = 0;
        while (startIdx < _rawCount &&
               fabs(_rawTrajectory[startIdx].forward) < 0.05f &&
               fabs(_rawTrajectory[startIdx].turn) < 0.05f) {
            startIdx++;
        }
        if (startIdx >= _rawCount) startIdx = 0; // fallback if entirely still

        size_t endIdx = _rawCount - 1;
        while (endIdx > startIdx &&
               fabs(_rawTrajectory[endIdx].forward) < 0.05f &&
               fabs(_rawTrajectory[endIdx].turn) < 0.05f) {
            endIdx--;
        }

        // Step B: Time compression with speedupMultiplier
        _optCount = 0;
        uint32_t baseTime = _rawTrajectory[startIdx].timeMs;

        for (size_t i = startIdx; i <= endIdx; i++) {
            if (_optCount >= MAX_POINTS) break;

            TrajectoryPoint p = _rawTrajectory[i];
            uint32_t relativeTime = p.timeMs - baseTime;

            // Compress time
            p.timeMs = (uint32_t)((float)relativeTime / _speedupMultiplier);

            // Boost straightaways where human drove cautiously (up to 20% boost, capped at 1.0)
            if (fabs(p.turn) < 0.15f && fabs(p.forward) > 0.2f) {
                p.forward = constrain(p.forward * 1.15f, -1.0f, 1.0f);
            }

            _optTrajectory[_optCount++] = p;
        }

        _state = STATE_OPTIMIZED;
        userLed.setSolid(255, 140, 0); // Solid Amber: Optimized & Ready
    }

    // ── Phase 3: Closed-Loop Replay with Gyro Heading Lock ─────────────────
    void startReplay(uint32_t now) {
        if (_optCount == 0) return;

        _replayStartMs = now;
        _replayIdx = 0;
        _state = STATE_REPLAYING;

        // Zero heading at start of playback to align with recorded zero
        NRLComms::zeroHeading();

        userLed.setSolid(160, 0, 255); // Purple: Precision Replay
    }

    void handleReplayTick(uint32_t now) {
        uint32_t elapsed = now - _replayStartMs;

        // Advance to the current keyframe for this point in time
        while (_replayIdx + 1 < _optCount && _optTrajectory[_replayIdx + 1].timeMs <= elapsed) {
            _replayIdx++;
        }

        // Finished replaying entire routine
        if (_replayIdx >= _optCount - 1 && elapsed >= _optTrajectory[_optCount - 1].timeMs) {
            abortToIdle();
            return;
        }

        const TrajectoryPoint& target = _optTrajectory[_replayIdx];

        // 1. Closed-Loop Gyro Heading Lock
        float currentHeading = NRLComms::getHeading();
        float headingError = target.headingDeg - currentHeading;

        // Normalize heading error into [-180, +180]
        while (headingError > 180.0f)  headingError -= 360.0f;
        while (headingError < -180.0f) headingError += 360.0f;

        // Proportional steering correction added to recorded turn
        float steerCorrection = headingError * _gyroKp;
        float finalTurn = constrain(target.turn + steerCorrection, -1.0f, 1.0f);

        // 2. Dynamic Battery Voltage Compensation
        float vCurrent = power.getBatteryVoltage();
        float vScale = 1.0f;
        if (vCurrent >= 8.0f && vCurrent <= 14.5f) {
            vScale = _nominalVoltage / vCurrent; // e.g. 12.0 / 11.1 = 1.08x PWM boost
        }
        float finalForward = constrain(target.forward * vScale, -1.0f, 1.0f);

        // 3. Actuate Motors & Servos
        drive.drive(finalForward, finalTurn);
        arm.setPosition(target.armAngle);
        gripper.setPosition(target.gripperAngle);
    }

    void abortToIdle() {
        drive.stop();
        _state = (_optCount > 0) ? STATE_OPTIMIZED : STATE_MANUAL_IDLE;
        if (_state == STATE_OPTIMIZED) {
            userLed.setSolid(255, 140, 0); // Amber: Ready to replay again
        } else {
            userLed.setSolid(0, 120, 120); // Cyan: Idle
        }
    }

    void sendTelemetryStatus() {
        const char* stateStr = "IDLE";
        if (_state == STATE_RECORDING) stateStr = "REC...";
        else if (_state == STATE_OPTIMIZED) stateStr = "OPTIMIZED";
        else if (_state == STATE_REPLAYING) stateStr = "REPLAYING";

        telemetry.addData("AI Mode", stateStr);
        telemetry.addData("Speedup", _speedupMultiplier);
        telemetry.addData("Points", (int)(_state == STATE_REPLAYING ? _optCount : _rawCount));
        telemetry.addData("Heading", NRLComms::getHeading());
        telemetry.addData("Batt(V)", power.getBatteryVoltage());
    }
};

// Register this OpMode to appear on the handheld Controller menu
REGISTER_OPMODE(TeachAndRepeatOpMode, "Teach & Repeat AI", TELEOP);
