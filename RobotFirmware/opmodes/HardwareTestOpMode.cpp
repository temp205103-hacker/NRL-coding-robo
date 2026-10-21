#include "NRL.h"
#include "RobotCalibration.h"

// ============================================================================
//  HardwareTestOpMode — interactive bench and pit diagnostics
// ============================================================================
//  BTN_X / BTN_A: run the left / right drive channel forward and reverse
//  D-pad LEFT / RIGHT: choose the active servo
//  Hold D-pad UP / DOWN: sweep the active servo through its configured range
//  BTN_Y: play three buzzer beeps
//  BTN_LB: cycle the user LED through red, green, blue, and off
//
//  The BaseBot build has four drive motors but this board exposes two
//  production motor channels. Test the left/right channels; paired front/rear
//  motors on a side must be wired to that side's channel. The BaseBot guide
//  includes the arm and gripper servos; Cargo Bay and Vault gate are optional
//  team-built mechanisms and should only be tested when connected.
// ============================================================================

static HexaDCMotor testLeftMotor  {{ .dirPin = MOTOR_L_DIR, .pwmPin = MOTOR_L_PWM }};
static HexaDCMotor testRightMotor {{ .dirPin = MOTOR_R_DIR, .pwmPin = MOTOR_R_PWM, .flipped = true }};
static HexaServo   testArm        {{ .signalPin = SERVO_1, .startAngle = ARM_ANGLE_START }};
static HexaServo   testGripper    {{ .signalPin = SERVO_2, .startAngle = GRIPPER_ANGLE_START }};
static HexaServo   testCargoBay   {{ .signalPin = SERVO_3, .startAngle = CARGOBAY_ANGLE_START }};
static HexaServo   testVaultGate  {{ .signalPin = SERVO_4, .startAngle = VAULT_ANGLE_START }};
static HexaIMU     testImu(hexaImuConfig());

class HardwareTestOpMode : public NRLOpMode {
private:
    enum MotorTestStage {
        MOTOR_TEST_IDLE,
        MOTOR_TEST_FORWARD,
        MOTOR_TEST_REVERSE
    };

    static constexpr uint32_t MOTOR_TEST_STEP_MS = 1000;
    static constexpr uint32_t SERVO_STEP_INTERVAL_MS = 80;
    static constexpr uint32_t SENSOR_SAMPLE_INTERVAL_MS = 100;
    static constexpr uint32_t OLED_REFRESH_INTERVAL_MS = 250;
    static constexpr int MOTOR_TEST_SPEED = 160;

    MotorTestStage _motorStage = MOTOR_TEST_IDLE;
    HexaDCMotor* _motorUnderTest = nullptr;
    uint32_t _motorStageStartMs = 0;

    uint8_t _activeServo = 0;
    float _servoPosition = ARM_ANGLE_START;
    int8_t _servoSweepDirection = 1;
    uint32_t _lastServoStepMs = 0;

    uint8_t _ledTestStep = 3;

    bool _buzzerActive = false;
    int8_t _buzzerPhase = -1;
    uint32_t _buzzerStartMs = 0;
    uint32_t _lastSensorSampleMs = 0;
    uint32_t _lastOledRefreshMs = 0;

    float _ultrasonicDistanceCm = 999.0f;
    float _irVoltage = 0.0f;
    float _pitchDeg = 0.0f;
    float _rollDeg = 0.0f;
    bool _imuAvailable = false;
    bool _oledAvailable = false;
    bool _leftMotorAvailable = false;
    bool _rightMotorAvailable = false;
    bool _armAvailable = false;
    bool _gripperAvailable = false;
    bool _cargoBayAvailable = false;
    bool _vaultGateAvailable = false;

public:
    void init() override {
        _leftMotorAvailable = testLeftMotor.begin();
        _rightMotorAvailable = testRightMotor.begin();
        _armAvailable = testArm.begin();
        _gripperAvailable = testGripper.begin();
        _cargoBayAvailable = testCargoBay.begin();
        _vaultGateAvailable = testVaultGate.begin();
        _imuAvailable = testImu.begin();

        pinMode(DIGITAL_D1, OUTPUT);
        digitalWrite(DIGITAL_D1, LOW);
        pinMode(DIGITAL_D2, INPUT);
        pinMode(BUZZER_PIN, OUTPUT);
        digitalWrite(BUZZER_PIN, LOW);

        _servoPosition = testArm.getPosition();
        _oledAvailable = botOled.isReady();
        userLed.setSolid(0, 255, 255);
    }

