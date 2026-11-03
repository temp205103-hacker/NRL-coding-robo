#include "AIDriverCommon.h"

// ============================================================================
//  NRL 2026 HYPERDRIVE — AI Driver: Autonomous Skill Challenge
// ============================================================================
//  Dedicated self-driving AI agent for the solo Autonomous Skill Challenge
//  (Sections 1.3.2 & 6.8.4 of the HYPERDRIVE Game Manual).
//
//  Executes an uninterrupted, high-yield autonomous path:
//    1. Auto gyro calibration
//    2. Spike Mark multi-Core clearing & High Pole stacking (+6 pts)
//    3. Source Zone Payload retrieve & Drop Zone score (+10 pts, unlocks Starburst)
//    4. Vault Golden Core retrieve & Star Pole alignment (+7 pts + Star Bonus)
//    5. Endgame Level Up: Space Station climb, active IMU balance (+15 pts),
//       and Apex Pole score (+9 pts).
//
//  INCLUDES: Smart Obstacle & Wall Collision Avoidance (Halts or dynamically reroutes
//  if unexpected field debris or misalignment occurs).
// ============================================================================

class AIDriverSkillsChallenge : public AIDriverMode {
public:
    void init() override {
        AIDriver::initAllHardware();
        userLed.setSolid(255, 215, 0); // Gold LED: Skills Challenge Initialized
    }

    void start() override {
        startAIPhases({
            // ── Phase 0: Gyro Stabilization ───────────────────────────────
            {
                driveActions.waitForHeadingReady(),
                instant([] { userLed.setBlink(255, 215, 0, 250); })
            },

            // ── Phase 1: Spike Mark 1 & High Pole Stack 1 ─────────────────
            {
                AIDriver::armFloor(),
                AIDriver::openGripper(),
                AIDriver::smartDriveInches(26, 35.0f),
                AIDriver::closeGripper(),
                sleep_ms(300),
                AIDriver::armCarry(),
                AIDriver::smartDriveInches(14, 30.0f),
                AIDriver::armHighPole(),
                sleep_ms(400),
                AIDriver::openGripper(),
                sleep_ms(250),
                AIDriver::armCarry(),
                AIDriver::smartDriveInches(-10, -30.0f)
            },

            // ── Phase 2: Spike Mark 2 & High Pole Stack 2 ─────────────────
            {
                driveActions.turn(35),
                AIDriver::smartDriveInches(18, 30.0f),
                AIDriver::armFloor(),
                AIDriver::closeGripper(),
                sleep_ms(300),
                AIDriver::armCarry(),
                driveActions.turn(-35),
                AIDriver::smartDriveInches(12, 30.0f),
                AIDriver::armHighPole(),
                sleep_ms(400),
                AIDriver::openGripper(),
                sleep_ms(250),
                AIDriver::armCarry(),
                AIDriver::smartDriveInches(-8, -30.0f)
            },

            // ── Phase 3: Payload Retrieve & Drop Zone Placement ───────────
            {
                driveActions.turn(-90),
                AIDriver::smartDriveInches(28, 35.0f),
                instant([] { cargoBay.setPosition(25.0f); }),
                sleep_ms(300),
                AIDriver::smartDriveInches(-10, -30.0f),
                driveActions.turn(180),
                AIDriver::smartDriveInches(44, 35.0f),
                AIDriver::deployPayload(),
                sleep_ms(300)
            },

            // ── Phase 4: Vault Retrieval & Star Pole Scoring ──────────────
            {
                AIDriver::smartDriveInches(-14, -30.0f),
                driveActions.turn(-90),
                AIDriver::smartDriveInches(22, 30.0f),
                AIDriver::openVaultGate(),
                sleep_ms(400),
                AIDriver::closeVaultGate(),
                driveActions.turn(45),
                AIDriver::smartDriveInches(26, 35.0f),
                AIDriver::armHighPole(),
                AIDriver::openGripper(),
                AIDriver::armCarry()
            },

            // ── Phase 5: Space Station Balance & Apex Pole Finish ─────────
            {
                driveActions.turn(45),
                AIDriver::smartDriveInches(34, 30.0f),
                AIDriver::autoBalanceSpaceStation(6000),
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
        // Stream telemetry to Controller display
        telemetry.addData("Arena", "SKILLS SOLO");
        telemetry.addData("Driver", isManualMode() ? "MANUAL" : "SELF-DRIVING AI");
        telemetry.addData("AI Phase", currentAIPhase());
        telemetry.addData("Total Phases", aiPhaseCount());
        telemetry.addData("Override", isManualMode() ? "A=RESUME NEXT PHASE" : "CIRCLE STICK=MANUAL");
        telemetry.addData("Obstacle", obstacleDetector.isObstacleAhead() ? "AVOIDING" : "CLEAR");
        telemetry.addData("Dist(cm)", obstacleDetector.getFrontDistanceCm());
        telemetry.addData("Heading", NRLComms::getHeading());
        telemetry.addData("Batt(V)", power.getBatteryVoltage());
    }

    void stop() override {
        AIDriver::stopAllHardware();
    }
};

REGISTER_OPMODE(AIDriverSkillsChallenge, "AI Driver: Skills", AUTO);
