#include "Game.h"
#include <algorithm>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
static Game* g_game = nullptr;
static void em_main_loop() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) g_game->handleEventPublic(e);
    g_game->tickPublic();
    g_game->render();
}
#endif
#include <cmath>
#include <cstdio>

// ============================================================
//  Game.cpp : リズム連動タワーディフェンス本体
//  音楽の拍クロック(AudioEngine)を唯一の時間基準として、
//  画面の脈動・キャラの跳ね・ビーム発射・金鉱の判定を同期させる
// ============================================================

namespace {
const int kCellWaypoints[][2] = { {-1,2}, {5,2}, {5,6}, {10,6}, {10,2}, {14,2} };
const char* kChordNames[4] = { "Am", "F", "C", "G" };

const char* kFontPaths[] = {
    "assets/font.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/Library/Fonts/Arial.ttf",
};

float dist(const Vec2& a, const Vec2& b) { return std::hypot(a.x - b.x, a.y - b.y); }

// 点pと線分abの距離(ビームの当たり判定)
float segDist(Vec2 a, Vec2 b, Vec2 p) {
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float len2 = vx * vx + vy * vy;
    float t = len2 > 0.f ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / len2 : 0.f;
    t = std::clamp(t, 0.f, 1.f);
    return std::hypot(a.x + vx * t - p.x, a.y + vy * t - p.y);
}

uint32_t g_rng = 0xC0FFEEu;
float frand() { g_rng = g_rng * 1664525u + 1013904223u; return (g_rng >> 8) / 16777216.f; }

// ---- UIレイアウト ----
SDL_FRect shopCardRect(int i) {
    const float w = (FIELD_W - 7 * 8) / 6.f;
    return { 8 + i * (w + 8), float(HUD_H + FIELD_H + 8), w, 74.f };
}
const SDL_FRect kBtnWave  { 8,   7, 110, 30 };
const SDL_FRect kBtnSpeed { 126, 7,  78, 30 };
const SDL_FRect kBtnAuto  { 212, 7,  82, 30 };
SDL_FRect btnUpgrade() { return { FIELD_W - 230.f, float(HUD_H + FIELD_H + 95), 130, 30 }; }
SDL_FRect btnSell()    { return { FIELD_W - 92.f,  float(HUD_H + FIELD_H + 95),  84, 30 }; }
SDL_FRect btnRestart() { return { FIELD_W / 2.f - 80, HUD_H + FIELD_H / 2.f + 20, 160, 36 }; }
bool inRect(int x, int y, const SDL_FRect& r) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}
} // namespace

// ============================================================
//  音楽クロック : 全ての演出の時間基準
// ============================================================

double Game::beatPhaseNow() const {
    return audio_.ready() ? audio_.beatPhase()
                          : fallbackBeats_ - std::floor(fallbackBeats_);
}
int64_t Game::beatIdxNow() const {
    return audio_.ready() ? audio_.beatIndex() : int64_t(fallbackBeats_);
}
int Game::chordIdxNow() const {
    return audio_.ready() ? audio_.chordIndex() : int((beatIdxNow() / 4) % 4);
}
// 拍頭で1.0、その後3乗カーブで0へ減衰 → "ドンッ"という脈動の形
float Game::pulseNow() const {
    const float p = float(beatPhaseNow());
    return (1.f - p) * (1.f - p) * (1.f - p);
}

// ============================================================
//  初期化 / 終了
// ============================================================

bool Game::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::printf("SDL_Init error: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() != 0) return false;
    window_ = SDL_CreateWindow("Rhythm Tower Defense (C++ / SDL2)",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!window_) return false;
    renderer_ = SDL_CreateRenderer(window_, -1,
                                   SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) return false;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (const char* path : kFontPaths) {
        fontS_ = TTF_OpenFont(path, 13);
        if (fontS_) { fontM_ = TTF_OpenFont(path, 16); fontL_ = TTF_OpenFont(path, 26); break; }
    }
    if (!fontS_ || !fontM_ || !fontL_) {
        std::printf("Font not found. Place a .ttf at assets/font.ttf\n");
        return false;
    }

    for (auto& wp : kCellWaypoints)
        waypoints_.push_back({ (wp[0] + 0.5f) * CELL, (wp[1] + 0.5f) * CELL });
    const int n = int(sizeof(kCellWaypoints) / sizeof(kCellWaypoints[0]));
    for (int i = 0; i + 1 < n; ++i) {
        int ax = kCellWaypoints[i][0],   ay = kCellWaypoints[i][1];
        int bx = kCellWaypoints[i+1][0], by = kCellWaypoints[i+1][1];
        if (ay == by) {
            for (int c = std::max(0, std::min(ax, bx));
                 c <= std::min(COLS - 1, std::max(ax, bx)); ++c)
                pathCells_.insert(ay * COLS + c);
        } else {
            for (int r = std::min(ay, by); r <= std::max(ay, by); ++r)
                pathCells_.insert(r * COLS + ax);
        }
    }

    if (!audio_.init()) std::printf("Audio device not available (running silent)\n");
    resetGame();
    return true;
}

void Game::shutdown() {
    audio_.shutdown();
    if (fontS_) TTF_CloseFont(fontS_);
    if (fontM_) TTF_CloseFont(fontM_);
    if (fontL_) TTF_CloseFont(fontL_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    TTF_Quit();
    SDL_Quit();
}

void Game::resetGame() {
    money_ = START_MONEY; lives_ = START_LIVES; wave_ = 0;
    enemies_.clear(); spawnQueue_.clear(); spawnIndex_ = 0;
    towers_.clear(); projectiles_.clear(); particles_.clear();
    waveActive_ = false; gameOver_ = false;
    selectedShop_ = -1; selectedTower_ = -1;
    nextEnemyId_ = 1; combo_ = 0; shakeAmp_ = 0.f;
    audio_.setBattle(false);
    setMessage("Build towers & feel the beat!  Try clicking a Mine in rhythm.");
}

// ============================================================
//  メインループ
// ============================================================

void Game::run() {
#ifdef __EMSCRIPTEN__
    g_game = this;
    emscripten_set_main_loop(em_main_loop, 0, 1);
#else
    uint64_t prev = SDL_GetPerformanceCounter();
    const float freq = float(SDL_GetPerformanceFrequency());
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = std::min((now - prev) / freq, 0.05f);
        prev = now;
        timeSec_ += dt;
        if (!gameOver_) update(dt * speedMult_);
        render();
    }
#endif
}

