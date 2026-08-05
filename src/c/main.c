/*
 * FOLDING WATCH PIC — 視認性優先リデザイン (v2)
 * Pebble Emery (200x228) / SDK 3
 *
 * 設計方針:
 *   旧版は「分」が45ptの数字なのに「時」は半径39pxのダイヤル上の3px幅の針1本で、
 *   数字目盛りも無かった。角度→数字の変換が必要で、瞬時に読めなかった。
 *   そこで、回転しない日付リング(31個の数字)を「時リング」に置き換え、
 *   分と同じ「12時位置の数字が現在値」という仕組みを時にも適用する。
 *   読み取り位置が縦に2つ並ぶだけになり、視線移動がほぼ無くなる。
 *
 * 構成 (中心 CX,CY=(120,78) からの半径。読み取りは6時側=下方向。時が上・分が下の順で並ぶ):
 *   r=61   時リングの数字  → 6時位置 (CX,139) がそのまま数字の中心
 *   r=99   分リングの目盛り (60本)
 *   r=113  分リングの数字  → 6時位置 (CX,191) がそのまま数字の中心
 *   r=39   バッテリー弧 (幅5px, 左半分) + 12時側から画面右端への水平延長線
 *   中央   BATTERY ラベルと残量の数値
 *   y=169..173  時と分の数字の隙間の色帯 (スマホから色を設定)。
 *   ★注意: declared=64の字形高が推定51px程度あり、gap=52pxとの余白は約1pxしか無い。
 *   帯5pxを戻すと数字と重なる可能性が高い (実機未確認)。
 *   時刻エリア (77,125)-(163,216) は保護矩形。色帯以外は描かない
 *
 * 設定 (スマホの Pebble アプリ):
 *   BG_WHITE    背景 黒(0) / 白(1)
 *   BAND_COLOR  帯の色 (GColor8 の argb 値)
 *   前景色は背景から自動導出する。白地に白文字のような組合せを作らせないため。
 *
 * 電力:
 *   更新は MINUTE_UNIT の1分ごと。起動時のみ AppTimer を約1.5秒回す。
 */

#include <pebble.h>

// ── 画面 ─────────────────────────────────────────────────
#define SCREEN_W  200
#define SCREEN_H  228

#define CX        120
#define CY         78   // 検討時のCY=88(時上/分下への反転案)から、さらに10px上げた値

// ── 分リング ──────────────────────────────────────────────
#define MIN_RING_R       99
#define MIN_TICK_MAJ_R   90
#define MIN_TICK_MIN_R   95
#define MIN_NUM_R       113

// ── 時リング (旧・日付リングを置換) ────────────────────────
#define HOUR_NUM_R       61
#define HOUR_TICK_OUT    52
#define HOUR_TICK_IN     45

// ── バッテリー ────────────────────────────────────────────
#define BAT_R_OUT        39
#define BAT_W             5      // 残量によらず一定。残量は色と数値で表す

// ── 読み取り位置 ──────────────────────────────────────────
// 6時側 (CY + 半径) にしたので、半径が小さい時リングが上、大きい分リングが下になる。
// リング自体はここ (139/191)、大きい数字はさらに DIGIT_OFFSET だけ下にずらして描く。
#define MIN_READ_CY   (CY + MIN_NUM_R)    // 191
#define HOUR_READ_CY  (CY + HOUR_NUM_R)   // 139
#define DIGIT_OFFSET    0                 // 数字だけ下げる量 (10→0: 数字を10px上へ移動)
#define MIN_DIGIT_CY   (MIN_READ_CY  + DIGIT_OFFSET)   // 191 (DIGIT_OFFSET=0のため MIN_READ_CYと同じ)
#define HOUR_DIGIT_CY  (HOUR_READ_CY + DIGIT_OFFSET)   // 139 (DIGIT_OFFSET=0のため HOUR_READ_CYと同じ)

