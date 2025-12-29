#ifndef GAMEMODESTRUCT_H
#define GAMEMODESTRUCT_H

#include <Util/Enums.h>
#include <Util/Types.h>

typedef struct gamemode_vars
{
    uint8_t      score[2];
    uint8_t      score_limit;
    intel_flag_t intel_flags;
    vector3f_t   intel[2];
    vector3f_t   base[2];
    uint8_t      player_intel_team[2];
    uint8_t      intel_held[2];
    // water damage
    uint8_t      water_damage_enabled;
    uint8_t      water_damage;
} gamemode_vars_t;

#endif