void Game::handleEventPublic(const SDL_Event& e) { handleEvent(e); }
void Game::tickPublic() {
    uint64_t now = SDL_GetPerformanceCounter();
    static uint64_t prev = now;
    float dt = std::min((now - prev) / float(SDL_GetPerformanceFrequency()), 0.05f);
    prev = now;
    timeSec_ += dt;
    if (!gameOver_) update(dt * speedMult_);
}

// ============================================================
//  入力
// ============================================================

void Game::handleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_QUIT: running_ = false; break;
    case SDL_MOUSEMOTION: {
        const int fx = e.motion.x, fy = e.motion.y - HUD_H;
        mouseFx_ = std::clamp(float(fx), 0.f, float(FIELD_W));   // ビーム照準は常に追従
        mouseFy_ = std::clamp(float(fy), 0.f, float(FIELD_H));
        if (fy >= 0 && fy < FIELD_H) { hoverCol_ = fx / CELL; hoverRow_ = fy / CELL; }
        else hoverCol_ = hoverRow_ = -1;
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT) onLeftClick(e.button.x, e.button.y);
        else if (e.button.button == SDL_BUTTON_RIGHT) { selectedShop_ = -1; selectedTower_ = -1; }
        break;
    case SDL_KEYDOWN:
        if (e.key.keysym.sym == SDLK_ESCAPE) { selectedShop_ = -1; selectedTower_ = -1; }
        break;
    }
}

void Game::onLeftClick(int mx, int my) {
    if (gameOver_) { if (inRect(mx, my, btnRestart())) resetGame(); return; }
    if (inRect(mx, my, kBtnWave))  { startWave(); return; }
    if (inRect(mx, my, kBtnSpeed)) { speedMult_ = (speedMult_ == 1) ? 2 : 1; return; }
    if (inRect(mx, my, kBtnAuto))  { autoNext_ = !autoNext_; return; }

    if (selectedTower_ >= 0 && selectedTower_ < int(towers_.size())) {
        Tower& t = towers_[selectedTower_];
        if (inRect(mx, my, btnUpgrade())) {
            const int cost = upgradeCost(t);
            if (t.level >= MAX_TOWER_LEVEL)  setMessage("Already max level");
            else if (money_ < cost)          setMessage("Not enough gold");
            else {
                money_ -= cost; t.spent += cost; t.level++;
                audio_.trigger(Sfx::Coin);
                fxRing(t.pos, 30.f, { 255, 255, 255 });
            }
            return;
        }
        if (inRect(mx, my, btnSell())) {
            money_ += int(t.spent * SELL_RATE);
            towers_.erase(towers_.begin() + selectedTower_);
            selectedTower_ = -1;
            setMessage("Tower sold (60% refund)");
            return;
        }
    }

    for (int i = 0; i < int(TowerKind::COUNT); ++i) {
        if (inRect(mx, my, shopCardRect(i))) {
            selectedShop_ = (selectedShop_ == i) ? -1 : i;
            selectedTower_ = -1;
            if (selectedShop_ >= 0)
                setMessage(towerDefs()[i].isMine
                    ? "Place it, then CLICK IT ON THE BEAT for gold!"
                    : "Click an empty cell to place (not on the path)");
            return;
        }
    }

    const int fy = my - HUD_H;
    if (fy < 0 || fy >= FIELD_H || mx < 0 || mx >= FIELD_W) return;
    const int col = mx / CELL, row = fy / CELL;
    const int existing = towerIndexAt(col, row);

    if (selectedShop_ >= 0) {
        const TowerDef& def = towerDefs()[selectedShop_];
        if (isPathCell(col, row)) { setMessage("Cannot build on the path"); return; }
        if (existing >= 0)        { setMessage("Cell is occupied"); return; }
        if (money_ < def.cost)    { setMessage("Not enough gold"); selectedShop_ = -1; return; }
        money_ -= def.cost;
        Tower t;
        t.kind = TowerKind(selectedShop_);
        t.col = col; t.row = row;
        t.pos = { (col + 0.5f) * CELL, (row + 0.5f) * CELL };
        t.spent = def.cost;
        towers_.push_back(t);
        audio_.trigger(Sfx::Place);
        fxRing(t.pos, 26.f, def.color);
        if (money_ < def.cost) selectedShop_ = -1;
        return;
    }

    // 金鉱クリック → リズム判定で収入 (選択も同時に行う)
    if (existing >= 0 && towerDefs()[int(towers_[existing].kind)].isMine)
        mineClicked(towers_[existing]);
    selectedTower_ = existing;
}

