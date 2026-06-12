#include "Game.h"

// ============================================================
//  main.cpp : エントリーポイント
//  Gameクラスに処理を委譲するだけの薄い入口に保つ
// ============================================================

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;   // SDLの要求するシグネチャ(未使用警告の抑制)

    Game game;
    if (!game.init()) {
        game.shutdown();
        return 1;
    }
    game.run();
    game.shutdown();
    return 0;
}
