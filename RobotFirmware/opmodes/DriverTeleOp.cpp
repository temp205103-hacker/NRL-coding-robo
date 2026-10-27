#include "NRL.h"
#include "RobotCalibration.h"
#include "BoardPins.h"

static HexaDCMotor leftMotor  {{ .dirPin = MOTOR_L_DIR, .pwmPin = MOTOR_L_PWM }};
static HexaDCMotor rightMotor {{ .dirPin = MOTOR_R_DIR, .pwmPin = MOTOR_R_PWM, .flipped = true }};
static HexaServo arm       {{ .signalPin = SERVO_1, .startAngle = ARM_ANGLE_START }};
static HexaServo gripper   {{ .signalPin = SERVO_2, .startAngle = GRIPPER_ANGLE_START }};
static HexaServo cargoBay  {{ .signalPin = SERVO_3, .startAngle = CARGOBAY_ANGLE_START }};
static HexaServo vaultGate {{ .signalPin = SERVO_4, .startAngle = VAULT_ANGLE_START }};
static TankDrive drive(leftMotor, rightMotor);

class DriverTeleOp : public NRLOpMode {
private:
    float _armAngle = ARM_ANGLE_START;
    bool _gripperClosed = false;
    bool _cargoBayOpen = false;
    bool _vaultGateOpen = false;

public:
    void init() override {
        leftMotor.begin();
        rightMotor.begin();
        arm.begin();
        gripper.begin();
        cargoBay.begin();
        vaultGate.begin();
        drive.setRampRate(0.18f);
        userLed.setSolid(0, 150, 255);
    }

    void loop() override {
        const bool precisionMode = gamepad1.pressed(BTN_LB);
        drive.setScale(precisionMode ? 0.35f : 1.0f);
        drive.drive(gamepad1.leftY(), gamepad1.rightX());

        if (gamepad1.justPressed(BTN_X) && !isActionRunning()) {
            runAction(sequential({
                instant([this] {
                    _gripperClosed = true;
                    gripper.setPosition(GRIPPER_ANGLE_CLOSE);
                }),
                sleep_ms(250),
                instant([this] {
                    _armAngle = ARM_ANGLE_CARRY;
                    arm.setPosition(_armAngle);
                })
            }));
        }

        if (!isActionRunning()) {
            if (gamepad1.justPressed(BTN_DPAD_UP)) {
                _armAngle = ARM_ANGLE_HIGH_POLE;
                arm.setPosition(_armAngle);
            } else if (gamepad1.justPressed(BTN_DPAD_DOWN)) {
                _armAngle = ARM_ANGLE_FLOOR;
                arm.setPosition(_armAngle);
            } else if (gamepad1.justPressed(BTN_RB)) {
                _armAngle = ARM_ANGLE_APEX_POLE;
                arm.setPosition(_armAngle);
            }
        }

        if (gamepad1.justPressed(BTN_Y)) {
            _cargoBayOpen = !_cargoBayOpen;
            cargoBay.setPosition(_cargoBayOpen ? CARGOBAY_ANGLE_OPEN : CARGOBAY_ANGLE_CLOSED);
        }
        if (!isActionRunning() && gamepad1.justPressed(BTN_A)) {
            _gripperClosed = !_gripperClosed;
            gripper.setPosition(_gripperClosed ? GRIPPER_ANGLE_CLOSE : GRIPPER_ANGLE_OPEN);
        }
        if (gamepad1.justPressed(BTN_RT)) {
            _vaultGateOpen = !_vaultGateOpen;
            vaultGate.setPosition(_vaultGateOpen ? VAULT_ANGLE_OPEN : VAULT_ANGLE_CLOSED);
        }

        const float batteryVoltage = power.getBatteryVoltage();
        telemetry.addData("Battery(V)", batteryVoltage);
        telemetry.addData("Battery", batteryVoltage < 11.0f ? "LOW: CHARGE" : "OK");
        telemetry.addData("Motor(A)", power.getMotorCurrent());
        telemetry.addData("Arm(deg)", _armAngle);
        telemetry.addData("Gripper", _gripperClosed ? "CLOSED" : "OPEN");
        telemetry.addData("Speed", precisionMode ? "PRECISION 35%" : "NORMAL");
        telemetry.addData("CargoBay", _cargoBayOpen ? "OPEN" : "CLOSED");
        telemetry.addData("Vault", _vaultGateOpen ? "OPEN" : "CLOSED");
    }

    void stop() override {
        drive.stop();
        arm.detach();
        gripper.detach();
        cargoBay.detach();
        vaultGate.detach();
        userLed.setOff();
    }
};

REGISTER_OPMODE(DriverTeleOp, "Driver TeleOp", TELEOP);
