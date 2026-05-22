#include <pebble.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// ── Message / command keys ────────────────────────────────────────────────────
#define KEY_CMD                0
#define KEY_STATE_LOCKED       1
#define KEY_STATE_CLIMATE      2
#define KEY_STATE_CHARGE_MIN   3
#define KEY_STATE_CHARGE_PCT   4
#define KEY_STATE_RANGE_KM     5
#define KEY_STATE_ODO_KM       6
#define KEY_STATE_LOCATION     7
#define KEY_STATE_OUTSIDE_TEMP 8
#define KEY_STATE_IS_CHARGING  9
#define KEY_SETTING_UNITS      10
#define KEY_SETTING_API_KEY    11
#define KEY_ERROR              12

#define CMD_REFRESH        1
#define CMD_TOGGLE_LOCK    2
#define CMD_TOGGLE_CLIMATE 3
#define CMD_HONK           4

// ── Pages ─────────────────────────────────────────────────────────────────────
#define PAGE_CLIMATE     0
#define PAGE_LOCK        1
#define PAGE_CHARGE_TIME 2
#define PAGE_CHARGE_PCT  3
#define PAGE_RANGE       4
#define PAGE_ODO         5
#define PAGE_LOCATION    6
#define PAGE_COUNT       7

// ── Colors ────────────────────────────────────────────────────────────────────
#ifdef PBL_COLOR
  #define COLOR_BG  GColorOrange
#else
  #define COLOR_BG  GColorBlack
#endif
#define COLOR_FG    GColorWhite
#define COLOR_DARK  GColorBlack
#define COLOR_DIM   GColorLightGray

// ── Layout ────────────────────────────────────────────────────────────────────
// Gabbro has rx=18 rounded corners — need wider inset to stay inside the mask
#ifdef PBL_ROUND
  #define INSET_X  22
#elif defined(PBL_PLATFORM_GABBRO)
  #define INSET_X  16
#else
  #define INSET_X   8
#endif
// On round screens the circle is too narrow near the top to fit INSET_X=22;
// drop content lower to where the circle left edge ≈ INSET_X.
#ifdef PBL_ROUND
  #define CONTENT_Y 60
#else
  #define CONTENT_Y (STATUS_BAR_LAYER_HEIGHT + 2)
#endif

// ── App state ─────────────────────────────────────────────────────────────────
static struct {
  bool locked;
  bool climate_on;
  int  charge_min;
  int  charge_pct;
  int  range_km;
  int  odo_km;
  char location[64];
  int  outside_temp;
  bool is_charging;
  bool use_metric;
  bool error;
} s_state = {
  .locked       = true,
  .climate_on   = false,
  .charge_min   = 25,
  .charge_pct   = 80,
  .range_km     = 180,
  .odo_km       = 12305,
  .location     = "136 S Ash St, Palatine IL",
  .outside_temp = 60,
  .is_charging  = true,
  .use_metric   = false,
  .error        = false,
};

static int               s_page = PAGE_CLIMATE;
static Window           *s_window;
static Layer            *s_canvas;
static StatusBarLayer   *s_status_bar;
static TextLayer        *s_page_label;
static char              s_page_buf[8];
static SimpleMenuLayer  *s_action_menu_layer;
static Window           *s_action_window;

// ── Icon drawing ──────────────────────────────────────────────────────────────

static void icon_fill(GContext *ctx) {
  graphics_context_set_fill_color(ctx, COLOR_FG);
}
static void icon_stroke(GContext *ctx) {
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 3);
}

