/*
 * Ressence Type 3 Clone - Concentric Ring Design v7
 * Pebble Emery (200x228) / SDK 3
 *
 * 背景画像機能追加版:
 *   - スマホからPNG画像を4bpp(16色)で受信して全画面背景表示
 *   - AppMessage + persistent storage で画像を保持
 *   - 起動アニメーション廃止 → メモリ節約
 *   - ハーフトーンマップ廃止 → メモリ節約
 */

#include <pebble.h>

// ── 画面サイズ定数 ────────────────────────────────────────
#define SCREEN_W  260
#define SCREEN_H  260

#define CX        120
#define CY        147

// ── 分リング ──────────────────────────────────────────────
#define MIN_RING_R       99
#define MIN_TICK_MAJ_R   90
#define MIN_TICK_MIN_R   95
#define MIN_NUM_R       113

// ── 日付リング ────────────────────────────────────────────
#define DATE_RING_R    92
#define DATE_TEXT_R    75

// ── バッテリー/万歩計リング ───────────────────────────────
#define BAT_R_OUT     65
#define BAT_R_IN      39

#define SEC_ARC_GOAL  60

// ── 時針ダイアル ──────────────────────────────────────────
#define HOUR_R          39
#define HOUR_TICK_OUT   36
#define HOUR_TICK_MAJ   29
#define HOUR_TICK_MIN   31
#define HOUR_HAND_LEN   37
#define HOUR_HAND_TAIL   6

// ── フレームレート ────────────────────────────────────────
#define FRAME_INTERVAL_MS   16
#define MIN_ROTATE_INTERVAL 2

// ── 背景画像 ─────────────────────────────────────────────
#define BG_W             200
#define BG_H             228
#define BG_ROW_BYTES     100    // 200px × 4bpp / 8 = 100 bytes/row
#define BG_PIXEL_BYTES   22800  // BG_ROW_BYTES × BG_H
#define BG_CHUNK_SIZE    3800   // < 4096 (persist_write_data 上限)
#define BG_NUM_CHUNKS    6      // ceil(22800 / 3800) = 6

// AppMessage キー (package.json の messageKeys 順と一致)
#define MSG_KEY_IMG_CHUNK_IDX  0
#define MSG_KEY_IMG_DATA       1
#define MSG_KEY_IMG_DONE       2
#define MSG_KEY_IMG_REQUEST    3
#define MSG_KEY_IMG_PALETTE    4

// persistent storage キー
#define PERSIST_KEY_BG_DONE    100
#define PERSIST_KEY_BG_PALETTE 200
// キー 0〜5: 画像ピクセルチャンク

// ── グローバル変数 ──────────────────────────────────────
static Window  *s_window;
static Layer   *s_canvas_layer;

static GFont s_font_14b;
static GFont s_font_18b;
static GFont s_font_24b;
static GFont s_font_28b;
static GFont s_font_BrelaDigits_45;

static int  s_min, s_sec, s_hour, s_wday, s_mday;
static int  s_battery_pct = 100;
static int  s_step_count  = 0;
static bool s_show_steps  = false;

// 分目盛り色
#define TICK_COLOR_COUNT 7
static int s_tick_color_idx = 0;
static const uint8_t s_tick_color_argb[TICK_COLOR_COUNT] = {
  GColorWhiteARGB8,
  GColorYellowARGB8,
  GColorCyanARGB8,
  GColorMintGreenARGB8,
  GColorOrangeARGB8,
  GColorLightGrayARGB8,
  GColorRedARGB8,
};

static AppTimer *s_frame_timer = NULL;

// 背景画像
static GBitmap *s_bg_bitmap = NULL;
static GColor   s_bg_palette[16];
static bool     s_bg_ready  = false;

// ── ユーティリティ ──────────────────────────────────────

static GPoint polar_pt(int cx, int cy, int r, int angle_deg) {
  int32_t norm = angle_deg % 360;
  if (norm < 0) norm += 360;
  int32_t angle = norm * TRIG_MAX_ANGLE / 360;
  return GPoint(
    cx + (int)(r * sin_lookup(angle) / TRIG_MAX_RATIO),
    cy - (int)(r * cos_lookup(angle) / TRIG_MAX_RATIO)
  );
}