// ── 時刻エリア (保護矩形) ─────────────────────────────────
// 時と分の数字を囲う長方形。この中には色帯以外いっさい描かない。
// 目盛り・ラベルとも、この矩形に入るものは間引く。
#define GUARD_DX      43
#define GUARD_X0   (CX - GUARD_DX)              //  77
#define GUARD_X1   (CX + GUARD_DX)              // 163
// Y方向は直接指定 (時・分の数字位置とは独立して固定)
#define GUARD_Y0      125
#define GUARD_Y1      216

// "pebble" ロゴ: バッテリー水平線 (y = CY - BAT_R_OUT = 39) の真上に置き、
// あの線がアンダーラインに見えるようにする。線は x=CX..SCREEN_W なのでその中央へ。
#define PEBBLE_CX   ((CX + SCREEN_W) / 2)   // 160
#define PEBBLE_CY    28
// pebble ロゴ画像は実測 51x17px
#define PEBBLE_IMG_W 51
#define PEBBLE_IMG_H 17
#define PEBBLE_DX    (PEBBLE_IMG_W / 2 + 4)
#define PEBBLE_DY    (PEBBLE_IMG_H / 2 + 4)
// 目盛りを消す矩形だけ 2px 上へ。バッテリー水平線(y=39)を削らないようにするため
#define PEBBLE_ERASE_DY   2
#define PEBBLE_ERASE_Y0  (PEBBLE_CY - 10 - PEBBLE_ERASE_DY)   // 16 (下端は 35)

// BATTERY ラベル画像は実測 57x12px
#define BATTERY_IMG_W 57
#define BATTERY_IMG_H 12
#define BATTERY_LABEL_CY  (CY - 9)

// ── 色帯 ──────────────────────────────────────────────────
// 時の数字(y=139)と分の数字(y=191)の間隔は52pxで固定。
// ここに「字形高 + 帯高」が入る必要があるので、帯は薄くして中央寄りに置く。
#define BAND_X    90
#define BAND_Y   169
#define BAND_H     5

// ── アニメーション ────────────────────────────────────────
#define ANIM_DURATION_MS  1500
#define ANIM_INTERVAL_MS    33

// ── 設定の保存キー ────────────────────────────────────────
#define PERSIST_KEY_BG_WHITE    10
#define PERSIST_KEY_BAND_COLOR  11

#define DEFAULT_BAND_ARGB  0xF4   // (255, 85, 0) オレンジ

// ── グローバル ────────────────────────────────────────────
static Window *s_window;
static Layer  *s_canvas_layer;

static GFont s_font_big;      // BrelaBig_64 (declared size) — 時・分の数字。gap=52px・帯2px固定に対し理論上は約1px超過するが、実測換算比が推定(0.8)より低い可能性を見て試す
static GFont s_font_mid;      // BrelaSmall_26 — バッテリーの数値
static GFont s_font_28b;      // 分リングのラベル / 日付 (GOTHIC_28_BOLD)
static GFont s_font_18b;      // 時リングのラベル (GOTHIC_18_BOLD)

// pebble ロゴ / BATTERY ラベルは画像。背景の黒/白それぞれに1枚ずつ ([0]=黒背景用, [1]=白背景用)
static GBitmap *s_bmp_pebble[2];
static GBitmap *s_bmp_battery[2];

static int s_hour, s_min, s_wday, s_mday;
static int s_battery_pct = 100;

static bool    s_bg_white  = false;
static uint8_t s_band_argb = DEFAULT_BAND_ARGB;

// 背景から導出する前景3階層
static GColor s_bg, s_ink, s_label, s_sub;

static bool      s_animating    = false;
static int       s_anim_elapsed = 0;
static AppTimer *s_anim_timer   = NULL;

static const char *s_min_labels[12] = {
  "60","5","10","15","20","25","30","35","40","45","50","55"
};
static const char *s_hour_labels[12] = {
  "12","1","2","3","4","5","6","7","8","9","10","11"
};
static const char *s_day_names[7] = {
  "SUN","MON","TUE","WED","THU","FRI","SAT"
};