static void draw_icon_car(GContext *ctx, GRect r) {
  int x = r.origin.x, y = r.origin.y, w = r.size.w, h = r.size.h;

  int wr  = h * 3 / 11;
  int wy  = y + h - wr;
  int wlx = x + w / 4;
  int wrx = x + w * 3 / 4;
  int sill_y = wy - h / 5;
  int sill_h = (wy - sill_y) + wr;
  int cab_x = x + w / 7;
  int cab_w = w * 5 / 7;
  int cab_y = y + 2;
  int cab_h = sill_y - cab_y + 6;

  icon_fill(ctx);
  graphics_fill_rect(ctx, GRect(x + 2, sill_y, w - 4, sill_h), 6, GCornersAll);
  graphics_fill_rect(ctx, GRect(cab_x, cab_y, cab_w, cab_h), 12, GCornersTop);
  graphics_fill_circle(ctx, GPoint(wlx, wy), wr);
  graphics_fill_circle(ctx, GPoint(wrx, wy), wr);

  icon_stroke(ctx);
  graphics_draw_round_rect(ctx, GRect(x + 2, sill_y, w - 4, sill_h), 6);
  graphics_draw_round_rect(ctx, GRect(cab_x, cab_y, cab_w, cab_h), 12);
  graphics_draw_circle(ctx, GPoint(wlx, wy), wr);
  graphics_draw_circle(ctx, GPoint(wrx, wy), wr);

  // White stroke erases the cabin/sill seam → unified silhouette
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, 5);
  graphics_draw_line(ctx,
    GPoint(cab_x + 5, sill_y),
    GPoint(cab_x + cab_w - 5, sill_y));

  graphics_context_set_fill_color(ctx, COLOR_DARK);
  graphics_fill_circle(ctx, GPoint(wlx, wy), 4);
  graphics_fill_circle(ctx, GPoint(wrx, wy), 4);
}

static void draw_icon_charger(GContext *ctx, GRect r) {
  int cx  = r.origin.x + r.size.w / 2;
  int ty  = r.origin.y;
  int by  = r.origin.y + r.size.h;
  int hw  = r.size.w * 9 / 20;   // half-width of body
  int hh  = r.size.h * 11 / 20;  // height of body
  int prong_x = hw / 3;

  icon_fill(ctx);
  graphics_fill_rect(ctx, GRect(cx - hw, ty + 6, hw * 2, hh), 6, GCornersAll);
  icon_stroke(ctx);
  graphics_draw_round_rect(ctx, GRect(cx - hw, ty + 6, hw * 2, hh), 6);

  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(cx - prong_x, ty), GPoint(cx - prong_x, ty + 8));
  graphics_draw_line(ctx, GPoint(cx + prong_x, ty), GPoint(cx + prong_x, ty + 8));

  int tip_r = MAX(r.size.w / 8, 5);
  graphics_draw_line(ctx, GPoint(cx, ty + 6 + hh), GPoint(cx, by - tip_r));

  icon_fill(ctx);
  graphics_fill_circle(ctx, GPoint(cx, by - tip_r), tip_r);
  icon_stroke(ctx);
  graphics_draw_circle(ctx, GPoint(cx, by - tip_r), tip_r);
}

static void draw_icon_lock(GContext *ctx, GRect r, bool locked) {
  int cx = r.origin.x + r.size.w / 2;

  // Scale body to fit available height. From Figma: body=72×64, shackle adds
  // arm_h(21)+arc_r(16)=37px above body → total ~101px for bw=72.
  int max_bw = r.size.h * 5 / 7;
  int bw = MIN(MIN(r.size.w * 7 / 10, 72), max_bw);
  int bh     = bw * 8 / 9;    // 72→64px (matches Figma body height exactly)
  int arm_h  = bw * 29 / 100; // 72→21px (arc center to body top)
  int arc_r  = bw * 22 / 100; // 72→16px (center-of-stroke shackle radius)
  int arm_sw = bw * 19 / 100; // 72→14px (shackle stroke width, Figma outer-inner)
  arm_sw = MAX(arm_sw, 4);

  // Center the entire icon (arc top → body bottom) vertically in r
  int total_h = arc_r + arm_h + bh;
  int by = r.origin.y + (r.size.h - total_h) / 2 + arc_r + arm_h;
  int bx = cx - bw / 2;

  // Shackle arc center: body_top minus arm height
  // Unlocked: shift arc well off the right edge so it disappears
  int arc_cx = locked ? cx : cx + bw * 2;
  int arc_cy = by - arm_h;

  // Shackle arms (filled rects, white, extend from arc center down to body top)
  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_fill_rect(ctx,
    GRect(arc_cx - arc_r - arm_sw / 2, arc_cy, arm_sw, by - arc_cy + 2), 0, GCornerNone);
  graphics_fill_rect(ctx,
    GRect(arc_cx + arc_r - arm_sw / 2, arc_cy, arm_sw, by - arc_cy + 2), 0, GCornerNone);

  // Shackle arc: top semicircle drawn as two 90° quarters to avoid wrap-around issues.
  // 270°→360° = left quarter (9→12 o'clock), 0°→90° = right quarter (12→3 o'clock).
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, arm_sw);
  {
    GRect arc_rect = GRect(arc_cx - arc_r, arc_cy - arc_r, arc_r * 2, arc_r * 2);
    graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle,
                      DEG_TO_TRIGANGLE(270), DEG_TO_TRIGANGLE(360));
    graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle,
                      DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(90));
  }

  // Lock body: white fill + black outline
  icon_fill(ctx);
  graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 5, GCornersAll);
  icon_stroke(ctx);
  graphics_draw_round_rect(ctx, GRect(bx, by, bw, bh), 5);

  // Keyhole: horizontal black line centered in body (matches Figma Frame 9 x=94-106, y=125)
  int kl = bw * 9 / 100; // half-length ≈6px at bw=72
  int ky = by + bh * 52 / 100;
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, MAX(arm_sw * 2 / 5, 3));
  graphics_draw_line(ctx, GPoint(cx - kl, ky), GPoint(cx + kl, ky));
}