// ── バッテリー色 ──────────────────────────────────────────
static GColor bat_color(int pct) {
  if (pct >= 85) return GColorCyan;
  if (pct >= 60) return GColorMintGreen;
  if (pct >= 40) return GColorYellow;
  if (pct >= 20) return GColorOrange;
  return GColorRed;
}

// ── 背景画像 ─────────────────────────────────────────────

static void create_bg_bitmap_if_needed(void) {
  if (!s_bg_bitmap) {
    s_bg_bitmap = gbitmap_create_blank(GSize(BG_W, BG_H), GBitmapFormat4BitPalette);
  }
}

static void apply_bg_palette(void) {
  if (s_bg_bitmap) {
    gbitmap_set_palette(s_bg_bitmap, s_bg_palette, false);
  }
}

static void load_bg_from_persist(void) {
  if (!persist_exists(PERSIST_KEY_BG_DONE)) return;

  create_bg_bitmap_if_needed();
  if (!s_bg_bitmap) return;

  // パレット読み込み
  if (persist_exists(PERSIST_KEY_BG_PALETTE)) {
    persist_read_data(PERSIST_KEY_BG_PALETTE, s_bg_palette, sizeof(s_bg_palette));
    apply_bg_palette();
  }

  // ピクセルデータ読み込み (6チャンク)
  uint8_t *dst = gbitmap_get_data(s_bg_bitmap);
  if (!dst) return;

  uint32_t offset = 0;
  for (int k = 0; k < BG_NUM_CHUNKS; k++) {
    if (!persist_exists(k)) break;
    int n = persist_read_data(k, dst + offset, BG_CHUNK_SIZE);
    if (n <= 0) break;
    offset += (uint32_t)n;
  }

  s_bg_ready = true;
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *palette_t    = dict_find(iter, MSG_KEY_IMG_PALETTE);
  Tuple *chunk_idx_t  = dict_find(iter, MSG_KEY_IMG_CHUNK_IDX);
  Tuple *data_t       = dict_find(iter, MSG_KEY_IMG_DATA);
  Tuple *done_t       = dict_find(iter, MSG_KEY_IMG_DONE);

  // パレット受信
  if (palette_t && palette_t->length >= 16) {
    create_bg_bitmap_if_needed();
    memcpy(s_bg_palette, palette_t->value->data, 16);
    apply_bg_palette();
    persist_write_data(PERSIST_KEY_BG_PALETTE, s_bg_palette, 16);
  }

  // ピクセルチャンク受信
  if (chunk_idx_t && data_t) {
    create_bg_bitmap_if_needed();
    uint8_t idx = chunk_idx_t->value->uint8;
    if (idx < BG_NUM_CHUNKS) {
      uint8_t  *src = (uint8_t *)data_t->value->data;
      uint16_t  len = data_t->length;

      // GBitmap の内部バッファに直接書き込み (コピー不要)
      uint8_t *pixel_dst = gbitmap_get_data(s_bg_bitmap);
      if (pixel_dst) {
        uint32_t offset = (uint32_t)idx * BG_CHUNK_SIZE;
        if (offset + len <= BG_PIXEL_BYTES) {
          memcpy(pixel_dst + offset, src, len);
        }
      }
      // persist に保存
      persist_write_data((uint32_t)idx, src, len);
    }
  }

  // 転送完了
  if (done_t) {
    s_bg_ready = true;
    persist_write_int(PERSIST_KEY_BG_DONE, 1);
    layer_mark_dirty(s_canvas_layer);
  }
}

// ── 描画 ──────────────────────────────────────────────

static const char *s_min_labels[12] = {
  "60","5","10","15","20","25","30","35","40","45","50","55"
};

