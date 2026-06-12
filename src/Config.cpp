#include "Config.h"

// ============================================================
//  Config.cpp : タワー・敵のステータス定義(データ駆動設計)
// ============================================================

const std::array<TowerDef, 6>& towerDefs() {
    static const std::array<TowerDef, 6> defs = {{
        //  name      desc              cost dmg  range  cd   splash slow  pierce color            beam  mine
        { "Gunner", "Rapid fire",      100,  8.f, 110.f, 0.4f,  0.f, false, false, { 29,158,117}, false, false },
        { "Cannon", "Splash dmg",      250, 24.f, 130.f, 1.3f, 60.f, false, false, {216, 90, 48}, false, false },
        { "Ice",    "Slows enemies",   150,  3.f, 105.f, 0.8f,  0.f, true,  false, { 55,138,221}, false, false },
        { "Sniper", "Anti-armor",      300, 65.f, 220.f, 2.0f,  0.f, false, true,  {127,119,221}, false, false },
        { "Beam",   "Aim with mouse",  350, 40.f,   0.f, 0.0f,  0.f, false, true,  {255, 92,138}, true,  false },
        { "Mine",   "Click on beat!",  200,  0.f,   0.f, 0.0f,  0.f, false, false, {245,196, 80}, false, true  },
    }};
    return defs;
}

const EnemyDef& enemyDef(EnemyKind k) {
    //                                hpMul speed radius rewardMul armored color
    static const EnemyDef normal  = {  1.0f,  60.f, 10.f,  1.0f, false, {226, 75, 74} };
    static const EnemyDef fast    = {  0.55f,115.f,  8.f,  1.2f, false, {239,159, 39} };
    static const EnemyDef tank    = {  3.4f,  38.f, 14.f,  3.0f, false, {138, 74, 42} };
    static const EnemyDef armored = {  1.7f,  55.f, 11.f,  2.5f, true,  { 95, 94, 90} };
    static const EnemyDef boss    = { 20.0f,  26.f, 20.f, 12.0f, false, {110, 60,150} };
    switch (k) {
        case EnemyKind::Fast:    return fast;
        case EnemyKind::Tank:    return tank;
        case EnemyKind::Armored: return armored;
        case EnemyKind::Boss:    return boss;
        default:                 return normal;
    }
}
