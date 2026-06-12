// ============================================================
//  main.js : Emscripten モジュールの設定
//  index.js (Emscripten 生成) の読み込み前に実行される必要があるため
//  <script async src="index.js"> より先に読み込む
// ============================================================

var Module = {
    // canvas を動的に生成して #canvas-wrapper に挿入する
    canvas: (function () {
        var canvas = document.createElement('canvas');
        canvas.id = 'canvas';
        canvas.width = 700;
        canvas.height = 634;   // HUD_H(44) + FIELD_H(450) + SHOP_H(140)
        canvas.oncontextmenu = function (e) { e.preventDefault(); };
        document.getElementById('canvas-wrapper').appendChild(canvas);
        return canvas;
    })(),

    print: function (text) { console.log(text); },
    printErr: function (text) { console.warn(text); },

    // ロード完了時にローディング表示を消す
    onRuntimeInitialized: function () {
        var el = document.getElementById('loading');
        if (el) el.style.display = 'none';
    },

    // ダウンロード進捗をローディング表示に反映
    setStatus: function (text) {
        var el = document.getElementById('load-msg');
        if (el && text) el.textContent = text;
    },

    monitorRunDependencies: function (left) {
        if (left === 0) {
            var el = document.getElementById('loading');
            if (el) el.style.display = 'none';
        }
    }
};