static void draw_minute_ring(GContext *ctx, int min_deg) {
  graphics_context_set_antialiased(ctx, true);

  graphics_context_set_stroke_color(ctx,
    (GColor){ .argb = s_tick_color_argb[s_tick_color_idx] });
  graphics_context_set_stroke_width(ctx, 2);

  for (int i = 0; i < 60; i++) {
    int    ang = i * 6 - min_deg;
    bool   maj = (i % 5 == 0);
    GPoint p1  = polar_pt(CX, CY, MIN_RING_R, ang);
    GPoint p2  = polar_pt(CX, CY, maj ? MIN_TICK_MAJ_R : MIN_TICK_MIN_R, ang);
    graphics_draw_line(ctx, p1, p2);
  }

  graphics_context_set_text_color(ctx, GColorDarkGray);
  for (int i = 0; i < 12; i++) {
    GPoint p = polar_pt(CX, CY, MIN_NUM_R, i * 30 - min_deg);
    graphics_draw_text(ctx, s_min_labels[i], s_font_24b,
                       GRect(p.x - 15, p.y - 16, 30, 20),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

static void draw_date_ring(GContext *ctx, int r, GColor accent) {
  if (r < 2) return;

  GRect rect = GRect(CX - r, CY - r, r * 2, r * 2);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle,
                        r, 0, DEG_TO_TRIGANGLE(360));
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, GPoint(CX, CY), r);

  if (r < 30) return;

  static const char *s_day_strs[] = {
    "1","2","3","4","5","6","7","8","9","10",
    "11","12","13","14","15","16","17","18","19","20",
    "21","22","23","24","25","26","27","28","29","30","31"
  };

  int text_r = r * DATE_TEXT_R / DATE_RING_R;

  for (int i = 1; i <= 31; i++) {
    int    ang = (i - 1) * 360 / 31;
    GPoint p   = polar_pt(CX, CY, text_r, ang);
    bool   cur = (i == s_mday);
    graphics_context_set_text_color(ctx, cur ? accent : GColorDarkGray);
    graphics_draw_text(ctx, s_day_strs[i - 1], s_font_14b,
                       GRect(p.x - 7, p.y - 9, 14, 11),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

static void draw_battery_ring(GContext *ctx, int bat_pct) {
  int ring_w = BAT_R_OUT - BAT_R_IN;
  GRect bg = GRect(CX - BAT_R_OUT, CY - BAT_R_OUT, BAT_R_OUT * 2, BAT_R_OUT * 2);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_radial(ctx, bg, GOvalScaleModeFitCircle, ring_w,
    DEG_TO_TRIGANGLE(180), DEG_TO_TRIGANGLE(360));

  if (bat_pct > 0) {
    int fill_w = bat_pct * ring_w / 100;
    if (fill_w < 1) fill_w = 1;
    graphics_context_set_fill_color(ctx, bat_color(bat_pct));
    graphics_fill_radial(ctx, bg, GOvalScaleModeFitCircle, fill_w,
      DEG_TO_TRIGANGLE(180), DEG_TO_TRIGANGLE(360));
  }

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, GPoint(CX, CY), BAT_R_OUT);
  graphics_draw_circle(ctx, GPoint(CX, CY), BAT_R_IN);
}

static void draw_hour_dial(GContext *ctx, int hour_deg) {
  GRect rect = GRect(CX - HOUR_R, CY - HOUR_R, HOUR_R * 2, HOUR_R * 2);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle,
                        HOUR_R, 0, DEG_TO_TRIGANGLE(360));
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, GPoint(CX, CY), HOUR_R);

  for (int i = 0; i < 12; i++) {
    bool  maj = (i % 3 == 0);
    GPoint p1 = polar_pt(CX, CY, HOUR_TICK_OUT, i * 30);
    GPoint p2 = polar_pt(CX, CY, maj ? HOUR_TICK_MAJ : HOUR_TICK_MIN, i * 30);
    graphics_context_set_stroke_color(ctx, maj ? GColorWhite : GColorDarkGray);
    graphics_context_set_stroke_width(ctx, maj ? 2 : 1);
    graphics_draw_line(ctx, p1, p2);
  }

  {
    static const char *day_names[] = {
      "SUN","MON","TUE","WED","THU","FRI","SAT"
    };
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, day_names[s_wday],
                       s_font_14b,
                       GRect(CX - 23, CY + 6, 46, 20),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  GPoint tip  = polar_pt(CX, CY,  HOUR_HAND_LEN,  hour_deg);
  GPoint tail = polar_pt(CX, CY, -HOUR_HAND_TAIL, hour_deg);
  graphics_context_set_stroke_color(ctx, GColorYellow);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, tail, tip);

  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_circle(ctx, GPoint(CX, CY), 4);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "pebble", s_font_18b,
                     GRect(20, 13, 100, 20),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// ── 虫眼鏡インジケーター ──────────────────────────────────
#define LENS_R   30
#define LENS_CX  CX
#define LENS_CY  (CY - MIN_NUM_R)

static void draw_indicator_bg(GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(LENS_CX, LENS_CY), LENS_R + 3);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(LENS_CX, LENS_CY), LENS_R);
}