// ── ユーティリティ ────────────────────────────────────────

static GPoint polar_pt(int cx, int cy, int r, int angle_deg) {
  int32_t norm = angle_deg % 360;
  if (norm < 0) norm += 360;
  int32_t angle = norm * TRIG_MAX_ANGLE / 360;
  return GPoint(
    cx + (int)(r * sin_lookup(angle) / TRIG_MAX_RATIO),
    cy - (int)(r * cos_lookup(angle) / TRIG_MAX_RATIO)
  );
}

static int iabs(int v) { return v < 0 ? -v : v; }

// 区間 [a,b] の進捗を 0..1000 で返す
static int32_t seg(int32_t t, int32_t a, int32_t b) {
  if (t <= a) return 0;
  if (t >= b) return 1000;
  return (t - a) * 1000 / (b - a);
}

static int32_t ease_in_out(int32_t t) {
  if (t < 500) return 2 * t * t / 1000;
  return -1000 + (4000 - 2 * t) * t / 1000;
}

// 終盤を強く引き延ばす減速。1 - (1-t)^5
static int32_t ease_out_quint(int32_t t) {
  int32_t u  = 1000 - t;
  int32_t u2 = u  * u / 1000;
  int32_t u3 = u2 * u / 1000;
  int32_t u4 = u3 * u / 1000;
  int32_t u5 = u4 * u / 1000;
  return 1000 - u5;
}

// 0 から deg へ向かう最短角 (180°を超えるなら逆回転)
static int32_t shortest(int32_t deg) {
  return (deg > 180) ? deg - 360 : deg;
}

// 背景から前景3階層を決める
static void apply_palette(void) {
  if (s_bg_white) {
    s_bg    = GColorWhite;
    s_ink   = GColorBlack;       // 数字・分リングの目盛り
    s_label = GColorLightGray;   // リングのラベル (テクスチャ)
    s_sub   = GColorDarkGray;    // 日付・曜日・BATTERY
  } else {
    s_bg    = GColorBlack;
    s_ink   = GColorWhite;
    s_label = GColorDarkGray;
    s_sub   = GColorLightGray;
  }
}

// バッテリー色。白背景では明るい色が飛ぶので濃い側の5色に差し替える
static GColor bat_color(int pct) {
  GColor c;
  if (s_bg_white) {
    if      (pct >= 85) c.argb = 0xCA;   // (0,170,170)   ティール
    else if (pct >= 60) c.argb = 0xC8;   // (0,170,0)     グリーン
    else if (pct >= 40) c.argb = 0xE8;   // (170,170,0)   オリーブ
    else if (pct >= 20) c.argb = 0xE4;   // (170,85,0)    ブラウン
    else                c.argb = 0xF0;   // (255,0,0)     レッド
  } else {
    if      (pct >= 85) c.argb = 0xCF;   // (0,255,255)   シアン
    else if (pct >= 60) c.argb = 0xDE;   // (170,255,170) ミント
    else if (pct >= 40) c.argb = 0xFC;   // (255,255,0)   イエロー
    else if (pct >= 20) c.argb = 0xF8;   // (255,170,0)   オレンジ
    else                c.argb = 0xF0;   // (255,0,0)     レッド
  }
  return c;
}

