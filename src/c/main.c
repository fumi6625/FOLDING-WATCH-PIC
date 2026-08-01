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
 * 構成 (中心 CX,CY からの半径):
 *   r=113  分リングの数字  → 12時位置 (CX,34) が現在の分
 *   r=99   分リングの目盛り (60本)
 *   r=62   時リングの数字  → 12時位置 (CX,85) が現在の時
 *   r=39   バッテリー弧 (幅5px, 左半分) + 12時側から画面右端への水平延長線
 *   中央   BATTERY ラベルと残量の数値
 *   y=52..65  時と分の隙間の色帯 (スマホから色を設定)
 *
 * 設定 (スマホの Pebble アプリ):
 *   BG_WHITE    背景 黒(0) / 白(1)
 *   BAND_COLOR  帯の色 (GColor8 の argb 値)
 *   前景色は背景から自動導出する。白地に白文字のような組合せを作らせないため。
 *
 * 電力:
 *   更新は MINUTE_UNIT の1分ごと。起動時のみ AppTimer を約1.2秒回す。
 */

#include <pebble.h>

// ── 画面 ─────────────────────────────────────────────────
#define SCREEN_W  200
#define SCREEN_H  228

#define CX        120
#define CY        147

// ── 分リング ──────────────────────────────────────────────
#define MIN_RING_R       99
#define MIN_TICK_MAJ_R   90
#define MIN_TICK_MIN_R   95
#define MIN_NUM_R       113

// ── 時リング (旧・日付リングを置換) ────────────────────────
#define HOUR_NUM_R       62
#define HOUR_TICK_OUT    50
#define HOUR_TICK_IN     44

// ── バッテリー ────────────────────────────────────────────
#define BAT_R_OUT        39
#define BAT_W             5      // 残量によらず一定。残量は色と数値で表す

// ── 読み取り位置 ──────────────────────────────────────────
#define MIN_READ_CY   (CY - MIN_NUM_R)    //  34
#define HOUR_READ_CY  (CY - HOUR_NUM_R)   //  85

// 読み取り位置に来るリングのラベルは描かない (大きい数字と二重になるため)
#define GHOST_DX   34
#define GHOST_DY   26

// ── 色帯 ──────────────────────────────────────────────────
#define BAND_X    90
#define BAND_Y    52
#define BAND_H    14

// ── アニメーション ────────────────────────────────────────
#define ANIM_DURATION_MS  1200
#define ANIM_INTERVAL_MS    33

// ── 設定の保存キー ────────────────────────────────────────
#define PERSIST_KEY_BG_WHITE    10
#define PERSIST_KEY_BAND_COLOR  11

#define DEFAULT_BAND_ARGB  0xF4   // (255, 85, 0) オレンジ

// ── グローバル ────────────────────────────────────────────
static Window *s_window;
static Layer  *s_canvas_layer;

static GFont s_font_big;      // BrelaDigits_45 — 時・分の数字
static GFont s_font_mid;      // BrelaDigits_24 — バッテリーの数値
static GFont s_font_24b;      // 分リングのラベル / 日付
static GFont s_font_14b;      // 時リングのラベル / pebble
static GFont s_font_09;       // BATTERY / 曜日

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

static int32_t ease_out_back(int32_t t) {
  int32_t tm1   = t - 1000;
  int32_t tm1_2 = tm1 * tm1 / 1000;
  int32_t tm1_3 = tm1_2 * tm1 / 1000;
  return 1000 + 2702 * tm1_3 / 1000 + 1702 * tm1_2 / 1000;
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
// graphics_draw_text は矩形の上端から描くので、実寸を測ってから中央に置く。
static void draw_text_mid(GContext *ctx, const char *text, GFont font,
                          GColor color, int cx, int cy, int box_w) {
  GRect probe = GRect(cx - box_w / 2, cy - 50, box_w, 100);
  GSize sz = graphics_text_layout_get_content_size(
      text, font, probe, GTextOverflowModeWordWrap, GTextAlignmentCenter);
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font,
                     GRect(cx - box_w / 2, cy - sz.h / 2, box_w, sz.h + 4),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// ── 描画 ──────────────────────────────────────────────────

// 時リング: hour_deg だけ逆回転させると12時位置に現在の時が来る
static void draw_hour_ring(GContext *ctx, int hour_deg) {
  graphics_context_set_stroke_color(ctx, s_label);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < 12; i++) {
    int ang = i * 30 - hour_deg;
    graphics_draw_line(ctx,
                       polar_pt(CX, CY, HOUR_TICK_OUT, ang),
                       polar_pt(CX, CY, HOUR_TICK_IN,  ang));
  }
  for (int i = 0; i < 12; i++) {
    GPoint p = polar_pt(CX, CY, HOUR_NUM_R, i * 30 - hour_deg);
    if (iabs(p.x - CX) < GHOST_DX && iabs(p.y - HOUR_READ_CY) < GHOST_DY) {
      continue;
    }
    draw_text_mid(ctx, s_hour_labels[i], s_font_14b, s_label, p.x, p.y, 30);
  }
}

// 分リング: 60本の目盛りと12個の数字
static void draw_minute_ring(GContext *ctx, int min_deg) {
  graphics_context_set_stroke_color(ctx, s_ink);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < 60; i++) {
    int  ang = i * 6 - min_deg;
    bool maj = (i % 5 == 0);
    graphics_draw_line(ctx,
        polar_pt(CX, CY, MIN_RING_R, ang),
        polar_pt(CX, CY, maj ? MIN_TICK_MAJ_R : MIN_TICK_MIN_R, ang));
  }
  for (int i = 0; i < 12; i++) {
    GPoint p = polar_pt(CX, CY, MIN_NUM_R, i * 30 - min_deg);
    if (iabs(p.x - CX) < GHOST_DX && iabs(p.y - MIN_READ_CY) < GHOST_DY) {
      continue;
    }
    draw_text_mid(ctx, s_min_labels[i], s_font_24b, s_label, p.x, p.y, 34);
  }
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
    draw_text_mid(ctx, "BATTERY", s_font_09, s_sub, CX, CY - 9, 70);
    draw_text_mid(ctx, buf, s_font_mid, bc, CX, CY + 13, 60);
  }
}