// 金鉱: クリックのタイミングが拍に近いほど多くのゴールド
void Game::mineClicked(Tower& mine) {
    if (!waveActive_) {                          // 待機中は金鉱から収入なし
        audio_.trigger(Sfx::Place);
        fxText({ mine.pos.x, mine.pos.y - 24 }, "mines pay during waves only",
               { 150, 150, 150 });
        return;
    }
    const int64_t beat = beatIdxNow();
    if (beat == mine.lastMineBeat) return;       // 同じ拍の連打は無効
    mine.lastMineBeat = beat;

    const double ph = beatPhaseNow();
    const double off = std::min(ph, 1.0 - ph);   // 最寄りの拍とのズレ量
    if (off < 0.12) {                            // PERFECT
        const int gold = 5 + 5 * mine.level;
        money_ += gold;
        audio_.trigger(Sfx::CoinPerfect);
        fxCoins(mine.pos, 6);
        fxRing(mine.pos, 34.f, { 255, 235, 140 }, 0.35f);
        fxText({ mine.pos.x, mine.pos.y - 24 }, "PERFECT +" + std::to_string(gold),
               { 255, 220, 90 });
        addShake(2.5f);
    } else if (off < 0.25) {                     // GOOD
        const int gold = 2 + 2 * mine.level;
        money_ += gold;
        audio_.trigger(Sfx::Coin);
        fxCoins(mine.pos, 2);
        fxText({ mine.pos.x, mine.pos.y - 24 }, "+" + std::to_string(gold),
               { 250, 250, 250 });
    } else {                                     // MISS
        audio_.trigger(Sfx::Place);
        fxText({ mine.pos.x, mine.pos.y - 24 }, "miss...", { 150, 150, 150 });
    }
}

// ============================================================
//  ロジック
// ============================================================

void Game::buildWave(int waveNum) {
    spawnQueue_.clear();
    spawnIndex_ = 0;
    const float baseHp = WAVE_HP_BASE * std::pow(WAVE_HP_GROWTH, float(waveNum - 1));
    const int count = std::min(6 + waveNum * 2, 40);

    auto makeEnemy = [&](EnemyKind kind) {
        const EnemyDef& def = enemyDef(kind);
        Enemy e;
        e.kind = kind;
        e.maxHp = e.hp = std::round(baseHp * def.hpMul);
        e.speed = def.speed; e.radius = def.radius;
        e.reward = int(std::round((8 + waveNum) * def.rewardMul));
        e.armored = def.armored; e.color = def.color;
        return e;
    };
    for (int i = 0; i < count; ++i) {
        EnemyKind kind = EnemyKind::Normal;
        if      (waveNum >= 3 && i % 7 == 6) kind = EnemyKind::Tank;
        else if (waveNum >= 5 && i % 5 == 4) kind = EnemyKind::Armored;
        else if (waveNum >= 2 && i % 3 == 2) kind = EnemyKind::Fast;
        spawnQueue_.push_back(makeEnemy(kind));
    }
    if (waveNum % 5 == 0)                        // 5の倍数ウェーブの最後にボス
        spawnQueue_.push_back(makeEnemy(EnemyKind::Boss));
}

void Game::startWave() {
    if (waveActive_ || gameOver_) return;
    wave_++;
    buildWave(wave_);
    spawnTimer_ = 0.f;
    waveActive_ = true;
    audio_.setBattle(true);
    if (wave_ % 5 == 0)      setMessage("BOSS WAVE!  Brace yourself!");
    else if (wave_ == 5)     setMessage("Armored enemies! Only Snipers & Beams hurt them!");
    else                     setMessage("Wave " + std::to_string(wave_) + " start!");
}

TowerStats Game::statsOf(const Tower& t) const {
    const TowerDef& def = towerDefs()[int(t.kind)];
    const float lv = float(t.level - 1);
    return { def.damage   * std::pow(UPG_DAMAGE, lv),
             def.range    * std::pow(UPG_RANGE,  lv),
             def.cooldown * std::pow(UPG_RATE,   lv) };
}

int Game::upgradeCost(const Tower& t) const {
    return int(std::round(towerDefs()[int(t.kind)].cost * 0.7f * t.level / 10.f)) * 10;
}

Enemy* Game::findEnemy(int id) {
    for (auto& e : enemies_)
        if (e.id == id && !e.dead) return &e;
    return nullptr;
}

bool Game::applyDamage(Enemy& e, float dmg, bool pierce) {
    if (e.armored && !pierce) return false;
    e.hp -= dmg;
    return true;
}

void Game::killEnemy(Enemy& e) {
    e.dead = true;
    combo_++; comboTimer_ = 2.f;                 // コンボ: 2秒以内の連続撃破で加算
    int reward = e.reward + combo_ / 2;          // コンボボーナス
    const double ph = beatPhaseNow();
    const bool onBeat = std::min(ph, 1.0 - ph) < 0.15;
    if (onBeat) reward = int(reward * 1.5f);     // 拍ジャストの撃破はさらに1.5倍!
    money_ += reward;

    fxSparks(e.pos, e.kind == EnemyKind::Boss ? 16 : 6, e.color);
    fxRing(e.pos, e.radius + 10.f, { 250, 199, 117 });
    fxText({ e.pos.x, e.pos.y - e.radius - 6 },
           "+" + std::to_string(reward) + (onBeat ? " ON BEAT!" : ""),
           onBeat ? Color{ 255, 220, 90 } : Color{ 255, 255, 255 });
    if (e.kind == EnemyKind::Boss) { addShake(8.f); fxCoins(e.pos, 12); }
    else if (e.kind == EnemyKind::Tank) addShake(3.f);
    audio_.trigger(Sfx::Kill);
}

