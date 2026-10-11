#include "AIDriverCommon.h"

// ============================================================================
//  NRL 2026 HYPERDRIVE — AI Driver: Red Field Arena
// ============================================================================
//  Fully autonomous self-driving agent for the RED SQUAD arena position.
//
//  Autonomously executes without human intervention:
//    1. Calibration lock against the Red perimeter wall
//    2. Spike Mark Core retrieval & High Pole scoring (+3 pts)
//    3. Source Zone Payload pickup & Drop Zone scoring (+10 pts, unlocks early Starburst)
//    4. Vault Golden Core retrieval & Star Pole scoring (+7 pts + Star Bonus)
//    5. Level Up: Climbs onto Red Space Station, auto-balances level (+15 pts),
//       and scores on the Apex Pole (+9 pts).
// ============================================================================

class AIDriverRedField : public NRLOpMode {
public:
    void init() override {
        AIDriver::initAllHardware();
        userLed.setSolid(255, 0, 0); // Solid Red: Red Squad Initialized
    }

    void start() override {
        // Queue the entire autonomous match plan to run once from start()
        runAction(sequential({
            // ── Phase 0: Gyro Stabilization ───────────────────────────────
            driveActions.waitForHeadingReady(),
            instant([] { userLed.setBlink(255, 0, 0, 300); }),

            // ── Phase 1: Spike Mark Red Core Pickup ───────────────────────
            AIDriver::armFloor(),
            AIDriver::openGripper(),
            driveActions.driveInches(26), // Drive from Start Zone to Spike Mark 1
            AIDriver::closeGripper(),     // Clamp Red Core
            sleep_ms(350),
            AIDriver::armCarry(),

            // ── Phase 2: High Pole Scoring ────────────────────────────────
            driveActions.driveInches(14), // Approach Red High Pole
            AIDriver::armHighPole(),      // Elevate to 130mm
            sleep_ms(450),
            AIDriver::openGripper(),      // Release onto pole (+3 pts)
            sleep_ms(300),
            AIDriver::armCarry(),
            driveActions.driveInches(-8), // Back off from pole

            // ── Phase 3: Payload Delivery (Early Starburst Unlock) ────────
            driveActions.turn(-90),       // Turn toward Red Source Zone
            driveActions.driveInches(28), // Arrive at Source Zone
            instant([] { cargoBay.setPosition(25.0f); }), // Clamp Payload in Cargo Bay (R9.4)
            sleep_ms(300),
            driveActions.driveInches(-10),// Back up from wall
            driveActions.turn(180),       // Rotate toward Red Drop Zone
            driveActions.driveInches(44), // Drive straight into Drop Zone
            AIDriver::deployPayload(),    // Release Payload (+10 pts, unlocks early Starburst!)
            sleep_ms(300),

            // ── Phase 4: Vault & Star Pole Golden Core ────────────────────
            driveActions.driveInches(-14),
            driveActions.turn(-90),       // Face Red Vault
            driveActions.driveInches(22), // Reach Vault opening
            AIDriver::openVaultGate(),    // Open intake gate
            sleep_ms(400),
            AIDriver::closeVaultGate(),
            driveActions.turn(45),        // Turn toward Red Star Pole
            driveActions.driveInches(26),
            AIDriver::armHighPole(),      // Align over Star Pole
            AIDriver::openGripper(),      // Score Golden Core (+7 pts)
            AIDriver::armCarry(),

            // ── Phase 5: Level Up End Game — Space Station & Apex Pole ────
            driveActions.turn(45),        // Turn toward Red Space Station ramp
            driveActions.driveInches(32), // Climb onto Space Station tilting deck
            AIDriver::autoBalanceSpaceStation(5500), // Active IMU leveling for 15-pt Balance!
            AIDriver::armApexPole(),      // Elevate to Apex Pole (365mm)
            sleep_ms(500),
            AIDriver::openGripper(),      // Score on Apex Pole (+9 pts)
            sleep_ms(300),
            AIDriver::armCarry(),
            driveActions.stopAll(),       // Lock brakes for match finish
            instant([] { userLed.setSolid(0, 255, 0); }) // Green: Routine Complete!
        }));
    }

    void loop() override {
        // 100 Hz Telemetry streamed to the handheld Controller screen
        telemetry.addData("Arena", "RED SQUAD");
        telemetry.addData("Driver", "SELF-DRIVING");
        telemetry.addData("Status", isActionRunning() ? "EXECUTING" : "FINISHED");
        telemetry.addData("Heading", NRLComms::getHeading());
        telemetry.addData("Batt(V)", power.getBatteryVoltage());
    }

    void stop() override {
        AIDriver::stopAllHardware();
    }
};

// Register this OpMode as an AUTO OpMode for the Red Arena
REGISTER_OPMODE(AIDriverRedField, "AI Driver: Red", AUTO);
