#include "NRL.h"
#include "StarMapPattern.h"

int starMapPattern = STAR_MAP_PATTERN_GOLD_SQUAD_GOLD;

class StarMapSelector : public NRLOpMode {
private:
    bool _confirmed = false;
    bool _oledAvailable = false;
    uint32_t _lastOledRefreshMs = 0;

public:
    void init() override {
        starMapPattern = STAR_MAP_PATTERN_GOLD_SQUAD_GOLD;
        _confirmed = false;
        _oledAvailable = botOled.isReady();
        userLed.setSolid(255, 180, 0);
    }

    void loop() override {
        if (!_confirmed) {
            if (gamepad1.justPressed(BTN_DPAD_UP)) {
                starMapPattern = (starMapPattern == STAR_MAP_PATTERN_GOLD_SQUAD_GOLD)
                    ? STAR_MAP_PATTERN_GOLD_GOLD_SQUAD
                    : starMapPattern - 1;
            } else if (gamepad1.justPressed(BTN_DPAD_DOWN)) {
                starMapPattern = (starMapPattern == STAR_MAP_PATTERN_GOLD_GOLD_SQUAD)
                    ? STAR_MAP_PATTERN_GOLD_SQUAD_GOLD
                    : starMapPattern + 1;
            }

            if (gamepad1.justPressed(BTN_A)) {
                _confirmed = true;
                userLed.setSolid(0, 255, 0);
            }
        }

        telemetry.addData("Star Map", starMapPatternName(starMapPattern));
        telemetry.addData("Setup", _confirmed ? "CONFIRMED" : "DPAD SELECT A OK");
        telemetry.addData("Next", _confirmed ? "STOP; SELECT AUTO" : "A CONFIRMS");
        updateDisplay(millis());
    }

    void stop() override {
        userLed.setOff();
    }

private:
    void updateDisplay(uint32_t now) {
        if (!_oledAvailable || now - _lastOledRefreshMs < 250) return;
        _lastOledRefreshMs = now;

        botOled.clearDisplay();
        botOled.setTextSize(1);
        botOled.setCursor(0, 0);
        botOled.print("STAR MAP SETUP");
        botOled.setCursor(0, 16);
        botOled.print(starMapPattern == STAR_MAP_PATTERN_GOLD_SQUAD_GOLD ? ">" : " ");
        botOled.print("1 GOLD-SQUAD-GOLD");
        botOled.setCursor(0, 28);
        botOled.print(starMapPattern == STAR_MAP_PATTERN_SQUAD_GOLD_GOLD ? ">" : " ");
        botOled.print("2 SQUAD-GOLD-GOLD");
        botOled.setCursor(0, 40);
        botOled.print(starMapPattern == STAR_MAP_PATTERN_GOLD_GOLD_SQUAD ? ">" : " ");
        botOled.print("3 GOLD-GOLD-SQUAD");
        botOled.setCursor(0, 54);
        botOled.print(_confirmed ? "CONFIRMED - STOP" : "UP/DN SELECT  A OK");
        botOled.display();
    }
};

REGISTER_OPMODE(StarMapSelector, "Star Map Setup", TELEOP);