// Draw two triangular mountain peaks behind the car.
static void draw_triangle(GContext *ctx, GPoint a, GPoint b, GPoint c) {
  GPoint pts[3] = {a, b, c};
  GPathInfo info = { .num_points = 3, .points = pts };
  GPath *path = gpath_create(&info);
  gpath_draw_filled(ctx, path);
  gpath_draw_outline(ctx, path);
  gpath_destroy(path);
}

static void draw_mountains(GContext *ctx, GRect bounds) {
  int w = bounds.size.w;
  int h = bounds.size.h;
  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 2);
  draw_triangle(ctx, GPoint(-5, h), GPoint(w * 29 / 100, h - 76), GPoint(w * 62 / 100, h));
  draw_triangle(ctx, GPoint(w * 33 / 100, h), GPoint(w * 70 / 100, h - 94), GPoint(w + 5, h));
}

// Draw a dome hill at the bottom with a car perched on top.
static void draw_hills_and_car(GContext *ctx, GRect bounds) {
  int w = bounds.size.w;
  int h = bounds.size.h;
  int hill_r = w * 3 / 4;
  GPoint hill_center = GPoint(w / 2, h - 80 + hill_r);
  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_fill_circle(ctx, hill_center, hill_r);
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, hill_center, hill_r);
  int car_h = 50, car_w = 96;
  int dome_top = h - 80;
  draw_icon_car(ctx, GRect(w / 2 - car_w / 2, dome_top - car_h + 4, car_w, car_h));
}

// On round watches, draw a white road strip with black top edge at screen bottom.
// The circular hardware clip masks the ends automatically.
#ifdef PBL_ROUND
static void draw_road_strip(GContext *ctx, GRect bounds) {
  int strip_h = 22;
  int y = bounds.size.h - strip_h;
  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_fill_rect(ctx, GRect(0, y, bounds.size.w, strip_h), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(0, y), GPoint(bounds.size.w, y));
}
#endif

// ── Text helper (round screen flow) ──────────────────────────────────────────
static void draw_text(GContext *ctx, const char *text, GFont font, GRect rect,
                      GTextOverflowMode overflow, GTextAlignment align, uint8_t inset) {
#ifdef PBL_ROUND
  if (inset > 0) {
    GTextAttributes *attrs = graphics_text_attributes_create();
    graphics_text_attributes_enable_screen_text_flow(attrs, inset);
    graphics_draw_text(ctx, text, font, rect, overflow, align, attrs);
    graphics_text_attributes_destroy(attrs);
    return;
  }
#endif
  graphics_draw_text(ctx, text, font, rect, overflow, align, NULL);
}

// Helper: draw LECO number + label below it
static void draw_big_stat(GContext *ctx, GRect bounds,
                          const char *number, const char *label) {
  int y = CONTENT_Y;
  int w = bounds.size.w - INSET_X * 2;

  graphics_context_set_text_color(ctx, COLOR_FG);
  draw_text(ctx, number, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
            GRect(INSET_X, y, w, 56),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);
  draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, y + 56, w, 68),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, 5);
}

