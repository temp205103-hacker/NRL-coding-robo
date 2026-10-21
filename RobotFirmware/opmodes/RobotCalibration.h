#pragma once

// ============================================================================
//  RobotCalibration.h — Centralized Physical Calibration Constants
// ============================================================================
//  ALL physical robot measurements live here. When you run the
//  CalibrationOpMode on the real robot and measure values, update ONLY
//  this file. All AI Drivers, SmartDrive, and mechanism actions pick up
//  the new values automatically on the next build.
//
//  HOW TO CALIBRATE (follow in this order):
//  ─────────────────────────────────────────
//  Step 1 ─ Flash firmware, run "HW Diagnostics" opmode on the field mat.
//  Step 2 ─ Run "Calibration Tool" opmode on the actual robot.
//            Press buttons as prompted, measure with a tape measure.
//  Step 3 ─ Update the constants below, rebuild, and re-upload.
// ============================================================================

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION A: Drive / Odometry
// ─────────────────────────────────────────────────────────────────────────────

//  MS_PER_INCH — Time in milliseconds for the robot to travel exactly
//  one inch at DRIVE_POWER on the NRL foam-tile field surface.
//
//  HOW TO MEASURE:
//    1. Run "Calibration Tool" opmode and press BTN_X (Odometry Test).
//    2. Robot drives forward for exactly 5 seconds at DRIVE_POWER.
//    3. Measure the actual distance traveled in inches with a tape measure.
//    4. New value = ODOMETRY_TEST_DURATION_MS / measured_inches
//    Example: Robot traveled 20 inches → MS_PER_INCH = 5000 / 20 = 250.0
//
//  CURRENT VALUE: 18.0f (estimated default — MUST be measured on real robot)
constexpr float MS_PER_INCH = 18.0f;
constexpr uint32_t ODOMETRY_TEST_DURATION_MS = 5000;

//  DRIVE_POWER — Forward drive power (0.0 = stopped, 1.0 = full speed).
//  Used by SmartDriveAction for all AI autonomous driving.
//  Must match the power level used when measuring MS_PER_INCH above.
constexpr float DRIVE_POWER = 0.75f;

//  GYRO_KP — Proportional correction gain for gyro heading lock during
//  straight driving. Higher = more aggressive steering corrections.
//  Increase if robot drifts sideways. Decrease if robot oscillates.
constexpr float GYRO_KP_DRIVE = 0.025f;

//  OBSTACLE_DISTANCE_CM — Distance threshold in cm at which an obstacle
//  triggers the avoidance routine. (~12 inches = 30 cm default)
constexpr float OBSTACLE_DISTANCE_CM = 30.0f;

//  STALL_CURRENT_A — Motor current (Amps) spike that indicates physical
//  contact/stall with another robot or wall.
constexpr float STALL_CURRENT_A = 2.85f;


// ─────────────────────────────────────────────────────────────────────────────
//  SECTION B: Arm Servo Angles (degrees, 0 ─ 180)
// ─────────────────────────────────────────────────────────────────────────────
//
//  HOW TO MEASURE:
//    Run "Calibration Tool" opmode, select Arm Sweep (BTN_A).
//    D-Pad UP/DOWN moves arm in 5° steps. Hold the robot over each target
//    and record the angle shown on the Controller LCD where the mechanism
//    reaches the correct physical position.

//  Floor pickup position — arm is fully lowered to the foam tile surface
//  so the gripper clears ground-level Cores at Spike Marks.
//  EXPECTED: Robot arm tip touches / hovers 5mm above the mat.
constexpr float ARM_ANGLE_FLOOR  = 10.0f;   // degrees — TUNE on real robot

//  Carry position — arm is raised just enough to lift a Core clear of the
//  floor while driving, without hitting any field structures.
constexpr float ARM_ANGLE_CARRY  = 35.0f;   // degrees — TUNE on real robot

//  High Pole scoring position — arm tip is at 130 mm above floor,
//  aligned to thread a Core down onto the High Pole.
constexpr float ARM_ANGLE_HIGH_POLE  = 90.0f;   // degrees — TUNE on real robot

//  Apex Pole scoring position — arm tip is at 365 mm above floor,
//  aligned to thread a Core onto the Apex Pole at the top of the Space Station.
constexpr float ARM_ANGLE_APEX_POLE  = 160.0f;  // degrees — TUNE on real robot

//  Arm servo start angle (position at power-on / init).
constexpr float ARM_ANGLE_START  = 10.0f;


