#!/bin/bash
# ============================================================
#  build_web.sh : Emscripten で WebAssembly ビルドを行い
#                 docs/ に GitHub Pages 用ファイルを出力する
#  使い方: chmod +x build_web.sh && ./build_web.sh
# ============================================================
set -e

# ---- Emscripten の確認 ----
if ! command -v emcc &> /dev/null; then
    echo ""
    echo "Emscripten が見つかりません。以下の手順でインストールしてください:"
    echo ""
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk"
    echo "  ./emsdk install latest"
    echo "  ./emsdk activate latest"
    echo "  source ./emsdk_env.sh   # ← これを毎回ターミナル起動時に実行 or .zshrc に追記"
    echo ""
    echo "または Homebrew があれば: brew install emscripten"
    echo ""
    exit 1
fi

echo "Emscripten: $(emcc --version | head -1)"

# ---- フォントの確認 ----
if [ ! -f "assets/font.ttf" ]; then
    echo "assets/font.ttf が見つかりません。コピーします..."
    mkdir -p assets
    # macOS システムフォントを試みる
    for f in \
        "/System/Library/Fonts/Supplemental/Arial.ttf" \
        "/Library/Fonts/Arial.ttf" \
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"; do
        if [ -f "$f" ]; then
            cp "$f" assets/font.ttf && echo "フォントをコピー: $f" && break
        fi
    done
    if [ ! -f "assets/font.ttf" ]; then
        echo "エラー: フォントが見つかりません。assets/font.ttf に任意の TTF を置いてください。"
        exit 1
    fi
fi

# ---- ビルド ----
mkdir -p build_web
cd build_web
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -- -j4
cd ..

echo ""
echo "=== ビルド完了! ==="
echo ""
echo "docs/ フォルダが更新されました。"
echo "次のコマンドでブラウザで動作確認できます:"
echo ""
echo "  cd docs && python3 -m http.server 8080"
echo "  → http://localhost:8080 で開く"
echo ""
echo "GitHub Pages への公開手順は README.md を参照してください。"