void Game::update(float dt) {
    if (!audio_.ready()) fallbackBeats_ += dt * MUSIC_BPM / 60.0;

    // ---- 拍の頭を検出してリズムイベントを発火 ----
    const int64_t beat = beatIdxNow();
    if (beat != lastBeatIdx_) { lastBeatIdx_ = beat; onBeat(beat); }

    // ビーム発射音の先行予約: 効果音は「次の16分音符」にクオンタイズされるため、
    // 発射拍ちょうどに鳴らすには1ステップ前にトリガーしておく必要がある
    const int64_t s16 = audio_.ready() ? audio_.stepIndex16()
                                       : int64_t(fallbackBeats_ * 4.0);
    if (s16 != lastStep16_) {
        lastStep16_ = s16;
        if ((s16 + 1) % (BEAM_PERIOD * 4) == 0 && (waveActive_ || !enemies_.empty())) {
            for (const auto& t : towers_)
                if (towerDefs()[int(t.kind)].isBeam) {
                    audio_.trigger(Sfx::BeamFire);
                    break;
                }
        }
    }

    // ---- 敵のスポーン ----
    if (waveActive_ && spawnIndex_ < spawnQueue_.size()) {
        spawnTimer_ -= dt;
        if (spawnTimer_ <= 0.f) {
            spawnTimer_ = SPAWN_INTERVAL;
            Enemy e = spawnQueue_[spawnIndex_++];
            e.id = nextEnemyId_++;
            e.pos = waypoints_[0];
            enemies_.push_back(e);
            if (e.kind == EnemyKind::Boss) { addShake(8.f); setMessage("The BOSS has arrived!"); }
        }
    }

    updateEnemies(dt);
    updateTowers(dt);
    updateProjectiles(dt);
    updateParticles(dt);

    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                   [](const Enemy& e){ return e.dead; }), enemies_.end());

    comboTimer_ -= dt;
    if (comboTimer_ <= 0.f) combo_ = 0;
    shakeAmp_ *= std::exp(-9.f * dt);            // 画面揺れは指数減衰

    if (waveActive_ && spawnIndex_ >= spawnQueue_.size() && enemies_.empty()) {
        waveActive_ = false;
        audio_.setBattle(false);
        audio_.trigger(Sfx::WaveClear);
        const int bonus = 50 + wave_ * 10;
        money_ += bonus;
        setMessage("Wave " + std::to_string(wave_) + " clear!  Bonus +" + std::to_string(bonus) + " G");
        autoTimer_ = 2.f;
    }
    if (!waveActive_ && autoNext_ && wave_ > 0 && !gameOver_) {
        autoTimer_ -= dt;
        if (autoTimer_ <= 0.f) startWave();
    }
}

// 拍の頭(オンビート)で1回呼ばれる: 揺れ・ビーム・金鉱の自動収入
void Game::onBeat(int64_t beat) {
    if (waveActive_) addShake((beat % 4 == 0) ? 2.2f : 1.1f);   // 小節頭は強め
    for (const auto& e : enemies_)
        if (e.kind == EnemyKind::Boss && !e.dead) { addShake(3.5f); break; }

    // ビームタワー: BEAM_PERIOD拍ごとに全基一斉発射(音楽の大きな区切りと一致)
    if (beat % BEAM_PERIOD == 0 && (waveActive_ || !enemies_.empty()))
        fireBeams();

    // 金鉱の自動収入: 1小節(4拍)ごと(ウェーブ進行中のみ)
    if (waveActive_ && beat / 4 != lastBar_) {
        lastBar_ = beat / 4;
        for (auto& t : towers_) {
            if (!towerDefs()[int(t.kind)].isMine) continue;
            const int gold = 2 + 2 * t.level;
            money_ += gold;
            fxCoins(t.pos, 1);
        }
    }
}

// 全ビームタワーがマウスカーソル方向へ直線ビームを発射
void Game::fireBeams() {
    bool fired = false;
    for (auto& t : towers_) {
        const TowerDef& def = towerDefs()[int(t.kind)];
        if (!def.isBeam) continue;
        Vec2 dir{ mouseFx_ - t.pos.x, mouseFy_ - t.pos.y };
        const float len = std::hypot(dir.x, dir.y);
        if (len < 1.f) dir = { 1.f, 0.f }; else { dir.x /= len; dir.y /= len; }
        const Vec2 end{ t.pos.x + dir.x * 900.f, t.pos.y + dir.y * 900.f };
        t.angle = std::atan2(dir.y, dir.x);

        const float dmg = statsOf(t).damage;
        for (auto& e : enemies_) {
            if (e.dead) continue;
            if (segDist(t.pos, end, e.pos) <= e.radius + 11.f) {
                fxSparks(e.pos, 3, def.color, 120.f);
                if (applyDamage(e, dmg, true) && e.hp <= 0.f) killEnemy(e);
            }
        }
        Particle p;                              // ビームの残光
        p.type = PType::BeamFx;
        p.pos = t.pos; p.pos2 = end;
        p.lifetime = 0.30f; p.size = 16.f; p.color = def.color;
        particles_.push_back(p);
        fxRing(t.pos, 24.f, def.color, 0.25f);
        fired = true;
    }
    if (fired) addShake(6.f);   // 音は1つ前の16分音符で予約済み(update参照)
}

void Game::updateEnemies(float dt) {
    for (auto& e : enemies_) {
        if (e.dead) continue;
        e.slowTimer = std::max(0.f, e.slowTimer - dt);
        const float step = e.speed * (e.slowTimer > 0.f ? SLOW_FACTOR : 1.f) * dt;
        const Vec2& target = waypoints_[e.wpIndex];
        const float d = dist(e.pos, target);
        if (d <= step) {
            e.pos = target;
            e.wpIndex++;
            if (e.wpIndex >= int(waypoints_.size())) {
                e.dead = true;
                lives_ -= (e.kind == EnemyKind::Boss) ? 3 : 1;
                audio_.trigger(Sfx::Leak);
                addShake(6.f);
                combo_ = 0;                       // 抜けられるとコンボが切れる
                if (lives_ <= 0) {
                    lives_ = 0; gameOver_ = true;
                    audio_.setBattle(false);
                    audio_.trigger(Sfx::GameOver);
                }
            }
        } else {
            e.pos.x += (target.x - e.pos.x) / d * step;
            e.pos.y += (target.y - e.pos.y) / d * step;
        }
    }
}

