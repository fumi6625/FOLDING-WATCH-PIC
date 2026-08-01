// FOLDING WATCH PIC — 設定ページ
//
// 設定項目は2つだけ:
//   BG_WHITE    背景 黒(0) / 白(1)
//   BAND_COLOR  時と分の隙間の帯の色 (GColor8 の argb 値)
//
// 前景色 (数字・目盛り・ラベル) はウォッチ側が背景から自動導出する。
// ここで前景色まで選ばせると、白地に白文字のような読めない組合せが作れてしまうため。

// Pebble の GColor8: alpha=3(不透明) を上位2bitに置き、RGB は各2bit
//   argb = 0xC0 | (r << 4) | (g << 2) | b     r,g,b は 0..3
function argb(r, g, b) {
  return 0xC0 | (r << 4) | (g << 2) | b;
}

// 帯の候補色。黒背景でも白背景でも輪郭が出るものを選んである
var SWATCHES = [
  { name: 'オレンジ', argb: argb(3, 1, 0), css: '#FF5500' },
  { name: 'レッド',   argb: argb(3, 0, 0), css: '#FF0000' },
  { name: 'マゼンタ', argb: argb(3, 0, 2), css: '#FF00AA' },
  { name: 'ピンク',   argb: argb(3, 1, 2), css: '#FF55AA' },
  { name: 'パープル', argb: argb(2, 0, 3), css: '#AA00FF' },
  { name: 'ブルー',   argb: argb(0, 1, 3), css: '#0055FF' },
  { name: 'ティール', argb: argb(0, 2, 2), css: '#00AAAA' },
  { name: 'シアン',   argb: argb(0, 3, 3), css: '#00FFFF' },
  { name: 'グリーン', argb: argb(0, 2, 0), css: '#00AA00' },
  { name: 'ライム',   argb: argb(1, 3, 0), css: '#55FF00' },
  { name: 'イエロー', argb: argb(3, 3, 0), css: '#FFFF00' },
  { name: 'オリーブ', argb: argb(2, 2, 0), css: '#AAAA00' },
  { name: 'ブラウン', argb: argb(2, 1, 0), css: '#AA5500' },
  { name: 'グレー',   argb: argb(2, 2, 2), css: '#AAAAAA' }
];

var DEFAULTS = { bgWhite: 0, band: argb(3, 1, 0) };

function currentSettings() {
  try {
    var raw = localStorage.getItem('fwp_settings');
    if (raw) {
      var s = JSON.parse(raw);
      return {
        bgWhite: s.bgWhite ? 1 : 0,
        band: (typeof s.band === 'number') ? s.band : DEFAULTS.band
      };
    }
  } catch (e) {
    console.log('settings load failed: ' + e);
  }
  return { bgWhite: DEFAULTS.bgWhite, band: DEFAULTS.band };
}