    void loop() override {
        const uint32_t now = millis();

        handleMotorTests(now);
        handleServoSelectionAndSweep(now);
        handleBuzzerTest(now);
        handleLedTest();
        updateSensorReadings(now);
        updateOled(now);
        sendControllerTelemetry();
    }

    void stop() override {
        testLeftMotor.stop();
        testRightMotor.stop();
        testArm.detach();
        testGripper.detach();
        testCargoBay.detach();
        testVaultGate.detach();
        noTone(BUZZER_PIN);
        digitalWrite(BUZZER_PIN, LOW);
        userLed.setOff();
    }

private:
    HexaServo& servoAt(uint8_t index) {
        switch (index) {
            case 0: return testArm;
            case 1: return testGripper;
            case 2: return testCargoBay;
            default: return testVaultGate;
        }
    }

    bool servoAvailable(uint8_t index) const {
        switch (index) {
            case 0: return _armAvailable;
            case 1: return _gripperAvailable;
            case 2: return _cargoBayAvailable;
            default: return _vaultGateAvailable;
        }
    }

    float servoLowerLimit(uint8_t index) const {
        switch (index) {
            case 0: return ARM_ANGLE_FLOOR;
            case 1: return GRIPPER_ANGLE_OPEN;
            case 2: return CARGOBAY_ANGLE_CLOSED;
            default: return VAULT_ANGLE_CLOSED;
        }
    }

    float servoUpperLimit(uint8_t index) const {
        switch (index) {
            case 0: return ARM_ANGLE_APEX_POLE;
            case 1: return GRIPPER_ANGLE_CLOSE;
            case 2: return CARGOBAY_ANGLE_OPEN;
            default: return VAULT_ANGLE_OPEN;
        }
    }

    const char* motorTestStatus() const {
        switch (_motorStage) {
            case MOTOR_TEST_FORWARD: return _motorUnderTest == &testLeftMotor
                ? "LEFT channel forward" : "RIGHT channel forward";
            case MOTOR_TEST_REVERSE: return _motorUnderTest == &testLeftMotor
                ? "LEFT channel reverse" : "RIGHT channel reverse";
            default: return "Idle";
        }
    }

    const char* servoName() const {
        switch (_activeServo) {
            case 0: return "ARM";
            case 1: return "GRIPPER";
            case 2: return "CARGO BAY";
            default: return "VAULT GATE";
        }
    }

    void startMotorTest(HexaDCMotor& motor, uint32_t now) {
        if ((&motor == &testLeftMotor && !_leftMotorAvailable) ||
            (&motor == &testRightMotor && !_rightMotorAvailable)) {
            return;
        }
        _motorUnderTest = &motor;
        _motorUnderTest->setSpeed(MOTOR_TEST_SPEED);
        _motorStage = MOTOR_TEST_FORWARD;
        _motorStageStartMs = now;
    }

    void handleMotorTests(uint32_t now) {
        if (_motorStage == MOTOR_TEST_IDLE) {
            if (gamepad1.justPressed(BTN_X)) {
                startMotorTest(testLeftMotor, now);
            } else if (gamepad1.justPressed(BTN_A)) {
                startMotorTest(testRightMotor, now);
            }
            return;
        }

        if (now - _motorStageStartMs < MOTOR_TEST_STEP_MS) return;

        if (_motorStage == MOTOR_TEST_FORWARD) {
            _motorUnderTest->setSpeed(-MOTOR_TEST_SPEED);
            _motorStage = MOTOR_TEST_REVERSE;
            _motorStageStartMs = now;
        } else {
            _motorUnderTest->stop();
            _motorUnderTest = nullptr;
            _motorStage = MOTOR_TEST_IDLE;
        }
    }

