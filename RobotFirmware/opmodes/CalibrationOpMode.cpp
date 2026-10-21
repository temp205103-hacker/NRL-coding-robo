#include "NRL.h"
#include "RobotCalibration.h"

// ============================================================================
//  CalibrationOpMode — Interactive Physical Calibration Tool  (Task 5)
// ============================================================================
//  Run this OpMode on the real robot to measure and verify all physical
//  calibration constants before competition. After measuring, update the
//  values in RobotCalibration.h, rebuild, and re-upload the firmware.
//
//  Controller menu name: "Calibration Tool"  (TELEOP)
//
//  MENU NAVIGATION:
//    BTN_X    →  Odometry Test (measures MS_PER_INCH)
//    BTN_A    →  Arm Servo Sweep (find ARM_ANGLE_* values)
//    BTN_Y    →  Gripper Test (find GRIPPER_ANGLE_* values)
//    BTN_RB   →  Cargo Bay Test (find CARGOBAY_ANGLE_* values)
//    BTN_LB   →  Vault Gate Test (find VAULT_ANGLE_* values)
//    BTN_RT   →  Balance Pitch Test (verify IMU reads real pitch)
//    BTN_LT   →  Return to Main Menu from any sub-test
//
//  D-PAD inside each test:
//    D-Pad UP / DOWN   →  Increase / Decrease angle by 5° (servo tests)
//    D-Pad RIGHT / LEFT →  Increase / Decrease by 1° (fine-tune)
//
//  Watch the CONTROLLER LCD for live readouts of the current value.
// ============================================================================

// Declare hardware specific to calibration (motors + all servos)
static HexaDCMotor calLeftMotor  {{ .dirPin = MOTOR_L_DIR, .pwmPin = MOTOR_L_PWM }};
static HexaDCMotor calRightMotor {{ .dirPin = MOTOR_R_DIR, .pwmPin = MOTOR_R_PWM, .flipped = true }};
static HexaServo   calArm        {{ .signalPin = SERVO_1, .startAngle = ARM_ANGLE_START   }};
static HexaServo   calGripper    {{ .signalPin = SERVO_2, .startAngle = GRIPPER_ANGLE_START }};
static HexaServo   calCargoBay   {{ .signalPin = SERVO_3, .startAngle = CARGOBAY_ANGLE_START }};
static HexaServo   calVaultGate  {{ .signalPin = SERVO_4, .startAngle = VAULT_ANGLE_START  }};
static TankDrive   calDrive(calLeftMotor, calRightMotor);
static HexaIMU     calImu(hexaImuConfig());

class CalibrationOpMode : public NRLOpMode {
private:
    enum TestMode {
        MODE_MENU,
        MODE_ODOMETRY,
        MODE_ARM,
        MODE_GRIPPER,
        MODE_CARGOBAY,
        MODE_VAULT,
        MODE_BALANCE
    };

    TestMode _mode = MODE_MENU;

    // Servo angle state for sweep tests
    float _armAngle      = ARM_ANGLE_START;
    float _gripperAngle  = GRIPPER_ANGLE_START;
    float _cargoBayAngle = CARGOBAY_ANGLE_START;
    float _vaultAngle    = VAULT_ANGLE_START;

    // Odometry test state
    bool     _odoRunning    = false;
    uint32_t _odoStartMs    = 0;
    uint32_t _odoDurationMs = ODOMETRY_TEST_DURATION_MS;

    // Track last button state to avoid repeat triggers
    uint32_t _lastAngleChangeMs = 0;
    static constexpr uint32_t ANGLE_STEP_INTERVAL_MS = 150; // ms between auto-repeat

public:
    void init() override {
        calArm.begin();
        calGripper.begin();
        calCargoBay.begin();
        calVaultGate.begin();
        calLeftMotor.begin();
        calRightMotor.begin();
        calImu.begin();

        // Ultrasonic sensor setup
        pinMode(DIGITAL_D1, OUTPUT);
        digitalWrite(DIGITAL_D1, LOW);
        pinMode(DIGITAL_D2, INPUT);

        _mode = MODE_MENU;
        userLed.setSolid(0, 255, 255); // Cyan: Calibration mode
    }

    void loop() override {
        uint32_t now = millis();

        // ── Global: BTN_LT returns to main menu ────────────────────────────────
        if (gamepad1.justPressed(BTN_LT)) {
            returnToMenu();
            return;
        }

        switch (_mode) {
            case MODE_MENU:    handleMenu();           break;
            case MODE_ODOMETRY:handleOdometry(now);    break;
            case MODE_ARM:     handleArmSweep(now);    break;
            case MODE_GRIPPER: handleGripperTest(now); break;
            case MODE_CARGOBAY:handleCargoBayTest(now);break;
            case MODE_VAULT:   handleVaultTest(now);   break;
            case MODE_BALANCE: handleBalanceTest();    break;
        }
    }