// テキストを (cx, cy) に上下中央で描く。
// graphics_draw_text は矩形の上端から描く。content_size の高さにはディセント
// (数字には存在しない下の余白) が含まれるため、それだけで中央を取ると字形が
// 下にずれる。dy でフォントごとに補正する。
// 旧版は BrelaDigits_45 に対し「中心 -32、高さ52」を実機で調整済みだった。
// これは content_size の半分より約6px 上、つまりフォント高の約13%にあたる。
static void draw_text_mid(GContext *ctx, const char *text, GFont font,
                          GColor color, int cx, int cy, int box_w, int dy) {
  GRect probe = GRect(cx - box_w / 2, cy - 60, box_w, 120);
  GSize sz = graphics_text_layout_get_content_size(
      text, font, probe, GTextOverflowModeWordWrap, GTextAlignmentCenter);
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font,
                     GRect(cx - box_w / 2, cy - sz.h / 2 + dy, box_w, sz.h + 4),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// フォント高の約13%。実機で字形が上下にずれる場合はここを増減させる
#define DY_BIG   (-7)    // BrelaBig_64
#define DY_MID   (-3)    // BrelaSmall_26
#define DY_SYS     0     // Gothic 系は content_size の半分でほぼ合う

// ── 描画 ──────────────────────────────────────────────────

// 時刻エリア (保護矩形) の中か。ここには色帯以外なにも描かない
static bool in_guard(GPoint p) {
  return p.x >= GUARD_X0 && p.x <= GUARD_X1 &&
         p.y >= GUARD_Y0 && p.y <= GUARD_Y1;
}

// 目盛り1本を描く。両端のどちらかが保護矩形に入るなら描かない
static void draw_tick(GContext *ctx, int r_out, int r_in, int ang) {
  GPoint a = polar_pt(CX, CY, r_out, ang);
  GPoint b = polar_pt(CX, CY, r_in,  ang);
  if (in_guard(a) || in_guard(b)) return;
  graphics_draw_line(ctx, a, b);
}

// ラベルを描かない条件: 保護矩形の中、または "pebble" ロゴと重なる位置
static bool label_should_skip(GPoint p) {
  if (in_guard(p)) return true;
  if (iabs(p.y - PEBBLE_CY) < PEBBLE_DY && iabs(p.x - PEBBLE_CX) < PEBBLE_DX) {
    return true;
  }
  return false;
}

// 時リング: 読み取りは6時側 (+180°)。hour_deg だけ逆回転させると
// 6時位置に現在の時が来る。
static void draw_hour_ring(GContext *ctx, int hour_deg) {
  graphics_context_set_stroke_color(ctx, s_label);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < 12; i++) {
    draw_tick(ctx, HOUR_TICK_OUT, HOUR_TICK_IN, i * 30 - hour_deg + 180);
  }
  for (int i = 0; i < 12; i++) {
    GPoint p = polar_pt(CX, CY, HOUR_NUM_R, i * 30 - hour_deg + 180);
    if (label_should_skip(p)) continue;
    draw_text_mid(ctx, s_hour_labels[i], s_font_18b, s_label, p.x, p.y, 34, DY_SYS);
  }
}

// 分リング: 60本の目盛りと12個の数字。読み取りは6時側 (+180°)
static void draw_minute_ring(GContext *ctx, int min_deg) {
  graphics_context_set_stroke_color(ctx, s_ink);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < 60; i++) {
    bool maj = (i % 5 == 0);
    draw_tick(ctx, MIN_RING_R, maj ? MIN_TICK_MAJ_R : MIN_TICK_MIN_R,
              i * 6 - min_deg + 180);
  }
  for (int i = 0; i < 12; i++) {
    GPoint p = polar_pt(CX, CY, MIN_NUM_R, i * 30 - min_deg + 180);
    if (label_should_skip(p)) continue;
    draw_text_mid(ctx, s_min_labels[i], s_font_28b, s_label, p.x, p.y, 40, DY_SYS);
  }
}

// 画像を (cx, cy) 中心に描く。[0]=黒背景用、[1]=白背景用を s_bg_white で選ぶ
static void draw_bitmap_mid(GContext *ctx, GBitmap *bmp[2], int cx, int cy) {
  GBitmap *b = bmp[s_bg_white ? 1 : 0];
  GRect bounds = gbitmap_get_bounds(b);
  GRect dest = GRect(cx - bounds.size.w / 2, cy - bounds.size.h / 2,
                     bounds.size.w, bounds.size.h);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, b, dest);
}