function buildConfigHtml(cur) {
  var swatchHtml = '';
  for (var i = 0; i < SWATCHES.length; i++) {
    var s = SWATCHES[i];
    var sel = (s.argb === cur.band) ? ' sel' : '';
    swatchHtml +=
      '<div class="sw' + sel + '" data-v="' + s.argb + '" title="' + s.name +
      '" style="background:' + s.css + '"></div>';
  }

  return [
    '<!DOCTYPE html>',
    '<html lang="ja"><head><meta charset="utf-8">',
    '<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">',
    '<title>Folding watch pic</title>',
    '<style>',
    '*{box-sizing:border-box;margin:0;padding:0}',
    'body{background:#111;color:#eee;font-family:-apple-system,sans-serif;padding:20px}',
    'h1{font-size:19px;margin-bottom:20px}',
    'h2{font-size:14px;color:#aaa;margin:0 0 10px;font-weight:600}',
    '.sec{margin-bottom:28px}',
    '.bg{display:flex;gap:10px}',
    '.bgo{flex:1;border:3px solid #444;border-radius:10px;padding:18px 0;',
    'text-align:center;font-size:15px}',
    '.bgo.b{background:#000;color:#fff}',
    '.bgo.w{background:#fff;color:#000}',
    '.bgo.sel{border-color:#3d7aed}',
    '.sws{display:grid;grid-template-columns:repeat(7,1fr);gap:8px}',
    '.sw{height:38px;border-radius:6px;border:3px solid transparent}',
    '.sw.sel{border-color:#fff;box-shadow:0 0 0 2px #3d7aed}',
    '#pv{margin-top:14px;border-radius:8px;padding:16px;text-align:center}',
    '#pv .n{font-size:34px;font-weight:700;line-height:1.1}',
    '#pv .band{height:12px;margin:6px auto;width:70%;border-radius:2px}',
    'button{background:#3d7aed;color:#fff;border:none;border-radius:8px;',
    'padding:15px;font-size:16px;width:100%;margin-top:8px}',
    '</style></head><body>',
    '<h1>Folding watch pic</h1>',

    '<div class="sec"><h2>背景</h2><div class="bg">',
    '<div class="bgo b' + (cur.bgWhite ? '' : ' sel') + '" data-v="0">黒</div>',
    '<div class="bgo w' + (cur.bgWhite ? ' sel' : '') + '" data-v="1">白</div>',
    '</div></div>',

    '<div class="sec"><h2>帯の色</h2>',
    '<div class="sws">' + swatchHtml + '</div>',
    '<div id="pv"><div class="n" id="pn1">50</div>',
    '<div class="band" id="pb"></div>',
    '<div class="n" id="pn2">8</div></div></div>',

    '<button id="save">保存</button>',

    '<script>',
    'var bgw=' + cur.bgWhite + ',band=' + cur.band + ';',
    'function paint(){',
    ' var pv=document.getElementById("pv");',
    ' pv.style.background=bgw?"#fff":"#000";',
    ' var ink=bgw?"#000":"#fff";',
    ' document.getElementById("pn1").style.color=ink;',
    ' document.getElementById("pn2").style.color=ink;',
    ' var el=document.querySelector(".sw[data-v=\\u0022"+band+"\\u0022]");',
    ' document.getElementById("pb").style.background=el?el.style.background:"#f50";',
    '}',
    'var bgos=document.querySelectorAll(".bgo");',
    'for(var i=0;i<bgos.length;i++){(function(el){',
    ' el.onclick=function(){',
    '  for(var j=0;j<bgos.length;j++)bgos[j].classList.remove("sel");',
    '  el.classList.add("sel");bgw=parseInt(el.getAttribute("data-v"),10);paint();',
    ' };})(bgos[i]);}',
    'var sws=document.querySelectorAll(".sw");',
    'for(var k=0;k<sws.length;k++){(function(el){',
    ' el.onclick=function(){',
    '  for(var j=0;j<sws.length;j++)sws[j].classList.remove("sel");',
    '  el.classList.add("sel");band=parseInt(el.getAttribute("data-v"),10);paint();',
    ' };})(sws[k]);}',
    'paint();',
    'document.getElementById("save").onclick=function(){',
    ' var out={bgWhite:bgw,band:band};',
    ' document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(out));',
    '};',
    '</script></body></html>'
  ].join('');
}

function sendSettings(s) {
  Pebble.sendAppMessage(
    { 'BG_WHITE': s.bgWhite ? 1 : 0, 'BAND_COLOR': s.band },
    function () { console.log('settings sent'); },
    function (e) { console.log('settings send failed: ' + JSON.stringify(e)); }
  );
}

Pebble.addEventListener('ready', function () {
  // 起動時にも保存済み設定を送る (ウォッチ側の persist と食い違わないように)
  sendSettings(currentSettings());
});

Pebble.addEventListener('showConfiguration', function () {
  var html = buildConfigHtml(currentSettings());
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response || e.response === '' || e.response === 'CANCELLED') {
    return;
  }
  var s;
  try {
    s = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    console.log('config parse failed: ' + err);
    return;
  }
  try {
    localStorage.setItem('fwp_settings', JSON.stringify(s));
  } catch (err2) {
    console.log('settings save failed: ' + err2);
  }
  sendSettings(s);
});