    void stop() override {
        calDrive.stop();
        calArm.detach();
        calGripper.detach();
        calCargoBay.detach();
        calVaultGate.detach();
        userLed.setOff();
    }

private:

    // ── Main Menu ─────────────────────────────────────────────────────────────
    void handleMenu() {
        userLed.setSolid(0, 255, 255);
        telemetry.addData("=== CALIBRATION TOOL ===", "");
        telemetry.addData("BTN_X",  "Odometry Test");
        telemetry.addData("BTN_A",  "Arm Servo Sweep");
        telemetry.addData("BTN_Y",  "Gripper Test");
        telemetry.addData("BTN_RB", "Cargo Bay Test");
        telemetry.addData("BTN_LB", "Vault Gate Test");
        telemetry.addData("BTN_RT", "IMU Balance Test");
        telemetry.addData("BTN_LT", "(any test) -> Menu");

        if (gamepad1.justPressed(BTN_X))  { _mode = MODE_ODOMETRY;  userLed.setBlink(255, 165, 0, 400); }
        if (gamepad1.justPressed(BTN_A))  { _mode = MODE_ARM;        userLed.setSolid(255, 0, 255); }
        if (gamepad1.justPressed(BTN_Y))  { _mode = MODE_GRIPPER;    userLed.setSolid(0, 0, 255); }
        if (gamepad1.justPressed(BTN_RB)) { _mode = MODE_CARGOBAY;   userLed.setSolid(255, 100, 0); }
        if (gamepad1.justPressed(BTN_LB)) { _mode = MODE_VAULT;      userLed.setSolid(100, 0, 255); }
        if (gamepad1.justPressed(BTN_RT)) { _mode = MODE_BALANCE;    userLed.setSolid(0, 255, 0); }
    }

    void returnToMenu() {
        calDrive.stop();
        _odoRunning = false;
        _mode = MODE_MENU;
    }

    // ── TEST A: Odometry — measures MS_PER_INCH ───────────────────────────────
    void handleOdometry(uint32_t now) {
        telemetry.addData("=== ODOMETRY TEST ===", "");
        telemetry.addData("BTN_X", "Start 5-sec drive");
        telemetry.addData("", "Measure inches driven");
        telemetry.addData("Formula", "5000 / inches = MS_PER_INCH");
        telemetry.addData("Current MS_PER_INCH", MS_PER_INCH);
        telemetry.addData("Drive Power", DRIVE_POWER);

        if (!_odoRunning) {
            if (gamepad1.justPressed(BTN_X)) {
                _odoRunning  = true;
                _odoStartMs  = now;
                userLed.setBlink(255, 0, 0, 200); // Rapid red: DRIVING
            }
            telemetry.addData("Status", "Press BTN_X to drive");
        } else {
            uint32_t elapsed = now - _odoStartMs;
            telemetry.addData("Status", "DRIVING...");
            telemetry.addData("Time left (ms)", (int)(_odoDurationMs - elapsed));
            telemetry.addData("Batt(V)", power.getBatteryVoltage());

            if (elapsed < _odoDurationMs) {
                // Drive straight with gyro heading lock
                calDrive.drive(DRIVE_POWER, 0.0f);
            } else {
                calDrive.stop();
                _odoRunning = false;
                userLed.setSolid(0, 255, 0); // Green: done
                telemetry.addData("Status", "DONE — measure distance!");
                telemetry.addData("Formula", "5000 / inches_traveled");
                telemetry.addData("Result goes in", "RobotCalibration.h");
                telemetry.addData("  MS_PER_INCH =", " ???");
            }
        }
    }

    // ── TEST B: Arm Servo Sweep ───────────────────────────────────────────────
    void handleArmSweep(uint32_t now) {
        telemetry.addData("=== ARM SERVO SWEEP ===", "");
        telemetry.addData("D-Pad UP/DOWN",  "+/- 5 degrees");
        telemetry.addData("D-Pad R/L",      "+/- 1 degree");
        telemetry.addData("Current Angle", _armAngle);
        telemetry.addData("---", "---");
        telemetry.addData("Floor target",    ARM_ANGLE_FLOOR);
        telemetry.addData("Carry target",    ARM_ANGLE_CARRY);
        telemetry.addData("HighPole target", ARM_ANGLE_HIGH_POLE);
        telemetry.addData("ApexPole target", ARM_ANGLE_APEX_POLE);

        stepAngle(_armAngle, now, 5.0f, 1.0f);
        _armAngle = constrain(_armAngle, 0.0f, 180.0f);
        calArm.setPosition(_armAngle);
    }

    // ── TEST C: Gripper Servo Test ────────────────────────────────────────────
    void handleGripperTest(uint32_t now) {
        telemetry.addData("=== GRIPPER TEST ===", "");
        telemetry.addData("D-Pad UP/DOWN", "+/- 5 degrees");
        telemetry.addData("D-Pad R/L",     "+/- 1 degree");
        telemetry.addData("Current Angle", _gripperAngle);
        telemetry.addData("---", "---");
        telemetry.addData("OPEN target",  GRIPPER_ANGLE_OPEN);
        telemetry.addData("CLOSE target", GRIPPER_ANGLE_CLOSE);
        telemetry.addData("Tip: No buzz", "= correct clamp angle");

        stepAngle(_gripperAngle, now, 5.0f, 1.0f);
        _gripperAngle = constrain(_gripperAngle, 0.0f, 180.0f);
        calGripper.setPosition(_gripperAngle);
    }