    void selectServo(int8_t direction) {
        _activeServo = static_cast<uint8_t>(
            (_activeServo + 4 + direction) % 4);
        HexaServo& servo = servoAt(_activeServo);
        _servoPosition = constrain(
            servo.getPosition(), servoLowerLimit(_activeServo), servoUpperLimit(_activeServo));
        _servoSweepDirection = 1;
    }

    void handleServoSelectionAndSweep(uint32_t now) {
        if (gamepad1.justPressed(BTN_DPAD_LEFT)) selectServo(-1);
        if (gamepad1.justPressed(BTN_DPAD_RIGHT)) selectServo(1);
        if (!servoAvailable(_activeServo)) return;

        if (gamepad1.justPressed(BTN_DPAD_UP)) _servoSweepDirection = 1;
        if (gamepad1.justPressed(BTN_DPAD_DOWN)) _servoSweepDirection = -1;
        if (!gamepad1.pressed(BTN_DPAD_UP) && !gamepad1.pressed(BTN_DPAD_DOWN)) return;
        if (now - _lastServoStepMs < SERVO_STEP_INTERVAL_MS) return;
        _lastServoStepMs = now;

        HexaServo& servo = servoAt(_activeServo);
        _servoPosition += _servoSweepDirection * 2.0f;
        const float lowerLimit = servoLowerLimit(_activeServo);
        const float upperLimit = servoUpperLimit(_activeServo);
        if (_servoPosition >= upperLimit) {
            _servoPosition = upperLimit;
            _servoSweepDirection = -1;
        } else if (_servoPosition <= lowerLimit) {
            _servoPosition = lowerLimit;
            _servoSweepDirection = 1;
        }
        servo.setPosition(_servoPosition);
    }

    void handleBuzzerTest(uint32_t now) {
        if (gamepad1.justPressed(BTN_Y) && !_buzzerActive) {
            _buzzerActive = true;
            _buzzerPhase = -1;
            _buzzerStartMs = now;
        }
        if (!_buzzerActive) return;

        const uint32_t elapsed = now - _buzzerStartMs;
        if (elapsed >= 600) {
            noTone(BUZZER_PIN);
            digitalWrite(BUZZER_PIN, LOW);
            _buzzerActive = false;
            _buzzerPhase = -1;
            return;
        }

        const int8_t phase = static_cast<int8_t>((elapsed / 100) % 2);
        if (phase == _buzzerPhase) return;

        if (phase == 0) {
            tone(BUZZER_PIN, 2000);
        } else {
            noTone(BUZZER_PIN);
            digitalWrite(BUZZER_PIN, LOW);
        }
        _buzzerPhase = phase;
    }

    void handleLedTest() {
        if (!gamepad1.justPressed(BTN_LB)) return;

        _ledTestStep = static_cast<uint8_t>((_ledTestStep + 1) % 4);
        switch (_ledTestStep) {
            case 0: userLed.setSolid(255, 0, 0); break;
            case 1: userLed.setSolid(0, 255, 0); break;
            case 2: userLed.setSolid(0, 0, 255); break;
            default: userLed.setOff(); break;
        }
    }

    void updateSensorReadings(uint32_t now) {
        if (_imuAvailable) {
            testImu.tick(now);
            if (testImu.read()) {
                const float ax = testImu.getAccelX();
                const float ay = testImu.getAccelY();
                const float az = testImu.getAccelZ();
                _pitchDeg = atan2f(-ax, sqrtf(ay * ay + az * az)) * (180.0f / 3.14159265f);
                _rollDeg = atan2f(ay, sqrtf(ax * ax + az * az)) * (180.0f / 3.14159265f);
            } else {
                _imuAvailable = false;
            }
        }

        if (now - _lastSensorSampleMs < SENSOR_SAMPLE_INTERVAL_MS) return;
        _lastSensorSampleMs = now;

        digitalWrite(DIGITAL_D1, LOW);
        delayMicroseconds(2);
        digitalWrite(DIGITAL_D1, HIGH);
        delayMicroseconds(10);
        digitalWrite(DIGITAL_D1, LOW);
        const long duration = pulseIn(DIGITAL_D2, HIGH, 16000);
        _ultrasonicDistanceCm = duration > 0
            ? (duration * 0.0343f) / 2.0f
            : 999.0f;
        _irVoltage = power.readChannelVoltage(ADC_EXT_1);
    }