static void draw_indicator_text(GContext *ctx, int current_min) {
  char buf[4];
  if (current_min == 0) snprintf(buf, sizeof(buf), "60");
  else                  snprintf(buf, sizeof(buf), "%d", current_min);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, s_font_BrelaDigits_45,
    GRect(LENS_CX - LENS_R, LENS_CY - 32, LENS_R * 2, 52),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_arc(ctx,
    GRect(LENS_CX - LENS_R + 4, LENS_CY - LENS_R + 4,
          (LENS_R - 3) * 2, (LENS_R - 3) * 2),
    GOvalScaleModeFitCircle,
    DEG_TO_TRIGANGLE(160), DEG_TO_TRIGANGLE(280));
}

static void draw_sec_arc(GContext *ctx, int sec, int bat_pct) {
  GRect rect = GRect(CX - BAT_R_OUT, CY - BAT_R_OUT,
                     BAT_R_OUT * 2, BAT_R_OUT * 2);
  int32_t R0 = DEG_TO_TRIGANGLE(0);

  graphics_context_set_antialiased(ctx, true);

  int filled_deg = sec * 180 / SEC_ARC_GOAL;
  if (filled_deg > 0) {
    graphics_context_set_fill_color(ctx, bat_color(bat_pct));
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle,
                         4, R0, DEG_TO_TRIGANGLE(filled_deg));
  }
}