void Game::updateTowers(float dt) {
    for (auto& t : towers_) {
        const TowerDef& def = towerDefs()[int(t.kind)];
        if (def.isBeam || def.isMine) continue;   // 特殊タワーは自動攻撃しない
        t.cooldown -= dt;
        if (t.cooldown > 0.f) continue;

        const TowerStats s = statsOf(t);
        Enemy* best = nullptr;
        float bestProgress = -1.f;
        for (auto& e : enemies_) {
            if (e.dead) continue;
            if (e.armored && !def.pierce) continue;
            if (dist(e.pos, t.pos) > s.range) continue;
            const float progress = e.wpIndex * 10000.f - dist(e.pos, waypoints_[e.wpIndex]);
            if (progress > bestProgress) { bestProgress = progress; best = &e; }
        }
        if (!best) continue;

        t.cooldown = s.cooldown;
        switch (t.kind) {
            case TowerKind::Gun:    audio_.trigger(Sfx::GunShot);    break;
            case TowerKind::Cannon: audio_.trigger(Sfx::CannonShot); break;
            case TowerKind::Ice:    audio_.trigger(Sfx::IceShot);    break;
            default:                audio_.trigger(Sfx::SniperShot); break;
        }
        t.angle = std::atan2(best->pos.y - t.pos.y, best->pos.x - t.pos.x);
        fxSparks({ t.pos.x + std::cos(t.angle) * 20.f,                 // マズルフラッシュ
                   t.pos.y + std::sin(t.angle) * 20.f }, 2, def.color, 90.f);

        Projectile p;
        p.pos = p.prev = t.pos;
        p.targetId = best->id;
        p.lastTargetPos = best->pos;
        p.damage = s.damage;
        p.kind = t.kind;
        p.splashRadius = def.splashRadius;
        p.slows = def.slows;
        p.pierce = def.pierce;
        projectiles_.push_back(p);
    }
}

void Game::updateProjectiles(float dt) {
    for (auto& p : projectiles_) {
        Enemy* target = findEnemy(p.targetId);
        if (target) p.lastTargetPos = target->pos;
        const float d = dist(p.pos, p.lastTargetPos);
        const float step = PROJECTILE_SPEED * dt;
        p.prev = p.pos;
        if (d <= step + (target ? target->radius : 4.f)) {
            p.hit = true;
            if (p.splashRadius > 0.f) {
                fxRing(p.lastTargetPos, p.splashRadius, { 240, 153, 123 }, 0.35f);
                fxSparks(p.lastTargetPos, 8, { 240, 153, 123 }, 220.f);
                addShake(4.f);
                for (auto& e : enemies_) {
                    if (e.dead) continue;
                    if (dist(e.pos, p.lastTargetPos) <= p.splashRadius)
                        if (applyDamage(e, p.damage, false) && e.hp <= 0.f) killEnemy(e);
                }
            } else if (target) {
                fxSparks(target->pos, 2, towerDefs()[int(p.kind)].color, 100.f);
                if (applyDamage(*target, p.damage, p.pierce)) {
                    if (p.slows) target->slowTimer = SLOW_DURATION;
                    if (target->hp <= 0.f) killEnemy(*target);
                }
            }
        } else {
            p.pos.x += (p.lastTargetPos.x - p.pos.x) / d * step;
            p.pos.y += (p.lastTargetPos.y - p.pos.y) / d * step;
        }
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(),
                       [](const Projectile& p){ return p.hit; }), projectiles_.end());
}

void Game::updateParticles(float dt) {
    for (auto& p : particles_) {
        p.t += dt;
        switch (p.type) {
        case PType::Spark:
            p.pos.x += p.vel.x * dt; p.pos.y += p.vel.y * dt;
            p.vel.x *= 0.90f; p.vel.y *= 0.90f;
            break;
        case PType::Coin:
            p.vel.y += 620.f * dt;                // 重力で放物線を描く
            p.pos.x += p.vel.x * dt; p.pos.y += p.vel.y * dt;
            break;
        case PType::Text:
            p.pos.y -= 28.f * dt;                 // ふわっと浮き上がる
            break;
        default: break;
        }
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                     [](const Particle& p){ return p.t >= p.lifetime; }), particles_.end());
}

// ---- パーティクル生成 ----
void Game::fxRing(Vec2 p, float radius, Color c, float life) {
    Particle fx; fx.type = PType::Ring; fx.pos = p;
    fx.size = radius; fx.lifetime = life; fx.color = c;
    particles_.push_back(fx);
}
void Game::fxText(Vec2 p, const std::string& s, Color c) {
    Particle fx; fx.type = PType::Text; fx.pos = p;
    fx.lifetime = 0.8f; fx.color = c; fx.text = s;
    particles_.push_back(fx);
}
void Game::fxSparks(Vec2 p, int n, Color c, float speed) {
    for (int i = 0; i < n; ++i) {
        Particle fx; fx.type = PType::Spark; fx.pos = p;
        const float a = frand() * 6.2831f, v = speed * (0.5f + frand());
        fx.vel = { std::cos(a) * v, std::sin(a) * v };
        fx.size = 2.f + frand() * 2.5f; fx.lifetime = 0.30f + frand() * 0.2f; fx.color = c;
        particles_.push_back(fx);
    }
}
void Game::fxCoins(Vec2 p, int n) {
    for (int i = 0; i < n; ++i) {
        Particle fx; fx.type = PType::Coin; fx.pos = p;
        fx.vel = { (frand() - 0.5f) * 160.f, -180.f - frand() * 120.f };
        fx.size = 3.5f; fx.lifetime = 0.65f; fx.color = { 250, 205, 95 };
        particles_.push_back(fx);
    }
}

int Game::towerIndexAt(int col, int row) const {
    for (size_t i = 0; i < towers_.size(); ++i)
        if (towers_[i].col == col && towers_[i].row == row) return int(i);
    return -1;
}
bool Game::isPathCell(int col, int row) const { return pathCells_.count(row * COLS + col) > 0; }
void Game::setMessage(const std::string& msg) { message_ = msg; }

// ============================================================
//  描画
// ============================================================