    // ── TEST D: Cargo Bay Test ────────────────────────────────────────────────
    void handleCargoBayTest(uint32_t now) {
        telemetry.addData("=== CARGO BAY TEST ===", "");
        telemetry.addData("D-Pad UP/DOWN", "+/- 5 degrees");
        telemetry.addData("D-Pad R/L",     "+/- 1 degree");
        telemetry.addData("Current Angle", _cargoBayAngle);
        telemetry.addData("---", "---");
        telemetry.addData("CLOSED target", CARGOBAY_ANGLE_CLOSED);
        telemetry.addData("OPEN target",   CARGOBAY_ANGLE_OPEN);
        telemetry.addData("CLAMP target",  CARGOBAY_ANGLE_CLAMP);

        stepAngle(_cargoBayAngle, now, 5.0f, 1.0f);
        _cargoBayAngle = constrain(_cargoBayAngle, 0.0f, 180.0f);
        calCargoBay.setPosition(_cargoBayAngle);
    }

    // ── TEST E: Vault Gate Test ───────────────────────────────────────────────
    void handleVaultTest(uint32_t now) {
        telemetry.addData("=== VAULT GATE TEST ===", "");
        telemetry.addData("D-Pad UP/DOWN", "+/- 5 degrees");
        telemetry.addData("D-Pad R/L",     "+/- 1 degree");
        telemetry.addData("Current Angle", _vaultAngle);
        telemetry.addData("---", "---");
        telemetry.addData("CLOSED target", VAULT_ANGLE_CLOSED);
        telemetry.addData("OPEN target",   VAULT_ANGLE_OPEN);

        stepAngle(_vaultAngle, now, 5.0f, 1.0f);
        _vaultAngle = constrain(_vaultAngle, 0.0f, 180.0f);
        calVaultGate.setPosition(_vaultAngle);
    }

    // ── TEST F: IMU Balance Pitch Test ────────────────────────────────────────
    void handleBalanceTest() {
        calImu.read();
        float ax = calImu.getAccelX();
        float ay = calImu.getAccelY();
        float az = calImu.getAccelZ();

        float pitchDeg = atan2f(-ax, sqrtf(ay * ay + az * az)) * (180.0f / 3.14159f);
        float rollDeg  = atan2f( ay, sqrtf(ax * ax + az * az)) * (180.0f / 3.14159f);

        telemetry.addData("=== BALANCE / IMU TEST ===", "");
        telemetry.addData("Pitch (fwd/back)", pitchDeg);
        telemetry.addData("Roll  (left/right)", rollDeg);
        telemetry.addData("Accel X", ax);
        telemetry.addData("Accel Y", ay);
        telemetry.addData("Accel Z", az);
        telemetry.addData("---", "---");
        telemetry.addData("Tolerance target", BALANCE_PITCH_TOLERANCE_DEG);

        if (fabsf(pitchDeg) <= BALANCE_PITCH_TOLERANCE_DEG) {
            userLed.setSolid(0, 255, 0);       // Green: level
            telemetry.addData("Status", "LEVEL (within tolerance)");
        } else if (pitchDeg > 0) {
            userLed.setSolid(255, 165, 0);     // Orange: tilted forward
            telemetry.addData("Status", "TILTED FORWARD");
        } else {
            userLed.setSolid(255, 0, 0);       // Red: tilted backward
            telemetry.addData("Status", "TILTED BACKWARD");
        }

        telemetry.addData("Batt(V)", power.getBatteryVoltage());
        telemetry.addData("Motor(A)", power.getMotorCurrent());
    }

    // ── Helper: D-Pad angle stepping with auto-repeat ─────────────────────────
    void stepAngle(float& angle, uint32_t now, float coarseStep, float fineStep) {
        bool canStep = (now - _lastAngleChangeMs >= ANGLE_STEP_INTERVAL_MS);

        if (canStep) {
            bool changed = false;
            if (gamepad1.pressed(BTN_DPAD_UP))    { angle += coarseStep; changed = true; }
            if (gamepad1.pressed(BTN_DPAD_DOWN))  { angle -= coarseStep; changed = true; }
            if (gamepad1.pressed(BTN_DPAD_RIGHT)) { angle += fineStep;   changed = true; }
            if (gamepad1.pressed(BTN_DPAD_LEFT))  { angle -= fineStep;   changed = true; }
            if (changed) _lastAngleChangeMs = now;
        }
    }
};

REGISTER_OPMODE(CalibrationOpMode, "Calibration Tool", TELEOP);
