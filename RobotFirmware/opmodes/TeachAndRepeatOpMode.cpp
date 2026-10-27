#include "NRL.h"
#include <Preferences.h>
#include <cmath>
#include <cstring>

// ============================================================================
//  Teach & Repeat AI OpMode (With Active Obstacle Avoidance & Rerouting)
// ============================================================================
//  Hardware:
//    - Left DC Motor:  DIR = MOTOR_L_DIR, PWM = MOTOR_L_PWM
//    - Right DC Motor: DIR = MOTOR_R_DIR, PWM = MOTOR_R_PWM (flipped)
//    - Arm Servo:      Signal = SERVO_1 (startAngle = 90 deg)
//    - Gripper Servo:  Signal = SERVO_2 (startAngle = 20 deg)
//    - IMU (Heading):  On-board LSM6DSOX (NRLComms::getHeading())
//    - User RGB LED:   userLed
//    - Distance Sensors: DIGITAL_D1 (Trig), DIGITAL_D2 (Echo), ADC_EXT_1 (IR)
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
//
//  NEW FEATURE: Real-Time Robot & Obstacle Collision Avoidance.
//  During autonomous replay, if another robot or obstacle enters the path,
//  the robot halts immediately, holds its place in the trajectory, and
//  autonomously steers around the blockage before resuming!
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

static_assert(sizeof(TrajectoryPoint) == 24, "NVS trajectory format must remain 24 bytes per point");

static constexpr size_t MAX_POINTS = 1200;
static constexpr uint32_t SAMPLE_INTERVAL_MS = 50; // 20 Hz capture rate
static constexpr uint32_t STORAGE_COMBO_HOLD_MS = 1200;

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
    uint32_t _lastReplayTickMs = 0;
    size_t   _replayIdx = 0;

    // Mechanism state
    float _targetArm = 90.0f;
    float _targetGripper = 20.0f;
    bool  _gripperClosed = false;

    // AI Speedup & Tuning Knobs
    float _speedupMultiplier = 1.35f; // Default 35% faster execution
    float _gyroKp = 0.025f;          // Heading lock proportional correction gain
    float _nominalVoltage = 12.0f;   // Reference battery voltage for normalization

    // Collision Avoidance State
    bool     _obstacleDetected = false;
    uint32_t _obstacleBlockedStartMs = 0;
    bool     _inEvasiveBypass = false;
    uint32_t _bypassStartMs = 0;

    const char* _flashStatus = "NO SAVED ROUTE";
    bool _oledAvailable = false;
    uint32_t _lastOledRefreshMs = 0;
    bool _saveComboTiming = false;
    bool _clearComboTiming = false;
    bool _saveComboTriggered = false;
    bool _clearComboTriggered = false;
    uint32_t _saveComboStartMs = 0;
    uint32_t _clearComboStartMs = 0;