// ── メイン描画 ────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  int min_deg, hour_deg, disp_min, disp_hour, shown_pct;
  int32_t sweep1000, line1000;

  if (s_animating) {
    int32_t t = (int32_t)s_anim_elapsed * 1000 / ANIM_DURATION_MS;
    if (t > 1000) t = 1000;

    // リングは12時位置 (60 と 12) から最短回りで現在値へ
    min_deg  = (int)(ease_in_out(seg(t, 0, 700)) * shortest(s_min * 6) / 1000);
    hour_deg = (int)(ease_out_back(seg(t, 0, 580))
                     * shortest((s_hour % 12) * 30) / 1000);

    // 弧が下から上へ → 続けて水平線が右端へ
    sweep1000 = ease_in_out(seg(t, 100, 600));
    line1000  = ease_in_out(seg(t, 580, 880));
    shown_pct = (t > 200)
        ? (int)(s_battery_pct * ease_in_out(seg(t, 200, 800)) / 1000)
        : -1;

    // 数字はリングの現在位置に追従させる (回って数字が変わるのが見どころ)
    disp_min  = ((min_deg  / 6)  % 60 + 60) % 60;
    disp_hour = ((hour_deg / 30) % 12 + 12) % 12;
  } else {
    min_deg   = s_min * 6;
    hour_deg  = (s_hour % 12) * 30;
    sweep1000 = 1000;
    line1000  = 1000;
    shown_pct = s_battery_pct;
    disp_min  = s_min;
    disp_hour = s_hour % 12;
  }

  // 背景
  graphics_context_set_fill_color(ctx, s_bg);
  graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);

  // 時と分の隙間の色帯
  {
    GColor band;
    band.argb = s_band_argb;
    graphics_context_set_fill_color(ctx, band);
    graphics_fill_rect(ctx, GRect(BAND_X, BAND_Y, SCREEN_W - BAND_X, BAND_H),
                       0, GCornerNone);
  }

  draw_battery(ctx, s_battery_pct, sweep1000, line1000, shown_pct);
  draw_hour_ring(ctx, hour_deg);
  draw_minute_ring(ctx, min_deg);

  // 時・分の数字 (最上層)
  {
    char mbuf[8], hbuf[8];
    snprintf(mbuf, sizeof(mbuf), "%02d", disp_min == 0 ? 60 : disp_min);
    snprintf(hbuf, sizeof(hbuf), "%d",  disp_hour == 0 ? 12 : disp_hour);
    draw_text_mid(ctx, mbuf, s_font_big, s_ink, CX, MIN_READ_CY,  90);
    draw_text_mid(ctx, hbuf, s_font_big, s_ink, CX, HOUR_READ_CY, 90);
  }

  // 日付・曜日 (左上、分リング r=113 の外側)
  {
    char dbuf[8];
    snprintf(dbuf, sizeof(dbuf), "%d", s_mday);
    draw_text_mid(ctx, dbuf, s_font_24b, s_sub, 28, 18, 44);
    draw_text_mid(ctx, s_day_names[s_wday], s_font_09, s_label, 28, 38, 44);
  }

  // pebble: 時分と同じ縦ライン上、画面下端。リングのラベルを背景色で打ち抜く
  graphics_context_set_fill_color(ctx, s_bg);
  graphics_fill_rect(ctx, GRect(CX - 32, SCREEN_H - 22, 64, 20), 0, GCornerNone);
  draw_text_mid(ctx, "pebble", s_font_14b, s_ink, CX, SCREEN_H - 12, 64);
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
      resource_get_handle(RESOURCE_ID_BrelaDigits_45));
  s_font_mid = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_BrelaDigits_24));
  s_font_24b = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_font_14b = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_font_09  = fonts_get_system_font(FONT_KEY_GOTHIC_09);

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
  layer_destroy(s_canvas_layer);
}

// ── App Entry ─────────────────────────────────────────────
static void init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
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