void Game::fillRect(float x, float y, float w, float h, Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_FRect r{ x, y, w, h };
    SDL_RenderFillRectF(renderer_, &r);
}
void Game::drawRectOutline(float x, float y, float w, float h, Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_FRect r{ x, y, w, h };
    SDL_RenderDrawRectF(renderer_, &r);
}
void Game::fillCircle(float cx, float cy, float r, Color c, uint8_t alpha) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, alpha);
    const int ri = int(r);
    for (int dy = -ri; dy <= ri; ++dy) {
        const float dx = std::sqrt(std::max(0.f, r * r - float(dy * dy)));
        SDL_RenderDrawLineF(renderer_, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
void Game::drawCircleOutline(float cx, float cy, float r, Color c, float thickness) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    const int seg = 48;
    for (float w = 0; w < thickness; w += 0.5f) {
        float px = cx + (r + w), py = cy;
        for (int i = 1; i <= seg; ++i) {
            const float a = float(i) / seg * 6.28318f;
            const float nx = cx + std::cos(a) * (r + w);
            const float ny = cy + std::sin(a) * (r + w);
            SDL_RenderDrawLineF(renderer_, px, py, nx, ny);
            px = nx; py = ny;
        }
    }
}
void Game::drawThickLine(float x1, float y1, float x2, float y2, Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    for (int off = -2; off <= 2; ++off) {
        SDL_RenderDrawLineF(renderer_, x1 + off * 0.5f, y1, x2 + off * 0.5f, y2);
        SDL_RenderDrawLineF(renderer_, x1, y1 + off * 0.5f, x2, y2 + off * 0.5f);
    }
}
// グラデーション付きの太いビーム(中心ほど明るい)
void Game::drawBeamLine(Vec2 a, Vec2 b, float width, Color c, uint8_t alpha) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::hypot(dx, dy);
    if (len < 1.f) return;
    const float px = -dy / len, py = dx / len;   // 垂直方向
    const int w = std::max(1, int(width));
    for (int i = -w; i <= w; ++i) {
        const float fall = 1.f - std::fabs(float(i)) / (w + 1.f);
        const uint8_t aa = uint8_t(alpha * fall * fall);
        const bool core = std::abs(i) <= w / 4;
        SDL_SetRenderDrawColor(renderer_, core ? 255 : c.r, core ? 255 : c.g,
                               core ? 255 : c.b, aa);
        SDL_RenderDrawLineF(renderer_, a.x + px * i, a.y + py * i,
                            b.x + px * i, b.y + py * i);
    }
}
void Game::drawText(const std::string& text, float x, float y, Color c,
                    TTF_Font* font, bool centered) {
    if (text.empty()) return;
    SDL_Color sc{ c.r, c.g, c.b, 255 };
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), sc);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_SetTextureAlphaMod(tex, c.a);
    SDL_FRect dst{ x, y, float(surf->w), float(surf->h) };
    if (centered) { dst.x -= surf->w / 2.f; dst.y -= surf->h / 2.f; }
    SDL_RenderCopyF(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

void Game::render() {
    SDL_SetRenderDrawColor(renderer_, 32, 32, 36, 255);
    SDL_RenderClear(renderer_);
    renderHUD();
    renderField();
    renderShop();
    if (gameOver_) renderGameOver();
    SDL_RenderPresent(renderer_);
}

void Game::renderHUD() {
    fillRect(0, 0, WIN_W, HUD_H, { 24, 24, 28 });
    auto button = [&](const SDL_FRect& r, const std::string& label, bool active) {
        fillRect(r.x, r.y, r.w, r.h, active ? Color{ 80, 120, 200 } : Color{ 58, 58, 66 });
        drawText(label, r.x + r.w / 2, r.y + r.h / 2, { 255, 255, 255 }, fontS_, true);
    };
    button(kBtnWave,  waveActive_ ? "Wave..." : "Start Wave", !waveActive_);
    button(kBtnSpeed, "Speed x" + std::to_string(speedMult_), speedMult_ == 2);
    button(kBtnAuto,  autoNext_ ? "Auto: ON" : "Auto: OFF", autoNext_);

    // ---- 拍インジケーター: 4つの点が今の拍で光って膨らむ ----
    const float pulse = pulseNow();
    const int barBeat = int(beatIdxNow() % 4);
    for (int i = 0; i < 4; ++i) {
        const bool on = (i == barBeat);
        fillCircle(320 + i * 20.f, 22.f, on ? 5.f + 3.5f * pulse : 3.5f,
                   on ? Color{ 250, 205, 95 } : Color{ 90, 90, 100 });
    }
    drawText(kChordNames[chordIdxNow()], 408, 14, { 160, 180, 230 }, fontM_);

    drawText("Gold " + std::to_string(money_),  WIN_W - 290, 14, { 250, 199, 117 }, fontM_);
    drawText("Lives " + std::to_string(lives_), WIN_W - 175, 14, { 240, 149, 149 }, fontM_);
    drawText("Wave " + std::to_string(wave_),   WIN_W -  80, 14, { 180, 210, 255 }, fontM_);
}

void Game::renderField() {
    // ---- 画面揺れ: ビューポート自体をずらす ----
    const float ox = std::sin(timeSec_ * 61.0) * shakeAmp_;
    const float oy = std::cos(timeSec_ * 53.0) * shakeAmp_;
    SDL_Rect clip{ int(ox), HUD_H + int(oy), FIELD_W, FIELD_H };
    SDL_RenderSetViewport(renderer_, &clip);

    const float pulse = pulseNow();
    const int64_t beat = beatIdxNow();

    // 草地: 拍に合わせてほんのり明滅
    const int g = int(6 * pulse);
    for (int c = 0; c < COLS; ++c)
        for (int r = 0; r < ROWS; ++r)
            fillRect(c * CELL, r * CELL, CELL, CELL,
                     (c + r) % 2 ? Color{ uint8_t(156+g), uint8_t(190+g), uint8_t(124+g) }
                                 : Color{ uint8_t(166+g), uint8_t(199+g), uint8_t(136+g) });
    const int pg = int(10 * pulse);
    for (int key : pathCells_) {
        const int c = key % COLS, r = key / COLS;
        fillRect(c * CELL, r * CELL, CELL, CELL,
                 { uint8_t(217+pg), uint8_t(199+pg), uint8_t(158+pg) });
        drawRectOutline(c * CELL, r * CELL, CELL, CELL, { 194, 172, 126 });
    }
    SDL_SetRenderDrawColor(renderer_, 126, 106, 69, 255);
    for (int i = 0; i < 12; ++i)
        SDL_RenderDrawLineF(renderer_, 8, 113.f + i * 2, 8 + i * 1.4f, 113.f + i * 2);
    fillRect(680, 105, 4, 40, { 163, 45, 45 });
    fillRect(684, 105, 14, 14, { 163, 45, 45 });

    // ---- ビームタワーの照準線(常時マウスへ薄く表示) ----
    for (const auto& t : towers_) {
        if (!towerDefs()[int(t.kind)].isBeam) continue;
        SDL_SetRenderDrawColor(renderer_, 255, 92, 138, 45);
        SDL_RenderDrawLineF(renderer_, t.pos.x, t.pos.y, mouseFx_, mouseFy_);
    }

    // 設置プレビュー
    if (selectedShop_ >= 0 && hoverCol_ >= 0) {
        const TowerDef& def = towerDefs()[selectedShop_];
        const bool ok = !isPathCell(hoverCol_, hoverRow_) &&
                        towerIndexAt(hoverCol_, hoverRow_) < 0;
        const float x = (hoverCol_ + 0.5f) * CELL, y = (hoverRow_ + 0.5f) * CELL;
        if (def.range > 0.f)
            fillCircle(x, y, def.range, ok ? Color{255,255,255} : Color{226,75,74}, 50);
        fillCircle(x, y, 15, ok ? def.color : Color{226,75,74}, 180);
    }

    // ---- タワー: 拍に合わせて少し膨らむ(スクワッシュ&ストレッチ) ----
    for (size_t i = 0; i < towers_.size(); ++i) {
        const Tower& t = towers_[i];
        const TowerDef& def = towerDefs()[int(t.kind)];
        const float sc = def.isMine ? 1.f + 0.16f * pulse : 1.f + 0.07f * pulse;
        if (int(i) == selectedTower_ && statsOf(t).range > 0.f)
            fillCircle(t.pos.x, t.pos.y, statsOf(t).range, { 255, 255, 255 }, 45);

        const float half = 18.f * sc;
        fillRect(t.pos.x - half, t.pos.y - half, half * 2, half * 2,
                 def.isMine ? Color{ 120, 100, 60 } : Color{ 110, 110, 102 });
        if (!def.isMine)
            drawThickLine(t.pos.x, t.pos.y,
                          t.pos.x + std::cos(t.angle) * 19.f * sc,
                          t.pos.y + std::sin(t.angle) * 19.f * sc, def.color);
        fillCircle(t.pos.x, t.pos.y, (def.isMine ? 13.f : 12.f) * sc, def.color);
        drawText(def.isMine ? "G" : std::to_string(t.level),
                 t.pos.x, t.pos.y, def.isMine ? Color{90,60,10} : Color{255,255,255},
                 fontS_, true);

        // ビームタワー: 次弾までのチャージバー(8拍ぶん)
        if (def.isBeam) {
            const double prog = double(beat % BEAM_PERIOD + beatPhaseNow()) / BEAM_PERIOD;
            fillRect(t.pos.x - 16, t.pos.y - 27, 32, 4, { 40, 40, 46 });
            const bool full = prog > 0.94;
            fillRect(t.pos.x - 16, t.pos.y - 27, float(32 * prog), 4,
                     full ? Color{ 255, 255, 255 } : def.color);
        }
        // 金鉱: レベル表示を上に
        if (def.isMine)
            drawText("Lv." + std::to_string(t.level), t.pos.x, t.pos.y - 24,
                     { 255, 235, 160 }, fontS_, true);
    }

    // ---- 敵: 交互に拍でぴょこぴょこ跳ねる ----
    for (const auto& e : enemies_) {
        const float hop = ((beat + e.id) % 2 == 0) ? -4.f * pulse : 0.f;
        const float ey = e.pos.y + hop;
        fillCircle(e.pos.x, e.pos.y + e.radius * 0.7f, e.radius * 0.8f, { 0,0,0 }, 36); // 影
        fillCircle(e.pos.x, ey, e.radius, e.color);
        if (e.armored) drawCircleOutline(e.pos.x, ey, e.radius + 2.5f, { 255, 255, 255 });
        if (e.kind == EnemyKind::Boss)
            drawCircleOutline(e.pos.x, ey, e.radius + 4.f + 2.f * pulse, { 200, 120, 255 });
        if (e.slowTimer > 0.f)
            drawCircleOutline(e.pos.x, ey, std::max(2.f, e.radius - 4.f), { 133, 183, 235 });
        const float bw = (e.kind == EnemyKind::Boss) ? 40.f : 24.f;
        fillRect(e.pos.x - bw/2, ey - e.radius - 9, bw, 4, { 51, 51, 51 });
        fillRect(e.pos.x - bw/2, ey - e.radius - 9,
                 bw * std::max(0.f, e.hp / e.maxHp), 4, { 151, 196, 89 });
    }

    // ---- 弾 + トレイル ----
    for (const auto& p : projectiles_) {
        const Color c = towerDefs()[int(p.kind)].color;
        const float r = p.kind == TowerKind::Cannon ? 6.f : 4.f;
        fillCircle((p.prev.x + p.pos.x) / 2, (p.prev.y + p.pos.y) / 2, r * 0.7f, c, 90);
        fillCircle(p.prev.x, p.prev.y, r * 0.45f, c, 45);
        fillCircle(p.pos.x, p.pos.y, r, c);
        fillCircle(p.pos.x, p.pos.y, r * 0.45f, { 255, 255, 255 }, 200);
    }

    // ---- パーティクル ----
    for (const auto& p : particles_) {
        const float k = p.t / p.lifetime;
        const uint8_t alpha = uint8_t(255 * (1.f - k));
        switch (p.type) {
        case PType::Ring: {
            Color c = p.color; c.a = alpha;
            drawCircleOutline(p.pos.x, p.pos.y, p.size * k, c, 3.f);
            break;
        }
        case PType::Spark:
            fillCircle(p.pos.x, p.pos.y, p.size * (1.f - k), p.color, alpha);
            break;
        case PType::Coin:
            fillCircle(p.pos.x, p.pos.y, p.size, p.color, alpha);
            fillCircle(p.pos.x - 1, p.pos.y - 1, p.size * 0.4f, { 255, 255, 230 }, alpha);
            break;
        case PType::Text:
            drawText(p.text, p.pos.x, p.pos.y, { p.color.r, p.color.g, p.color.b, alpha },
                     fontS_, true);
            break;
        case PType::BeamFx:
            drawBeamLine(p.pos, p.pos2, p.size * (1.f - k), p.color, alpha);
            break;
        }
    }

    // ---- コンボ表示 ----
    if (combo_ >= 3)
        drawText("COMBO x" + std::to_string(combo_), FIELD_W / 2.f, 16.f + 2.f * pulse,
                 { 255, uint8_t(200 + 55 * pulse), 90 }, fontM_, true);

    SDL_RenderSetViewport(renderer_, nullptr);
}

void Game::renderShop() {
    fillRect(0, HUD_H + FIELD_H, WIN_W, SHOP_H, { 24, 24, 28 });
    for (int i = 0; i < int(TowerKind::COUNT); ++i) {
        const TowerDef& def = towerDefs()[i];
        const SDL_FRect r = shopCardRect(i);
        const bool selected = (selectedShop_ == i);
        const bool affordable = (money_ >= def.cost);
        fillRect(r.x, r.y, r.w, r.h, selected ? Color{ 52, 64, 92 } : Color{ 42, 42, 50 });
        if (selected) drawRectOutline(r.x, r.y, r.w, r.h, { 120, 160, 240 });
        fillCircle(r.x + 14, r.y + 14, 6, def.color);
        drawText(def.name, r.x + 26, r.y + 7,
                 affordable ? Color{ 240, 240, 240 } : Color{ 130, 130, 130 }, fontS_);
        drawText(def.desc, r.x + 8, r.y + 30, { 165, 165, 175 }, fontS_);
        drawText(std::to_string(def.cost) + " G", r.x + 8, r.y + 52,
                 affordable ? Color{ 250, 199, 117 } : Color{ 150, 120, 80 }, fontS_);
    }

    if (selectedTower_ >= 0 && selectedTower_ < int(towers_.size())) {
        const Tower& t = towers_[selectedTower_];
        const TowerDef& def = towerDefs()[int(t.kind)];
        const TowerStats s = statsOf(t);
        char buf[180];
        if (def.isMine)
            std::snprintf(buf, sizeof(buf),
                "Gold Mine Lv.%d   Auto +%d/bar   Perfect click +%d (Good +%d)",
                t.level, 2 + 2 * t.level, 5 + 5 * t.level, 2 + 2 * t.level);
        else if (def.isBeam)
            std::snprintf(buf, sizeof(buf),
                "Beam Lv.%d   DMG %.0f to ALL enemies in line, fires every %d beats",
                t.level, s.damage, BEAM_PERIOD);
        else
            std::snprintf(buf, sizeof(buf), "%s Lv.%d   DMG %.0f / Range %.0f / Rate %.2fs",
                          def.name, t.level, s.damage, s.range, s.cooldown);
        drawText(buf, 12, float(HUD_H + FIELD_H + 102), { 230, 230, 230 }, fontS_);

        const SDL_FRect up = btnUpgrade(), sell = btnSell();
        const bool maxed = (t.level >= MAX_TOWER_LEVEL);
        const std::string upLabel = maxed ? "Max level"
            : "Upgrade (" + std::to_string(upgradeCost(t)) + " G)";
        fillRect(up.x, up.y, up.w, up.h, maxed ? Color{ 58, 58, 66 } : Color{ 60, 110, 70 });
        drawText(upLabel, up.x + up.w / 2, up.y + up.h / 2, { 255, 255, 255 }, fontS_, true);
        fillRect(sell.x, sell.y, sell.w, sell.h, { 120, 60, 60 });
        drawText("Sell", sell.x + sell.w / 2, sell.y + sell.h / 2, { 255, 255, 255 }, fontS_, true);
    } else {
        drawText(message_, 12, float(HUD_H + FIELD_H + 102), { 170, 170, 180 }, fontS_);
    }
}

void Game::renderGameOver() {
    fillRect(0, HUD_H, FIELD_W, FIELD_H, { 0, 0, 0, 160 });
    drawText("GAME OVER", WIN_W / 2.f, HUD_H + FIELD_H / 2.f - 40, { 255, 255, 255 }, fontL_, true);
    drawText("Reached wave " + std::to_string(wave_), WIN_W / 2.f,
             HUD_H + FIELD_H / 2.f - 5, { 210, 210, 210 }, fontM_, true);
    const SDL_FRect r = btnRestart();
    fillRect(r.x, r.y, r.w, r.h, { 235, 235, 235 });
    drawText("Play again", r.x + r.w / 2, r.y + r.h / 2, { 30, 30, 30 }, fontM_, true);
}