// ─────────────────────────────────────────────────────────────────────────────
//  SECTION C: Gripper Servo Angles (degrees, 0 ─ 180)
// ─────────────────────────────────────────────────────────────────────────────
//
//  HOW TO MEASURE:
//    Run "Calibration Tool" opmode, select Gripper Test (BTN_Y).
//    D-Pad RIGHT = close, D-Pad LEFT = open. Find the angle where:
//    OPEN:  Core slides in and out of gripper freely with no contact.
//    CLOSE: Core is firmly clamped. Servo does NOT buzz/hum (stall).
//    If servo buzzes when clamped → reduce CLOSE angle by 5° steps.

//  Fully open — Core can enter/exit freely.
constexpr float GRIPPER_ANGLE_OPEN  = 20.0f;   // degrees — TUNE on real robot

//  Fully clamped — Core is held securely.
//  WARNING: Do NOT set so high that the servo stalls against the Core.
constexpr float GRIPPER_ANGLE_CLOSE = 95.0f;   // degrees — TUNE on real robot

//  Gripper servo start angle at power-on.
constexpr float GRIPPER_ANGLE_START = 20.0f;


// ─────────────────────────────────────────────────────────────────────────────
//  SECTION D: Cargo Bay Servo Angles (degrees, 0 ─ 180)
// ─────────────────────────────────────────────────────────────────────────────
//
//  HOW TO MEASURE:
//    Run "Calibration Tool" opmode, select Cargo Bay Test (BTN_RB).
//    D-Pad RIGHT = open door, D-Pad LEFT = close door.
//    CLOSED: Door is fully latched, Payload cannot fall out during driving.
//    OPEN:   Door swings fully clear so Payload drops cleanly into Drop Zone.

//  Cargo Bay door fully closed — Payload secured for transit.
constexpr float CARGOBAY_ANGLE_CLOSED = 0.0f;   // degrees — TUNE on real robot

//  Cargo Bay door fully open — Payload drops freely into Drop Zone.
constexpr float CARGOBAY_ANGLE_OPEN   = 90.0f;  // degrees — TUNE on real robot

//  Cargo Bay clamp angle — grips Payload from Source Zone.
constexpr float CARGOBAY_ANGLE_CLAMP  = 25.0f;  // degrees — TUNE on real robot

//  Cargo Bay start angle at power-on.
constexpr float CARGOBAY_ANGLE_START  = 0.0f;

//  Time (ms) to hold Cargo Bay open before closing after deployment.
constexpr uint32_t CARGOBAY_DEPLOY_HOLD_MS = 600;


// ─────────────────────────────────────────────────────────────────────────────
//  SECTION E: Vault Gate Servo Angles (degrees, 0 ─ 180)
// ─────────────────────────────────────────────────────────────────────────────
//
//  HOW TO MEASURE:
//    Run "Calibration Tool" opmode, select Vault Gate Test (BTN_LB).
//    D-Pad RIGHT = open gate, D-Pad LEFT = close gate.
//    CLOSED: Gate blocks the Vault opening completely.
//    OPEN:   Gate swings fully clear so Golden Core can exit the Vault.

//  Gate fully closed — Vault sealed.
constexpr float VAULT_ANGLE_CLOSED = 0.0f;   // degrees — TUNE on real robot

//  Gate fully open — Golden Core can exit.
constexpr float VAULT_ANGLE_OPEN   = 90.0f;  // degrees — TUNE on real robot

//  Vault gate start angle at power-on.
constexpr float VAULT_ANGLE_START  = 0.0f;

//  Time (ms) to hold Vault gate open for Core intake.
constexpr uint32_t VAULT_INTAKE_HOLD_MS = 400;


// ─────────────────────────────────────────────────────────────────────────────
//  SECTION F: Space Station Auto-Balance Tuning
// ─────────────────────────────────────────────────────────────────────────────

//  BALANCE_PITCH_TOLERANCE_DEG — How close to 0° pitch the platform must
//  be before the robot stops nudging. Smaller = more precise but may hunt.
//  Larger = faster but may not score the Balance bonus.
constexpr float BALANCE_PITCH_TOLERANCE_DEG = 2.5f;

//  BALANCE_CREEP_POWER — Drive power for micro-corrections on the platform.
//  Too high = overshoots. Too low = can't overcome platform friction.
constexpr float BALANCE_CREEP_POWER = 0.22f;

//  BALANCE_TIMEOUT_MS — Maximum time (ms) to attempt balance before giving up.
constexpr uint32_t BALANCE_TIMEOUT_MS = 6000;


// ─────────────────────────────────────────────────────────────────────────────
//  SECTION G: Nominal Battery Voltage
// ─────────────────────────────────────────────────────────────────────────────

//  NOMINAL_VOLTAGE_V — The "full charge" battery voltage at which MS_PER_INCH
//  was measured. Drive PWM is scaled proportionally as the battery drains.
//  Example: If battery reads 11.2V and nominal is 12.0V → scale = 12.0/11.2 = 1.07x PWM.
constexpr float NOMINAL_VOLTAGE_V = 12.0f;
