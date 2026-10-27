#pragma once

#include <stddef.h>
#include <stdint.h>

enum class StarMapCore : uint8_t {
    GOLD,
    SQUAD
};

constexpr int STAR_MAP_PATTERN_GOLD_SQUAD_GOLD = 1;
constexpr int STAR_MAP_PATTERN_SQUAD_GOLD_GOLD = 2;
constexpr int STAR_MAP_PATTERN_GOLD_GOLD_SQUAD = 3;
constexpr size_t STAR_MAP_CORE_COUNT = 3;

extern int starMapPattern;

inline StarMapCore starMapCoreAt(int pattern, size_t index) {
    static constexpr StarMapCore patterns[3][STAR_MAP_CORE_COUNT] = {
        { StarMapCore::GOLD,  StarMapCore::SQUAD, StarMapCore::GOLD  },
        { StarMapCore::SQUAD, StarMapCore::GOLD,  StarMapCore::GOLD  },
        { StarMapCore::GOLD,  StarMapCore::GOLD,  StarMapCore::SQUAD }
    };

    if (index >= STAR_MAP_CORE_COUNT) return StarMapCore::GOLD;
    if (pattern < STAR_MAP_PATTERN_GOLD_SQUAD_GOLD ||
        pattern > STAR_MAP_PATTERN_GOLD_GOLD_SQUAD) {
        pattern = STAR_MAP_PATTERN_GOLD_SQUAD_GOLD;
    }
    return patterns[pattern - 1][index];
}

inline const char* starMapPatternName(int pattern) {
    switch (pattern) {
        case STAR_MAP_PATTERN_SQUAD_GOLD_GOLD: return "SQUAD-GOLD-GOLD";
        case STAR_MAP_PATTERN_GOLD_GOLD_SQUAD: return "GOLD-GOLD-SQUAD";
        default: return "GOLD-SQUAD-GOLD";
    }
}
