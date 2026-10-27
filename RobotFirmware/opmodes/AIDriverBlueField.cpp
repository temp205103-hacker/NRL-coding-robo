#include "AIDriverCommon.h"

// ============================================================================
//  NRL 2026 HYPERDRIVE — AI Driver: Blue Field Arena (With Collision Avoidance)
// ============================================================================
//  Fully autonomous self-driving agent for the BLUE SQUAD arena position.
//
//  Autonomously executes without human intervention:
//    1. Calibration lock against the Blue perimeter wall
//    2. Spike Mark Blue Core retrieval & High Pole scoring (+3 pts)
//    3. Source Zone Payload pickup & Drop Zone scoring (+10 pts, unlocks early Starburst)
//    4. Vault Golden Core retrieval & Star Pole scoring (+7 pts + Star Bonus)
//    5. Level Up: Climbs onto Blue Space Station, auto-balances level (+15 pts),
//       and scores on the Apex Pole (+9 pts).
//
//  NEW FEATURE: Real-time dynamic obstacle detection & intelligent path rerouting.
//  If opponent robots enter the trajectory, the robot halts to prevent fouls (G4.8),
//  waits for passing robots, or dynamically steers around them to complete tasks!
// ============================================================================

class AIDriverBlueField : public NRLOpMode {
public:
    void init() override {
        AIDriver::initAllHardware();
        userLed.setSolid(0, 0, 255); // Solid Blue: Blue Squad Initialized
    }

    void start() override {
        std::vector<ActionPtr> matchPlan = {
            // ── Phase 0: Gyro Stabilization ───────────────────────────────
            driveActions.waitForHeadingReady(),
            instant([] { userLed.setBlink(0, 0, 255, 300); }),

            // ── Phase 1: Spike Mark Blue Core Pickup ──────────────────────
            AIDriver::armFloor(),
            AIDriver::openGripper(),
            AIDriver::smartDriveInches(26, -35.0f), // Smart drive: avoids crossing bots
            AIDriver::closeGripper(),               // Clamp Blue Core
            sleep_ms(350),
            AIDriver::armCarry(),

            // ── Phase 2: High Pole Scoring ────────────────────────────────
            AIDriver::smartDriveInches(14, -30.0f), // Approach Blue High Pole safely
            AIDriver::armHighPole(),                // Elevate to 130mm
            sleep_ms(450),
            AIDriver::openGripper(),                // Release onto pole (+3 pts)
            sleep_ms(300),
            AIDriver::armCarry(),
            AIDriver::smartDriveInches(-8, 30.0f),

            // ── Phase 3: Payload Delivery (Early Starburst Unlock) ────────
            driveActions.turn(90),                 // Turn toward Blue Source Zone
            AIDriver::smartDriveInches(28, -35.0f),// Arrive at Source Zone
            instant([] { cargoBay.setPosition(25.0f); }), // Clamp Payload in Cargo Bay
            sleep_ms(300),
            AIDriver::smartDriveInches(-10, 30.0f),
            driveActions.turn(-180),               // Rotate toward Blue Drop Zone
            AIDriver::smartDriveInches(44, -35.0f),// Traverse to Drop Zone with avoidance
            AIDriver::deployPayload(),              // Release Payload (+10 pts, unlocks Starburst!)
            sleep_ms(300)
        };

        matchPlan.push_back(AIDriver::starMapScoringActions(90.0f));

        const std::vector<ActionPtr> endgamePlan = {
            // ── Phase 5: Level Up End Game — Space Station & Apex Pole ────
            driveActions.turn(-45),                 // Turn toward Blue Space Station ramp
            AIDriver::smartDriveInches(32, -30.0f),// Climb onto Space Station tilting deck
            AIDriver::autoBalanceSpaceStation(5500), // Active IMU leveling for 15-pt Balance!
            AIDriver::armApexPole(),               // Elevate to Apex Pole (365mm)
            sleep_ms(500),
            AIDriver::openGripper(),               // Score on Apex Pole (+9 pts)
            sleep_ms(300),
            AIDriver::armCarry(),
            driveActions.stopAll(),                // Lock brakes for match finish
            instant([] { userLed.setSolid(0, 255, 0); }) // Green: Routine Complete!
        };
        matchPlan.insert(matchPlan.end(), endgamePlan.begin(), endgamePlan.end());
        runAction(sequential(matchPlan));
    }

    void loop() override {
        // Stream telemetry to the handheld Controller screen
        telemetry.addData("Arena", "BLUE SQUAD");
        telemetry.addData("Driver", "SELF-DRIVING AI");
        telemetry.addData("Obstacle", obstacleDetector.isObstacleAhead() ? "AVOIDING" : "CLEAR");
        telemetry.addData("StarMap", starMapPatternName(starMapPattern));
        telemetry.addData("Dist(cm)", obstacleDetector.getFrontDistanceCm());
        telemetry.addData("Heading", NRLComms::getHeading());
        telemetry.addData("Batt(V)", power.getBatteryVoltage());
    }

    void stop() override {
        AIDriver::stopAllHardware();
    }
};

REGISTER_OPMODE(AIDriverBlueField, "AI Driver: Blue", AUTO);