    void printOledSensorLine(int16_t y, const char* label, float value, const char* unit) {
        botOled.setCursor(0, y);
        botOled.print(label);
        botOled.print(value, 1);
        botOled.print(unit);
    }

    void updateOled(uint32_t now) {
        if (!_oledAvailable || now - _lastOledRefreshMs < OLED_REFRESH_INTERVAL_MS) return;
        _lastOledRefreshMs = now;

        botOled.clearDisplay();
        botOled.setTextSize(1);
        botOled.setCursor(0, 0);
        botOled.print("US:");
        if (_ultrasonicDistanceCm >= 999.0f) {
            botOled.print("NO ECHO");
        } else {
            botOled.print(_ultrasonicDistanceCm, 1);
            botOled.print("cm");
        }

        printOledSensorLine(8, "IR:", _irVoltage, "V");
        printOledSensorLine(16, "BAT:", power.getBatteryVoltage(), "V");
        printOledSensorLine(24, "MOTOR:", power.getMotorCurrent(), "A");
        printOledSensorLine(32, "SERVO:", power.getServoCurrent(), "A");
        if (_imuAvailable) {
            printOledSensorLine(40, "PITCH:", _pitchDeg, "d");
            printOledSensorLine(48, "ROLL:", _rollDeg, "d");
            printOledSensorLine(56, "HDG:", testImu.getHeading(), "d");
        } else {
            botOled.setCursor(0, 40);
            botOled.print("IMU UNAVAILABLE");
        }
        botOled.display();
    }

    void sendControllerTelemetry() {
        telemetry.addData("Diagnostics", "HW Diagnostics");
        telemetry.addData("Motor test", motorTestStatus());
        telemetry.addData("Left motor channel", _leftMotorAvailable ? "INIT OK" : "INIT FAILED");
        telemetry.addData("Right motor channel", _rightMotorAvailable ? "INIT OK" : "INIT FAILED");
        telemetry.addData("Drive outputs", "2 side channels; pair motors");
        telemetry.addData("Active servo", servoAvailable(_activeServo)
            ? servoName() : "DRIVER INIT FAILED");
        if (servoAvailable(_activeServo)) telemetry.addData("Servo angle", _servoPosition);
        telemetry.addData("Servo sweep", "Configured range only");
        telemetry.addData("Buzzer", _buzzerActive ? "Playing" : "Y=3 beeps");
        telemetry.addData("LED", "LB=cycle color");
        telemetry.addData("Ultrasonic (cm)", _ultrasonicDistanceCm);
        telemetry.addData("IR ADC (V)", _irVoltage);
        telemetry.addData("Battery (V)", power.getBatteryVoltage());
        telemetry.addData("Motor current (A)", power.getMotorCurrent());
        telemetry.addData("Servo current (A)", power.getServoCurrent());
        if (_imuAvailable) {
            telemetry.addData("IMU pitch (deg)", _pitchDeg);
            telemetry.addData("IMU roll (deg)", _rollDeg);
            telemetry.addData("IMU heading (deg)", testImu.getHeading());
        } else {
            telemetry.addData("IMU pitch (deg)", "UNAVAILABLE");
            telemetry.addData("IMU roll (deg)", "UNAVAILABLE");
            telemetry.addData("IMU heading (deg)", "UNAVAILABLE");
        }
        telemetry.addData("IMU status", _imuAvailable ? "OK" : "UNAVAILABLE");
        telemetry.addData("OLED status", _oledAvailable ? "OK" : "UNAVAILABLE");
    }
};

REGISTER_OPMODE(HardwareTestOpMode, "HW Diagnostics", TELEOP);