// バッテリー: 6時位置(Pebble角180°)を起点に左側を通って12時位置(360°)まで伸びる弧。
// そのあと line1000 に応じて12時側の端から画面右端まで水平に延長する。
static void draw_battery(GContext *ctx, int pct,
                         int32_t sweep1000, int32_t line1000, int shown_pct) {
  GColor bc = bat_color(pct);

  if (sweep1000 > 0) {
    GRect r = GRect(CX - BAT_R_OUT, CY - BAT_R_OUT,
                    BAT_R_OUT * 2, BAT_R_OUT * 2);
    int32_t end_deg = 180 + 180 * sweep1000 / 1000;
    graphics_context_set_fill_color(ctx, bc);
    graphics_fill_radial(ctx, r, GOvalScaleModeFitCircle, BAT_W,
                         DEG_TO_TRIGANGLE(180), DEG_TO_TRIGANGLE(end_deg));
  }

  if (line1000 > 0) {
    int len = (SCREEN_W - CX) * line1000 / 1000;
    graphics_context_set_fill_color(ctx, bc);
    graphics_fill_rect(ctx, GRect(CX, CY - BAT_R_OUT, len, BAT_W),
                       0, GCornerNone);
  }

  if (shown_pct >= 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", shown_pct);
    draw_bitmap_mid(ctx, s_bmp_battery, CX, BATTERY_LABEL_CY);
    draw_text_mid(ctx, buf, s_font_mid, bc, CX, CY + 13, 60, DY_MID);
  }
}

