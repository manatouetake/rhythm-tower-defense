#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <vector>
#include <set>
#include <string>
#include "Config.h"
#include "Entities.h"
#include "AudioEngine.h"

// ============================================================
//  Game.h : ゲーム本体クラス
//  音楽(AudioEngine)の拍クロックを映像・ゲームプレイの両方に
//  同期させる「リズム連動タワーディフェンス」
// ============================================================

class Game {
public:
    bool init();
    void run();
    void shutdown();
    // Emscripten用
    void handleEventPublic(const SDL_Event& e);
    void tickPublic();
    void render();

private:
    // ---- ループ ----
    void handleEvent(const SDL_Event& e);
    void update(float dt);

    // ---- 音楽クロック(映像同期の心臓部) ----
    double  beatPhaseNow() const;   // 拍内位置 0-1 (音声無効時は内部時計)
    int64_t beatIdxNow()   const;   // 通算拍番号
    int     chordIdxNow()  const;
    float   pulseNow()     const;   // 拍頭で1.0→減衰する脈動カーブ

    // ---- ロジック ----
    void  resetGame();
    void  startWave();
    void  buildWave(int waveNum);
    void  updateEnemies(float dt);
    void  updateTowers(float dt);
    void  updateProjectiles(float dt);
    void  updateParticles(float dt);
    void  onBeat(int64_t beat);     // 拍の頭で1回呼ばれる(脈動/ビーム/金鉱収入)
    void  fireBeams();              // 全ビームタワーがマウス方向へ一斉発射
    void  mineClicked(Tower& mine); // 金鉱クリックのリズム判定
    TowerStats statsOf(const Tower& t) const;
    int   upgradeCost(const Tower& t) const;
    Enemy* findEnemy(int id);
    bool  applyDamage(Enemy& e, float dmg, bool pierce);
    void  killEnemy(Enemy& e);
    void  onLeftClick(int mx, int my);
    int   towerIndexAt(int col, int row) const;
    bool  isPathCell(int col, int row) const;
    void  setMessage(const std::string& msg);
    void  addShake(float amp) { if (amp > shakeAmp_) shakeAmp_ = amp; }

    // ---- パーティクル生成ヘルパー ----
    void fxRing(Vec2 p, float radius, Color c, float life = 0.3f);
    void fxText(Vec2 p, const std::string& s, Color c);
    void fxSparks(Vec2 p, int n, Color c, float speed = 160.f);
    void fxCoins(Vec2 p, int n);

    // ---- 描画 ----
    void fillRect(float x, float y, float w, float h, Color c);
    void drawRectOutline(float x, float y, float w, float h, Color c);
    void fillCircle(float cx, float cy, float r, Color c, uint8_t alpha = 255);
    void drawCircleOutline(float cx, float cy, float r, Color c, float thickness = 2.f);
    void drawThickLine(float x1, float y1, float x2, float y2, Color c);
    void drawBeamLine(Vec2 a, Vec2 b, float width, Color c, uint8_t alpha);
    void drawText(const std::string& text, float x, float y, Color c,
                  TTF_Font* font, bool centered = false);
    void renderHUD();
    void renderField();
    void renderShop();
    void renderGameOver();

    // ---- SDLリソース ----
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font*     fontS_ = nullptr;
    TTF_Font*     fontM_ = nullptr;
    TTF_Font*     fontL_ = nullptr;
    AudioEngine   audio_;

    // ---- ゲーム状態 ----
    bool  running_ = true;
    bool  gameOver_ = false;
    int   money_ = START_MONEY;
    int   lives_ = START_LIVES;
    int   wave_ = 0;
    int   speedMult_ = 1;
    bool  autoNext_ = false;
    float autoTimer_ = 0.f;
    bool  waveActive_ = false;
    float spawnTimer_ = 0.f;
    int   nextEnemyId_ = 1;

    std::vector<Enemy>      enemies_;
    std::vector<Enemy>      spawnQueue_;
    size_t                  spawnIndex_ = 0;
    std::vector<Tower>      towers_;
    std::vector<Projectile> projectiles_;
    std::vector<Particle>   particles_;

    // ---- リズム連動の状態 ----
    double  fallbackBeats_ = 0.0;   // 音声デバイスが無い時の代替拍時計
    int64_t lastBeatIdx_ = -1;
    int64_t lastStep16_ = -1;
    int64_t lastBar_ = -1;
    float   shakeAmp_ = 0.f;        // 画面揺れの振幅(指数減衰)
    double  timeSec_ = 0.0;
    int     combo_ = 0;             // 連続撃破コンボ(ゴールドボーナス)
    float   comboTimer_ = 0.f;

    // ---- 入力/UI ----
    int   selectedShop_ = -1;
    int   selectedTower_ = -1;
    int   hoverCol_ = -1, hoverRow_ = -1;
    float mouseFx_ = FIELD_W / 2.f, mouseFy_ = FIELD_H / 2.f;  // ビーム照準
    std::string message_;

    // ---- マップ ----
    std::vector<Vec2> waypoints_;
    std::set<int>     pathCells_;
};