public:
    void init() override {
        arm.begin();
        gripper.begin();
        leftMotor.begin();
        rightMotor.begin();

        pinMode(DIGITAL_D1, OUTPUT);
        digitalWrite(DIGITAL_D1, LOW);
        pinMode(DIGITAL_D2, INPUT);

        _state = STATE_MANUAL_IDLE;
        _rawCount = 0;
        _optCount = 0;
        _inEvasiveBypass = false;
        _oledAvailable = botOled.isReady();
        _lastOledRefreshMs = 0;
        loadSavedRoute();

        userLed.setSolid(_optCount > 0 ? 255 : 0,
                         _optCount > 0 ? 140 : 120,
                         _optCount > 0 ? 0 : 120);
    }

    void loop() override {
        uint32_t now = millis();

        if (handleStorageCombos(now)) {
            sendTelemetryStatus();
            return;
        }

        // ── Emergency Abort ───────────────────────────────────────────────
        if (gamepad1.justPressed(BTN_LT)) {
            abortToIdle();
            return;
        }

        // ── Adjust Speedup Multiplier with D-pad ──────────────────────────
        if (gamepad1.justPressed(BTN_DPAD_RIGHT)) {
            _speedupMultiplier += 0.15f;
            if (_speedupMultiplier > 2.50f) _speedupMultiplier = 2.50f;
            if (_rawCount > 0) runOptimization();
        }
        if (gamepad1.justPressed(BTN_DPAD_LEFT)) {
            _speedupMultiplier -= 0.15f;
            if (_speedupMultiplier < 1.00f) _speedupMultiplier = 1.00f;
            if (_rawCount > 0) runOptimization();
        }

        // ── Manual Servo Controls ─────────────────────────────────────────
        if (_state != STATE_REPLAYING) {
            if (gamepad1.justPressed(BTN_LB)) {
                _gripperClosed = !_gripperClosed;
                _targetGripper = _gripperClosed ? 90.0f : 20.0f;
                gripper.setPosition(_targetGripper);
            }

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

                if (gamepad1.justPressed(BTN_X)) {
                    startRecording(now);
                } else if (gamepad1.justPressed(BTN_Y) && _rawCount > 0) {
                    runOptimization();
                } else if (gamepad1.justPressed(BTN_A) && _optCount > 0) {
                    startReplay(now);
                }
                break;

            case STATE_RECORDING:
                handleManualDrive();
                recordSample(now);

                if (gamepad1.justPressed(BTN_X)) {
                    stopRecording();
                    runOptimization();
                }
                break;

            case STATE_REPLAYING:
                if (gamepad1.justPressed(BTN_A)) {
                    abortToIdle();
                } else {
                    handleReplayTick(now);
                }
                break;
        }

        // ── Telemetry ─────────────────────────────────────────────────────
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

    bool handleStorageCombos(uint32_t now) {
        const bool saveHeld = gamepad1.pressed(BTN_RB) && gamepad1.pressed(BTN_Y);
        const bool clearHeld = gamepad1.pressed(BTN_RB) && gamepad1.pressed(BTN_LT);

        if (!saveHeld) {
            _saveComboTiming = false;
            _saveComboTriggered = false;
        }
        if (!clearHeld) {
            _clearComboTiming = false;
            _clearComboTriggered = false;
        }
        if (!saveHeld && !clearHeld) return false;

        drive.stop();

        if (clearHeld) {
            if (!_clearComboTiming) {
                _clearComboTiming = true;
                _clearComboStartMs = now;
            } else if (!_clearComboTriggered &&
                       now - _clearComboStartMs >= STORAGE_COMBO_HOLD_MS) {
                _clearComboTriggered = true;
                clearSavedRoute();
            }
        } else {
            if (!_saveComboTiming) {
                _saveComboTiming = true;
                _saveComboStartMs = now;
            } else if (!_saveComboTriggered &&
                       now - _saveComboStartMs >= STORAGE_COMBO_HOLD_MS) {
                _saveComboTriggered = true;
                saveOptimizedRoute();
            }
        }

        return true;
    }

    void loadSavedRoute() {
        _rawCount = 0;
        _optCount = 0;
        _state = STATE_MANUAL_IDLE;

        Preferences preferences;
        if (!preferences.begin("teach_repeat", true)) {
            _flashStatus = "NO SAVED ROUTE";
            return;
        }

        const uint32_t count = preferences.getUInt("opt_count", 0);
        const size_t byteLength = preferences.getBytesLength("opt_traj");
        if (count == 0) {
            preferences.end();
            _flashStatus = "NO SAVED ROUTE";
            return;
        }
        if (count > MAX_POINTS || byteLength != count * sizeof(TrajectoryPoint)) {
            preferences.end();
            _flashStatus = "INVALID ROUTE";
            return;
        }

        const size_t bytesRead = preferences.getBytes(
            "opt_traj", _optTrajectory, byteLength);
        preferences.end();
        if (bytesRead != byteLength) {
            _flashStatus = "INVALID ROUTE";
            return;
        }

        for (size_t i = 0; i < count; ++i) {
            const TrajectoryPoint& point = _optTrajectory[i];
            if (!std::isfinite(point.forward) || point.forward < -1.0f || point.forward > 1.0f ||
                !std::isfinite(point.turn) || point.turn < -1.0f || point.turn > 1.0f ||
                !std::isfinite(point.armAngle) || point.armAngle < 0.0f || point.armAngle > 180.0f ||
                !std::isfinite(point.gripperAngle) ||
                point.gripperAngle < 0.0f || point.gripperAngle > 180.0f ||
                !std::isfinite(point.headingDeg) ||
                (i > 0 && point.timeMs < _optTrajectory[i - 1].timeMs)) {
                _flashStatus = "INVALID ROUTE";
                return;
            }
        }

        _optCount = count;
        _state = STATE_OPTIMIZED;
        _flashStatus = "LOADED FROM FLASH";
    }

    void saveOptimizedRoute() {
        if (_optCount == 0 || _optCount > MAX_POINTS) {
            _flashStatus = "NO OPT ROUTE";
            return;
        }

        Preferences preferences;
        if (!preferences.begin("teach_repeat", false)) {
            _flashStatus = "SAVE FAILED";
            return;
        }

        const size_t byteLength = _optCount * sizeof(TrajectoryPoint);
        const size_t bytesWritten = preferences.putBytes(
            "opt_traj", _optTrajectory, byteLength);
        const size_t countWritten = bytesWritten == byteLength
            ? preferences.putUInt("opt_count", static_cast<uint32_t>(_optCount))
            : 0;
        preferences.end();

        _flashStatus = (bytesWritten == byteLength && countWritten == sizeof(uint32_t))
            ? "SAVED TO FLASH"
            : "SAVE FAILED";
    }

    void clearSavedRoute() {
        Preferences preferences;
        if (!preferences.begin("teach_repeat", false)) {
            _flashStatus = "CLEAR FAILED";
            return;
        }
        const bool cleared = preferences.clear();
        preferences.end();
        if (!cleared) {
            _flashStatus = "CLEAR FAILED";
            return;
        }

        _rawCount = 0;
        _optCount = 0;
        _replayIdx = 0;
        _state = STATE_MANUAL_IDLE;
        _flashStatus = "FLASH CLEARED";
        userLed.setSolid(0, 120, 120);
    }

    // ── Obstacle Sensing ──────────────────────────────────────────────────
    float readFrontDistanceCm() {
        digitalWrite(DIGITAL_D1, LOW);
        delayMicroseconds(2);
        digitalWrite(DIGITAL_D1, HIGH);
        delayMicroseconds(10);
        digitalWrite(DIGITAL_D1, LOW);
        long dur = pulseIn(DIGITAL_D2, HIGH, 16000);
        if (dur > 0) {
            float cm = (dur * 0.0343f) / 2.0f;
            if (cm > 2.0f && cm < 150.0f) return cm;
        }

        float v = power.readChannelVoltage(ADC_EXT_1);
        if (v >= 0.40f) return (27.0f / v);
        return 999.0f;
    }

    bool isObstacleAhead() {
        float dist = readFrontDistanceCm();
        if (dist > 2.5f && dist < 30.0f) return true;
        if (power.getMotorCurrent() > 2.85f) return true;
        return false;
    }

    // ── Phase 1: Recording ────────────────────────────────────────────────
    void startRecording(uint32_t now) {
        _rawCount = 0;
        _optCount = 0;
        _flashStatus = "UNSAVED ROUTE";
        _recordStartMs = now;
        _lastSampleMs = now;
        _state = STATE_RECORDING;

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

        size_t startIdx = 0;
        while (startIdx < _rawCount &&
               fabs(_rawTrajectory[startIdx].forward) < 0.05f &&
               fabs(_rawTrajectory[startIdx].turn) < 0.05f) {
            startIdx++;
        }
        if (startIdx >= _rawCount) startIdx = 0;

        size_t endIdx = _rawCount - 1;
        while (endIdx > startIdx &&
               fabs(_rawTrajectory[endIdx].forward) < 0.05f &&
               fabs(_rawTrajectory[endIdx].turn) < 0.05f) {
            endIdx--;
        }

        _optCount = 0;
        uint32_t baseTime = _rawTrajectory[startIdx].timeMs;

        for (size_t i = startIdx; i <= endIdx; i++) {
            if (_optCount >= MAX_POINTS) break;

            TrajectoryPoint p = _rawTrajectory[i];
            uint32_t relativeTime = p.timeMs - baseTime;
            p.timeMs = (uint32_t)((float)relativeTime / _speedupMultiplier);

            if (fabs(p.turn) < 0.15f && fabs(p.forward) > 0.2f) {
                p.forward = constrain(p.forward * 1.15f, -1.0f, 1.0f);
            }

            _optTrajectory[_optCount++] = p;
        }

        _state = STATE_OPTIMIZED;
        _flashStatus = "UNSAVED ROUTE";
        userLed.setSolid(255, 140, 0); // Solid Amber: Optimized & Ready
    }

    // ── Phase 3: Closed-Loop Replay with Collision Avoidance ───────────────
    void startReplay(uint32_t now) {
        if (_optCount == 0) return;

        _replayStartMs = now;
        _lastReplayTickMs = now;
        _replayIdx = 0;
        _state = STATE_REPLAYING;
        _inEvasiveBypass = false;
        _obstacleBlockedStartMs = 0;

        NRLComms::zeroHeading();
        userLed.setSolid(160, 0, 255); // Purple: Precision Replay
    }

    void handleReplayTick(uint32_t now) {
        uint32_t dt = now - _lastReplayTickMs;
        _lastReplayTickMs = now;

        // ── Real-Time Obstacle Detection & Dynamic Avoidance ──────────────
        if (isObstacleAhead()) {
            drive.stop();
            userLed.setBlink(255, 120, 0, 150); // Flash Orange: Obstacle in path!
            _obstacleDetected = true;

            if (_obstacleBlockedStartMs == 0) {
                _obstacleBlockedStartMs = now;
            }

            // Freeze the replay timeline so robot doesn't lose its position
            _replayStartMs += dt;

            // If blocked for more than 1.5 seconds, execute active bypass arc
            if (now - _obstacleBlockedStartMs > 1500 && !_inEvasiveBypass) {
                _inEvasiveBypass = true;
                _bypassStartMs = now;
            }

            if (_inEvasiveBypass) {
                uint32_t bypassTime = now - _bypassStartMs;
                if (bypassTime < 450) {
                    drive.drive(0.0f, 0.55f); // Steer right around obstacle
                } else if (bypassTime < 1100) {
                    drive.drive(0.60f, 0.0f); // Advance past obstacle
                } else if (bypassTime < 1550) {
                    drive.drive(0.0f, -0.55f); // Counter-steer back to course
                } else {
                    _inEvasiveBypass = false;
                    _obstacleBlockedStartMs = 0;
                }
            }
            return;
        } else {
            _obstacleDetected = false;
            _obstacleBlockedStartMs = 0;
            _inEvasiveBypass = false;
        }

        uint32_t elapsed = now - _replayStartMs;

        // Advance to the current keyframe
        while (_replayIdx + 1 < _optCount && _optTrajectory[_replayIdx + 1].timeMs <= elapsed) {
            _replayIdx++;
        }

        // Replay completed
        if (_replayIdx >= _optCount - 1 && elapsed >= _optTrajectory[_optCount - 1].timeMs) {
            abortToIdle();
            return;
        }

        const TrajectoryPoint& target = _optTrajectory[_replayIdx];

        // 1. Gyro Heading Lock
        float currentHeading = NRLComms::getHeading();
        float headingError = target.headingDeg - currentHeading;
        while (headingError > 180.0f)  headingError -= 360.0f;
        while (headingError < -180.0f) headingError += 360.0f;

        float steerCorrection = headingError * _gyroKp;
        float finalTurn = constrain(target.turn + steerCorrection, -1.0f, 1.0f);

        // 2. Battery Voltage Normalization
        float vCurrent = power.getBatteryVoltage();
        float vScale = 1.0f;
        if (vCurrent >= 8.0f && vCurrent <= 14.5f) {
            vScale = _nominalVoltage / vCurrent;
        }
        float finalForward = constrain(target.forward * vScale, -1.0f, 1.0f);

        // 3. Actuate
        drive.drive(finalForward, finalTurn);
        arm.setPosition(target.armAngle);
        gripper.setPosition(target.gripperAngle);
        userLed.setSolid(160, 0, 255);
    }

    void abortToIdle() {
        drive.stop();
        _state = (_optCount > 0) ? STATE_OPTIMIZED : STATE_MANUAL_IDLE;
        if (_state == STATE_OPTIMIZED) {
            userLed.setSolid(255, 140, 0);
        } else {
            userLed.setSolid(0, 120, 120);
        }
    }

    void sendTelemetryStatus() {
        const char* stateStr = "IDLE";
        if (_state == STATE_RECORDING) stateStr = "REC...";
        else if (_state == STATE_OPTIMIZED) stateStr = "OPTIMIZED";
        else if (_state == STATE_REPLAYING) {
            stateStr = _obstacleDetected ? "AVOIDING!" : "REPLAYING";
        }

        telemetry.addData("AI Mode", stateStr);
        telemetry.addData("Obstacle", _obstacleDetected ? "DETECTED" : "CLEAR");
        telemetry.addData("Speedup", _speedupMultiplier);
        telemetry.addData("Heading", NRLComms::getHeading());
        telemetry.addData("Batt(V)", power.getBatteryVoltage());
        if (strcmp(_flashStatus, "LOADED FROM FLASH") == 0) {
            telemetry.addData("Flash", "LOADED FROM");
            telemetry.addData("Flash2", "FLASH");
        } else {
            telemetry.addData("Flash", _flashStatus);
            telemetry.addData("Flash2", "");
        }
        updateFlashDisplay(millis());
    }

    void updateFlashDisplay(uint32_t now) {
        if (!_oledAvailable || now - _lastOledRefreshMs < 500) return;
        _lastOledRefreshMs = now;

        botOled.clearDisplay();
        botOled.setTextSize(1);
        botOled.setCursor(0, 0);
        botOled.print("TEACH & REPEAT");
        botOled.setCursor(0, 16);
        botOled.print("MODE: ");
        botOled.print(_state == STATE_OPTIMIZED ? "OPTIMIZED" :
                      _state == STATE_RECORDING ? "RECORDING" :
                      _state == STATE_REPLAYING ? "REPLAYING" : "IDLE");
        botOled.setCursor(0, 32);
        botOled.print(_flashStatus);
        botOled.display();
    }
};

REGISTER_OPMODE(TeachAndRepeatOpMode, "Teach & Repeat AI", TELEOP);