// ── メイン描画 ────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  int min_deg, hour_deg, disp_min, disp_hour, shown_pct;
  int32_t sweep1000, line1000;
  int band_x0, band_w;

  if (s_animating) {
    int32_t t = (int32_t)s_anim_elapsed * 1000 / ANIM_DURATION_MS;
    if (t > 1000) t = 1000;

    // リングは12時位置 (60 と 12) から最短回りで現在値へ。
    // ease_out_quint で最後まで到達させ、終盤を強く引き延ばして焦らす
    min_deg  = (int)(ease_out_quint(seg(t, 0, 1000)) * shortest(s_min * 6) / 1000);
    hour_deg = (int)(ease_out_quint(seg(t, 0, 900))
                     * shortest((s_hour % 12) * 30) / 1000);

    // 弧が下から上へ → 続けて水平線が右端へ
    sweep1000 = ease_in_out(seg(t, 80, 550));
    line1000  = ease_in_out(seg(t, 530, 800));
    shown_pct = (t > 180)
        ? (int)(s_battery_pct * ease_in_out(seg(t, 180, 780)) / 1000)
        : -1;

    // 数字はリングの現在位置に追従させる (回って数字が変わるのが見どころ)
    disp_min  = ((min_deg  / 6)  % 60 + 60) % 60;
    disp_hour = ((hour_deg / 30) % 12 + 12) % 12;

    // 色帯: 電車が通過して駅に停まるイメージ。
    // 右端は画面右端(SCREEN_W)で固定。
    //   フェーズ1 (0〜260): 右端へ向けて右辺が伸び、左辺は0のまま
    //                      → 画面いっぱいの一本の帯になる (電車が通過)
    //   フェーズ2 (260〜700): 左辺だけが0→BAND_Xへ、減速しながら移動
    //                      → 最後尾が定位置で停車 (デフォルト表示)
    {
      int32_t grow   = ease_in_out(seg(t, 0, 260));
      int32_t settle = ease_out_quint(seg(t, 260, 700));
      int right_edge = SCREEN_W * grow / 1000;
      int left_edge  = (t <= 260) ? 0 : BAND_X * settle / 1000;
      band_x0 = left_edge;
      band_w  = right_edge - left_edge;
      if (band_w < 0) band_w = 0;
    }
  } else {
    min_deg   = s_min * 6;
    hour_deg  = (s_hour % 12) * 30;
    sweep1000 = 1000;
    line1000  = 1000;
    shown_pct = s_battery_pct;
    disp_min  = s_min;
    disp_hour = s_hour % 12;
    band_x0   = BAND_X;
    band_w    = SCREEN_W - BAND_X;
  }

  // 背景
  graphics_context_set_fill_color(ctx, s_bg);
  graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);

  // 時と分の隙間の色帯
  if (band_w > 0) {
    GColor band;
    band.argb = s_band_argb;
    graphics_context_set_fill_color(ctx, band);
    graphics_fill_rect(ctx, GRect(band_x0, BAND_Y, band_w, BAND_H),
                       0, GCornerNone);
  }

  draw_battery(ctx, s_battery_pct, sweep1000, line1000, shown_pct);
  draw_hour_ring(ctx, hour_deg);
  draw_minute_ring(ctx, min_deg);

  // 時・分の数字 (最上層)。
  // 分は 0 のとき素直に "00" と表示する (分リングのラベルは慣習で "60" を
  // 使うが、実際の時刻読み取り数字にその慣習を持ち込むと「4時60分」の
  // ように誤読される。時は12時間表記の慣習どおり 0→12 のままでよい)
  {
    char mbuf[8], hbuf[8];
    snprintf(mbuf, sizeof(mbuf), "%02d", disp_min);
    snprintf(hbuf, sizeof(hbuf), "%d",  disp_hour == 0 ? 12 : disp_hour);
    draw_text_mid(ctx, mbuf, s_font_big, s_ink, CX, MIN_DIGIT_CY,  100, DY_BIG);
    draw_text_mid(ctx, hbuf, s_font_big, s_ink, CX, HOUR_DIGIT_CY, 100, DY_BIG);
  }

  // 日付・曜日 (左下、分リング r=113 の外側。分の数字が下に来たため)。
  // 日付・曜日は横並び (日付→曜日の順)。曜日は色帯と同じ色・日付と同じ
  // 大きさ(GOTHIC_28_BOLD)
  {
    GColor band_color;
    band_color.argb = s_band_argb;
    char dbuf[8];
    snprintf(dbuf, sizeof(dbuf), "%d", s_mday);
    draw_text_mid(ctx, dbuf, s_font_28b, s_sub, 20, SCREEN_H - 18, 36, DY_SYS);
    draw_text_mid(ctx, s_day_names[s_wday], s_font_28b, band_color,
                 54, SCREEN_H - 18, 64, DY_SYS);
  }

  // pebble: バッテリー水平線 (y = CY - BAT_R_OUT) の真上。
  // あの線がそのままアンダーラインに見える。リングのラベルは背景色で打ち抜く
  graphics_context_set_fill_color(ctx, s_bg);
  graphics_fill_rect(ctx, GRect(PEBBLE_CX - 34, PEBBLE_ERASE_Y0, 68, 20),
                     0, GCornerNone);
  draw_bitmap_mid(ctx, s_bmp_pebble, PEBBLE_CX, PEBBLE_CY);
}

// ── アニメーション ────────────────────────────────────────
static void anim_tick(void *data) {
  s_anim_elapsed += ANIM_INTERVAL_MS;
  if (s_anim_elapsed >= ANIM_DURATION_MS) {
    s_anim_elapsed = ANIM_DURATION_MS;
    s_animating    = false;
    s_anim_timer   = NULL;
    layer_mark_dirty(s_canvas_layer);
    return;
  }
  layer_mark_dirty(s_canvas_layer);
  s_anim_timer = app_timer_register(ANIM_INTERVAL_MS, anim_tick, NULL);
}