// Helper: draw car icon at bottom of screen, with road strip on round watches
static void draw_car_bottom(GContext *ctx, GRect bounds) {
#ifdef PBL_ROUND
  int car_w = 160;
  int car_h = 64;
  int car_x = bounds.size.w / 2 - car_w / 2;
  int car_y = bounds.size.h - car_h - 14;
  draw_icon_car(ctx, GRect(car_x, car_y, car_w, car_h));
  draw_road_strip(ctx, bounds);
#else
  int car_w = MIN(bounds.size.w - INSET_X * 2, 150);
  int car_x = bounds.size.w / 2 - car_w / 2;
  draw_icon_car(ctx, GRect(car_x, bounds.size.h - 68, car_w, 62));
#endif
}

// ── Page renderers ────────────────────────────────────────────────────────────

static void draw_page_climate(GContext *ctx, GRect bounds) {
  int y = CONTENT_Y;
  int w = bounds.size.w - INSET_X * 2;

  graphics_context_set_text_color(ctx, COLOR_FG);
  draw_text(ctx, s_state.climate_on ? "ON" : "OFF",
            fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
            GRect(INSET_X, y, w, 52),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);
  draw_text(ctx, "climate",
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, y + 52, w, 32),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);

  // Divider line
  int div_y = y + 52 + 32 + 10;
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(INSET_X, div_y), GPoint(bounds.size.w - INSET_X, div_y));

  // Outside temperature below divider
  char temp_buf[24];
  if (s_state.use_metric) {
    int c = (s_state.outside_temp - 32) * 5 / 9;
    snprintf(temp_buf, sizeof(temp_buf), "%d\xC2\xB0""C outside", c);
  } else {
    snprintf(temp_buf, sizeof(temp_buf), "%d\xC2\xB0""F outside", s_state.outside_temp);
  }
  draw_text(ctx, temp_buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, div_y + 8, w, 36),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);
}

static void draw_page_lock(GContext *ctx, GRect bounds) {
  int w = bounds.size.w - INSET_X * 2;
  int lbl_h = 32;
  int gap = 6;
  int icon_y = CONTENT_Y + 2;
  int icon_h = bounds.size.h - icon_y - lbl_h - gap - 8;

  draw_icon_lock(ctx,
    GRect(INSET_X, icon_y, w, icon_h),
    s_state.locked);

  graphics_context_set_text_color(ctx, COLOR_FG);
  draw_text(ctx, s_state.locked ? "locked" : "unlocked",
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, icon_y + icon_h + gap, w, lbl_h),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, 5);
}

static void draw_page_charge_time(GContext *ctx, GRect bounds) {
  if (!s_state.is_charging) {
    int y = CONTENT_Y;
    int w = bounds.size.w - INSET_X * 2;
    graphics_context_set_text_color(ctx, COLOR_FG);
    draw_text(ctx, "NOT",
              fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
              GRect(INSET_X, y, w, 52),
              GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);
    draw_text(ctx, "charging",
              fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
              GRect(INSET_X, y + 52, w, 30),
              GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);
  } else {
    char num[8];
    snprintf(num, sizeof(num), "%d", s_state.charge_min);
    draw_big_stat(ctx, bounds, num, "minutes\nuntil full");
  }
#ifdef PBL_ROUND
  draw_icon_charger(ctx,
    GRect(bounds.size.w / 2 + 14, bounds.size.h - 116, 88, 108));
#else
  draw_icon_charger(ctx,
    GRect(bounds.size.w - INSET_X - 78, bounds.size.h - 96, 74, 88));
#endif
}

// Draws a % glyph sized to sit beside LECO_42_NUMBERS digits.
// LECO_42_NUMBERS only contains digit glyphs (no %) so we draw it manually.
// Blocky % glyph — two filled squares + thick diagonal — matches LECO's geometric weight.
static void draw_percent_glyph(GContext *ctx, GPoint origin, int size) {
  int sq = MAX(size * 3 / 10, 4);  // square size (~8px at size=28)
  int sw = MAX(size / 6,      3);  // diagonal stroke width (~5px at size=28)
  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_fill_rect(ctx, GRect(origin.x,                  origin.y,                  sq, sq), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + size - sq,      origin.y + size - sq,      sq, sq), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, sw);
  graphics_draw_line(ctx,
    GPoint(origin.x + size - 1, origin.y + 1),
    GPoint(origin.x + 1,        origin.y + size - 1));
}