// ── 万歩計ゲージ ──────────────────────────────────────────
static void draw_step_ring(GContext *ctx, int steps) {
  int ring_w = BAT_R_OUT - BAT_R_IN;
  GRect bg = GRect(CX - BAT_R_OUT, CY - BAT_R_OUT, BAT_R_OUT * 2, BAT_R_OUT * 2);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_radial(ctx, bg, GOvalScaleModeFitCircle, ring_w,
    0, DEG_TO_TRIGANGLE(360));

  int fill_deg = (steps * 360) / 10000;
  if (fill_deg > 360) fill_deg = 360;

  if (fill_deg > 0) {
    GColor step_color;
    if (steps >= 10000) step_color = GColorGreen;
    else if (steps >= 7500) step_color = GColorMintGreen;
    else if (steps >= 5000) step_color = GColorCyan;
    else if (steps >= 2500) step_color = GColorYellow;
    else step_color = GColorOrange;

    graphics_context_set_fill_color(ctx, step_color);
    graphics_fill_radial(ctx, bg, GOvalScaleModeFitCircle, ring_w,
      0, DEG_TO_TRIGANGLE(fill_deg));
  }

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, GPoint(CX, CY), BAT_R_OUT);
  graphics_draw_circle(ctx, GPoint(CX, CY), BAT_R_IN);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "STEP", s_font_14b,
                     GRect(CX - 25, 5, 50, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  char step_str[8];
  snprintf(step_str, sizeof(step_str), "%d", steps);
  graphics_context_set_text_color(ctx, GColorCyan);
  graphics_draw_text(ctx, step_str, s_font_24b,
                     GRect(CX - 40, CY + 2, 80, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ── メイン描画 ──────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  // 背景: 画像があれば全画面表示、なければ黒塗り
  if (s_bg_ready && s_bg_bitmap) {
    graphics_draw_bitmap_in_rect(ctx, s_bg_bitmap, GRect(0, 0, BG_W, BG_H));
  } else {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);
  }

  // 現在時刻を直接取得（アニメーションなし）
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int current_min  = t->tm_min;
  int current_sec  = t->tm_sec;
  int current_hour = t->tm_hour;
  int quantized_sec = (current_sec / MIN_ROTATE_INTERVAL) * MIN_ROTATE_INTERVAL;
  int min_deg  = current_min * 6 + quantized_sec * 6 / 60;
  int hour_deg = (current_hour % 12) * 30 + current_min / 2;
  int bat_pct  = s_battery_pct;

  GColor accent = bat_color(bat_pct);

  draw_date_ring(ctx, DATE_RING_R, accent);

  if (s_show_steps) {
    draw_step_ring(ctx, s_step_count);
  } else {
    draw_battery_ring(ctx, bat_pct);
    draw_sec_arc(ctx, s_sec, bat_pct);
  }

  draw_hour_dial(ctx, hour_deg);
  draw_minute_ring(ctx, min_deg);
  draw_indicator_bg(ctx);
  draw_indicator_text(ctx, s_min);
}

// ── フレームタイマー ──────────────────────────────────────
static void frame_timer_proc(void *data) {
  layer_mark_dirty(s_canvas_layer);
  s_frame_timer = app_timer_register(FRAME_INTERVAL_MS, frame_timer_proc, NULL);
}

// ── イベントハンドラ ────────────────────────────────────
static void tick_handler(struct tm *t, TimeUnits u) {
  s_min  = t->tm_min;
  s_sec  = t->tm_sec;
  s_hour = t->tm_hour;
  s_wday = t->tm_wday;
  s_mday = t->tm_mday;
}

static void battery_handler(BatteryChargeState state) {
  s_battery_pct = state.charge_percent;
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  s_step_count = (int)health_service_sum_today(HealthMetricStepCount);
  layer_mark_dirty(s_canvas_layer);
}
#endif

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  s_show_steps = !s_show_steps;
  layer_mark_dirty(s_canvas_layer);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_tick_color_idx = (s_tick_color_idx + 1) % TICK_COLOR_COUNT;
  layer_mark_dirty(s_canvas_layer);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

// ── Window ──────────────────────────────────────────────
static void window_load(Window *window) {
  s_font_14b  = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_font_18b  = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_font_24b  = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_font_28b  = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_font_BrelaDigits_45 = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_BrelaDigits_45));

  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root, s_canvas_layer);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  s_min  = t->tm_min;
  s_sec  = t->tm_sec;
  s_hour = t->tm_hour;
  s_wday = t->tm_wday;
  s_mday = t->tm_mday;

  BatteryChargeState bat = battery_state_service_peek();
  s_battery_pct = bat.charge_percent;

  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  accel_tap_service_subscribe(accel_tap_handler);
  window_set_click_config_provider(s_window, click_config_provider);

#if defined(PBL_HEALTH)
  s_step_count = (int)health_service_sum_today(HealthMetricStepCount);
  health_service_events_subscribe(health_handler, NULL);
#endif

  // AppMessage 設定
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(4096, 512);

  // 保存済み背景画像を読み込み
  load_bg_from_persist();

  // フレームタイマー開始
  s_frame_timer = app_timer_register(FRAME_INTERVAL_MS, frame_timer_proc, NULL);
}

static void window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  accel_tap_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  if (s_frame_timer) {
    app_timer_cancel(s_frame_timer);
    s_frame_timer = NULL;
  }
  if (s_bg_bitmap) {
    gbitmap_destroy(s_bg_bitmap);
    s_bg_bitmap = NULL;
  }
  app_message_deregister_callbacks();
  fonts_unload_custom_font(s_font_BrelaDigits_45);
  layer_destroy(s_canvas_layer);
}

// ── App Entry ──────────────────────────────────────────
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