static void start_animation(void) {
  if (s_anim_timer) {
    app_timer_cancel(s_anim_timer);
    s_anim_timer = NULL;
  }
  s_animating    = true;
  s_anim_elapsed = 0;
  s_anim_timer   = app_timer_register(ANIM_INTERVAL_MS, anim_tick, NULL);
}

// ── 設定 ──────────────────────────────────────────────────
static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  bool changed = false;

  Tuple *t_bg = dict_find(iter, MESSAGE_KEY_BG_WHITE);
  if (t_bg) {
    s_bg_white = (t_bg->value->int32 != 0);
    persist_write_int(PERSIST_KEY_BG_WHITE, s_bg_white ? 1 : 0);
    changed = true;
  }

  Tuple *t_band = dict_find(iter, MESSAGE_KEY_BAND_COLOR);
  if (t_band) {
    s_band_argb = (uint8_t)(t_band->value->int32 & 0xFF);
    persist_write_int(PERSIST_KEY_BAND_COLOR, s_band_argb);
    changed = true;
  }

  if (changed) {
    apply_palette();
    layer_mark_dirty(s_canvas_layer);
  }
}

static void load_settings(void) {
  if (persist_exists(PERSIST_KEY_BG_WHITE)) {
    s_bg_white = (persist_read_int(PERSIST_KEY_BG_WHITE) != 0);
  }
  if (persist_exists(PERSIST_KEY_BAND_COLOR)) {
    s_band_argb = (uint8_t)(persist_read_int(PERSIST_KEY_BAND_COLOR) & 0xFF);
  }
  apply_palette();
}

// ── イベント ──────────────────────────────────────────────
static void tick_handler(struct tm *t, TimeUnits units) {
  s_min  = t->tm_min;
  s_hour = t->tm_hour;
  s_wday = t->tm_wday;
  s_mday = t->tm_mday;
  if (!s_animating) layer_mark_dirty(s_canvas_layer);
}

static void battery_handler(BatteryChargeState state) {
  s_battery_pct = state.charge_percent;
  if (!s_animating) layer_mark_dirty(s_canvas_layer);
}

// ── Window ────────────────────────────────────────────────
static void window_load(Window *window) {
  s_font_big = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_BrelaBig_64));
  s_font_mid = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_BrelaSmall_26));
  s_font_28b = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_font_18b = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  // pebble ロゴ / BATTERY ラベルは画像 (黒背景用・白背景用の2枚ずつ)
  s_bmp_pebble[0]  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PEBBLE_DARK);
  s_bmp_pebble[1]  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PEBBLE_LIGHT);
  s_bmp_battery[0] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY_DARK);
  s_bmp_battery[1] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY_LIGHT);

  Layer *root = window_get_root_layer(window);
  s_canvas_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root, s_canvas_layer);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  s_min  = t->tm_min;
  s_hour = t->tm_hour;
  s_wday = t->tm_wday;
  s_mday = t->tm_mday;

  s_battery_pct = battery_state_service_peek().charge_percent;

  load_settings();

  // 更新は1分ごと。秒表示は無いので秒単位で起こす必要が無い
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(256, 64);
}

// .appear は初回表示時と、通知など他のウィンドウが閉じてこの画面に
// 戻ってきた時の両方で呼ばれる。どちらでも起動アニメーションを再生する
static void window_appear(Window *window) {
  start_animation();
}

static void window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  app_message_deregister_callbacks();
  if (s_anim_timer) {
    app_timer_cancel(s_anim_timer);
    s_anim_timer = NULL;
  }
  fonts_unload_custom_font(s_font_big);
  fonts_unload_custom_font(s_font_mid);
  gbitmap_destroy(s_bmp_pebble[0]);
  gbitmap_destroy(s_bmp_pebble[1]);
  gbitmap_destroy(s_bmp_battery[0]);
  gbitmap_destroy(s_bmp_battery[1]);
  layer_destroy(s_canvas_layer);
}

// ── App Entry ─────────────────────────────────────────────
static void init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .appear = window_appear,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