static void draw_page_charge_pct(GContext *ctx, GRect bounds) {
  char num[8];
  snprintf(num, sizeof(num), "%d", s_state.charge_pct);
  draw_big_stat(ctx, bounds, num, "charged");
  // LECO_42_NUMBERS digits are ~26px wide each; draw % glyph after the number
  int digits   = s_state.charge_pct >= 100 ? 3 : (s_state.charge_pct >= 10 ? 2 : 1);
  int glyph_sz = 28;
  draw_percent_glyph(ctx,
    GPoint(INSET_X + digits * 26 + 4, CONTENT_Y + 8),
    glyph_sz);
  draw_car_bottom(ctx, bounds);
}

static void draw_page_range(GContext *ctx, GRect bounds) {
  char num[16];
  int val = s_state.use_metric
    ? s_state.range_km
    : (int)(s_state.range_km * 621 / 1000);
  snprintf(num, sizeof(num), "%d", val);
  draw_big_stat(ctx, bounds, num,
    s_state.use_metric ? "kilometer\nrange" : "mile\nrange");
  draw_mountains(ctx, bounds);
  {
    int car_w = 140;
    int car_h = 54;
    int car_x = bounds.size.w / 2 - car_w / 2;
    int car_y = bounds.size.h - car_h - 24;
    draw_icon_car(ctx, GRect(car_x, car_y, car_w, car_h));
  }
#ifdef PBL_ROUND
  draw_road_strip(ctx, bounds);
#endif
}

static void draw_page_odo(GContext *ctx, GRect bounds) {
  char num[16];
  int val = s_state.use_metric
    ? s_state.odo_km
    : (int)(s_state.odo_km * 621 / 1000);
  snprintf(num, sizeof(num), "%d", val);
  draw_big_stat(ctx, bounds, num,
    s_state.use_metric ? "kilometers\ndriven" : "miles\ndriven");

  draw_hills_and_car(ctx, bounds);
}

static void draw_page_location(GContext *ctx, GRect bounds) {
  int y = CONTENT_Y;
  int w = bounds.size.w - INSET_X * 2;
  graphics_context_set_text_color(ctx, COLOR_FG);
  draw_text(ctx, s_state.location,
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, y, w, 90),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, 5);
  draw_text(ctx, "current location",
            fonts_get_system_font(FONT_KEY_GOTHIC_18),
            GRect(INSET_X, y + 90, w, 26),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);
}

// ── Canvas update ─────────────────────────────────────────────────────────────

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, COLOR_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_state.error) {
    graphics_context_set_text_color(ctx, COLOR_FG);
    draw_text(ctx, "Error\nCheck phone",
              fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
              GRect(INSET_X, bounds.size.h / 2 - 28, bounds.size.w - INSET_X * 2, 56),
              GTextOverflowModeWordWrap, GTextAlignmentCenter, 5);
    return;
  }

  switch (s_page) {
    case PAGE_CLIMATE:     draw_page_climate(ctx, bounds);     break;
    case PAGE_LOCK:        draw_page_lock(ctx, bounds);        break;
    case PAGE_CHARGE_TIME: draw_page_charge_time(ctx, bounds); break;
    case PAGE_CHARGE_PCT:  draw_page_charge_pct(ctx, bounds);  break;
    case PAGE_RANGE:       draw_page_range(ctx, bounds);       break;
    case PAGE_ODO:         draw_page_odo(ctx, bounds);         break;
    case PAGE_LOCATION:    draw_page_location(ctx, bounds);    break;
  }
}

// ── AppMessage ────────────────────────────────────────────────────────────────

static void send_cmd(int cmd) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_int(iter, KEY_CMD, &cmd, sizeof(int), true);
  app_message_outbox_send();
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  s_state.error = false;

  if ((t = dict_find(iter, KEY_ERROR))) {
    s_state.error = true;
    layer_mark_dirty(s_canvas);
    return;
  }

  if ((t = dict_find(iter, KEY_STATE_LOCKED)))       s_state.locked       = (bool)t->value->int32;
  if ((t = dict_find(iter, KEY_STATE_CLIMATE)))      s_state.climate_on   = (bool)t->value->int32;
  if ((t = dict_find(iter, KEY_STATE_CHARGE_MIN)))   s_state.charge_min   = t->value->int32;
  if ((t = dict_find(iter, KEY_STATE_CHARGE_PCT)))   s_state.charge_pct   = t->value->int32;
  if ((t = dict_find(iter, KEY_STATE_RANGE_KM)))     s_state.range_km     = t->value->int32;
  if ((t = dict_find(iter, KEY_STATE_ODO_KM)))       s_state.odo_km       = t->value->int32;
  if ((t = dict_find(iter, KEY_STATE_OUTSIDE_TEMP))) s_state.outside_temp = t->value->int32;
  if ((t = dict_find(iter, KEY_STATE_IS_CHARGING)))  s_state.is_charging  = (bool)t->value->int32;
  if ((t = dict_find(iter, KEY_SETTING_UNITS)))      s_state.use_metric   = (bool)t->value->int32;
  if ((t = dict_find(iter, KEY_STATE_LOCATION)))
    snprintf(s_state.location, sizeof(s_state.location), "%s", t->value->cstring);

  layer_mark_dirty(s_canvas);
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ── Location action menu ──────────────────────────────────────────────────────

