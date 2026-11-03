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

class AIDriverBlueField : public AIDriverMode {
public:
    void init() override {
        AIDriver::initAllHardware();
        userLed.setSolid(0, 0, 255); // Solid Blue: Blue Squad Initialized
    }

    void start() override {
        startAIPhases({
            // ── Phase 0: Gyro Stabilization ───────────────────────────────
            {
                driveActions.waitForHeadingReady(),
                instant([] { userLed.setBlink(0, 0, 255, 300); })
            },

            // ── Phase 1: Spike Mark Pickup & High Pole Score ─────────────
            {
                AIDriver::armFloor(),
                AIDriver::openGripper(),
                AIDriver::smartDriveInches(26, -35.0f),
                AIDriver::closeGripper(),
                sleep_ms(350),
                AIDriver::armCarry(),
                AIDriver::smartDriveInches(14, -30.0f),
                AIDriver::armHighPole(),
                sleep_ms(450),
                AIDriver::openGripper(),
                sleep_ms(300),
                AIDriver::armCarry(),
                AIDriver::smartDriveInches(-8, 30.0f)
            },

            // ── Phase 2: Payload Delivery (Early Starburst Unlock) ───────
            {
                driveActions.turn(90),
                AIDriver::smartDriveInches(28, -35.0f),
                instant([] { cargoBay.setPosition(25.0f); }),
                sleep_ms(300),
                AIDriver::smartDriveInches(-10, 30.0f),
                driveActions.turn(-180),
                AIDriver::smartDriveInches(44, -35.0f),
                AIDriver::deployPayload(),
                sleep_ms(300)
            },
            { AIDriver::starMapScoringActions(90.0f) },

            // ── Phase 4: Space Station & Apex Pole ────────────────────────
            {
                driveActions.turn(-45),
                AIDriver::smartDriveInches(32, -30.0f),
                AIDriver::autoBalanceSpaceStation(5500),
                AIDriver::armApexPole(),
                sleep_ms(500),
                AIDriver::openGripper(),
                sleep_ms(300),
                AIDriver::armCarry(),
                driveActions.stopAll(),
                instant([] { userLed.setSolid(0, 255, 0); })
            }
        });
    }

    void loop() override {
        updateAIControl();
        // Stream telemetry to the handheld Controller screen
        telemetry.addData("Arena", "BLUE SQUAD");
        telemetry.addData("Driver", isManualMode() ? "MANUAL" : "SELF-DRIVING AI");
        telemetry.addData("AI Phase", currentAIPhase());
        telemetry.addData("Total Phases", aiPhaseCount());
        telemetry.addData("Override", isManualMode() ? "A=RESUME NEXT PHASE" : "CIRCLE STICK=MANUAL");
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