static void action_menu_select_cb(int index, void *context) {
  if (index == 0) {
    vibes_short_pulse();
    send_cmd(CMD_HONK);
  }
  window_stack_pop(true);
}

static void action_window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  static SimpleMenuItem items[] = {
    { .title = "Honk + Flash", .subtitle = "Locate your car", .callback = action_menu_select_cb },
  };
  static SimpleMenuSection sections[] = {
    { .title = "Actions", .items = items, .num_items = 1 },
  };

  s_action_menu_layer = simple_menu_layer_create(bounds, window, sections, 1, NULL);
  layer_add_child(root, simple_menu_layer_get_layer(s_action_menu_layer));
}

static void action_window_unload(Window *window) {
  simple_menu_layer_destroy(s_action_menu_layer);
  s_action_menu_layer = NULL;
  window_destroy(s_action_window);
  s_action_window = NULL;
}

static void open_action_menu(void) {
  s_action_window = window_create();
  window_set_window_handlers(s_action_window, (WindowHandlers){
    .load   = action_window_load,
    .unload = action_window_unload,
  });
  window_stack_push(s_action_window, true);
}

// ── Button handlers ───────────────────────────────────────────────────────────

static void update_page_indicator(void) {
  snprintf(s_page_buf, sizeof(s_page_buf), "%d/%d", s_page + 1, PAGE_COUNT);
  text_layer_set_text(s_page_label, s_page_buf);
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  s_page = (s_page - 1 + PAGE_COUNT) % PAGE_COUNT;
  update_page_indicator();
  layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  s_page = (s_page + 1) % PAGE_COUNT;
  update_page_indicator();
  layer_mark_dirty(s_canvas);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  switch (s_page) {
    case PAGE_CLIMATE:
      s_state.climate_on = !s_state.climate_on;
      layer_mark_dirty(s_canvas);
      send_cmd(CMD_TOGGLE_CLIMATE);
      break;
    case PAGE_LOCK:
      s_state.locked = !s_state.locked;
      layer_mark_dirty(s_canvas);
      send_cmd(CMD_TOGGLE_LOCK);
      break;
    case PAGE_LOCATION:
      open_action_menu();
      break;
    default:
      send_cmd(CMD_REFRESH);
      break;
  }
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,     up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

// ── Window lifecycle ──────────────────────────────────────────────────────────

static void window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  // Full-screen canvas (drawn behind the status bar layer)
  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);

  // Native status bar — auto-updates time, orange bg, white fg, no separator
  s_status_bar = status_bar_layer_create();
  status_bar_layer_set_colors(s_status_bar, COLOR_BG, COLOR_FG);
  status_bar_layer_set_separator_mode(s_status_bar, StatusBarLayerSeparatorModeNone);
  layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

  // Page indicator overlaid on the right side of the status bar row
  int lbl_w = 36;
  s_page_label = text_layer_create(
    GRect(bounds.size.w - lbl_w - 2, 0, lbl_w, STATUS_BAR_LAYER_HEIGHT));
  text_layer_set_background_color(s_page_label, GColorClear);
  text_layer_set_text_color(s_page_label, COLOR_FG);
  text_layer_set_font(s_page_label, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_page_label, GTextAlignmentRight);
  layer_add_child(root, text_layer_get_layer(s_page_label));

  update_page_indicator();
  window_set_click_config_provider(window, click_config_provider);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  status_bar_layer_destroy(s_status_bar);
  text_layer_destroy(s_page_label);
}

// ── App lifecycle ─────────────────────────────────────────────────────────────

static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_open(512, 512);
  window_stack_push(s_window, true);
  send_cmd(CMD_REFRESH);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
