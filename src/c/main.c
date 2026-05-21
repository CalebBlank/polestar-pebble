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
#ifdef PBL_ROUND
  #define INSET_X  22
#else
  #define INSET_X   8
#endif
// Content starts just below the StatusBarLayer (16 px) with 2 px gap
#define CONTENT_Y (STATUS_BAR_LAYER_HEIGHT + 2)

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
  .use_metric   = true,
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
  int bw = r.size.w * 7 / 10;
  int bh = bw * 55 / 100;
  int by = r.origin.y + r.size.h / 2 - bh / 4;
  int bx = cx - bw / 2;

  icon_fill(ctx);
  graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 5, GCornersAll);
  icon_stroke(ctx);
  graphics_draw_round_rect(ctx, GRect(bx, by, bw, bh), 5);

  graphics_context_set_fill_color(ctx, COLOR_DARK);
  graphics_fill_circle(ctx, GPoint(cx, by + bh / 2 - 3), 4);
  graphics_fill_rect(ctx, GRect(cx - 2, by + bh / 2 - 1, 5, 8), 0, GCornerNone);

  int sr = bw * 3 / 8;
  int sy = by - sr + 2;
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, 5);
  if (locked) {
    graphics_draw_arc(ctx, GRect(cx - sr, sy, sr * 2, sr * 2),
                      GOvalScaleModeFitCircle,
                      DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(180));
  } else {
    graphics_draw_arc(ctx, GRect(cx - sr + bw / 4, sy - sr / 2, sr * 2, sr * 2),
                      GOvalScaleModeFitCircle,
                      DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(180));
  }
  icon_stroke(ctx);
  if (locked) {
    graphics_draw_arc(ctx, GRect(cx - sr, sy, sr * 2, sr * 2),
                      GOvalScaleModeFitCircle,
                      DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(180));
  }
}

static void draw_icon_climate(GContext *ctx, GRect r, bool on) {
  int cx     = r.origin.x + r.size.w / 2;
  int cy     = r.origin.y + r.size.h / 2;
  int radius = MIN(r.size.w, r.size.h) / 2 - 2;
  GColor col = on ? COLOR_FG : COLOR_DIM;

  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_context_set_fill_color(ctx, col);

  graphics_fill_circle(ctx, GPoint(cx, cy), 5);

  for (int i = 0; i < 6; i++) {
    int32_t angle = DEG_TO_TRIGANGLE(i * 60);
    int ex = cx + (radius * sin_lookup(angle) / TRIG_MAX_RATIO);
    int ey = cy - (radius * cos_lookup(angle) / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, GPoint(cx, cy), GPoint(ex, ey));

    int32_t perp = DEG_TO_TRIGANGLE(i * 60 + 90);
    int mx = cx + ((radius * 6 / 10) * sin_lookup(angle) / TRIG_MAX_RATIO);
    int my = cy - ((radius * 6 / 10) * cos_lookup(angle) / TRIG_MAX_RATIO);
    int dx = (5 * sin_lookup(perp) / TRIG_MAX_RATIO);
    int dy = -(5 * cos_lookup(perp) / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, GPoint(mx - dx, my - dy), GPoint(mx + dx, my + dy));
  }
}

static void draw_icon_globe(GContext *ctx, GRect r) {
  int cx     = r.origin.x + r.size.w / 2;
  int cy     = r.origin.y + r.size.h / 2;
  int radius = MIN(r.size.w, r.size.h) / 2 - 3;

  icon_fill(ctx);
  graphics_fill_circle(ctx, GPoint(cx, cy), radius);
  icon_stroke(ctx);
  graphics_draw_circle(ctx, GPoint(cx, cy), radius);

  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 2);
  for (int dy = -radius / 2; dy <= radius / 2; dy += radius / 2) {
    int hw = radius * 7 / 8;
    graphics_draw_line(ctx, GPoint(cx - hw, cy + dy), GPoint(cx + hw, cy + dy));
  }
  graphics_draw_line(ctx, GPoint(cx, cy - radius + 2), GPoint(cx, cy + radius - 2));

  graphics_context_set_fill_color(ctx, COLOR_DARK);
  graphics_fill_rect(ctx, GRect(cx - 10, cy - radius / 3 - 4, 20, 8), 2, GCornersAll);
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
            GRect(INSET_X, y, w, 52),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);
  draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, y + 52, w, 30),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);
}

// Helper: draw car icon at bottom of screen, with road strip on round watches
static void draw_car_bottom(GContext *ctx, GRect bounds) {
#ifdef PBL_ROUND
  // Car bleeds off the right edge — circular clip handles masking
  int bleed_w = bounds.size.w * 5 / 4;
  int car_h   = 66;
  int car_y   = bounds.size.h - car_h - 24;
  draw_icon_car(ctx, GRect(bounds.size.w / 8, car_y, bleed_w, car_h));
  draw_road_strip(ctx, bounds);
#else
  draw_icon_car(ctx, GRect(INSET_X, bounds.size.h - 62,
                           bounds.size.w - INSET_X * 2, 58));
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
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);
  draw_text(ctx, "climate",
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, y + 52, w / 2, 30),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);

  char temp_buf[16];
  if (s_state.use_metric) {
    int c = (s_state.outside_temp - 32) * 5 / 9;
    snprintf(temp_buf, sizeof(temp_buf), "out %d\xC2\xB0""C", c);
  } else {
    snprintf(temp_buf, sizeof(temp_buf), "out %d\xC2\xB0""F", s_state.outside_temp);
  }
  draw_text(ctx, temp_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18),
            GRect(INSET_X, bounds.size.h - 44, w / 2, 40),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);

  draw_icon_climate(ctx,
    GRect(bounds.size.w / 2 + 4, bounds.size.h - 54, w / 2 - 4, 48),
    s_state.climate_on);
}

static void draw_page_lock(GContext *ctx, GRect bounds) {
  int y = CONTENT_Y;
  int w = bounds.size.w - INSET_X * 2;

  graphics_context_set_text_color(ctx, COLOR_FG);
  draw_text(ctx, s_state.locked ? "locked" : "unlocked",
            fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
            GRect(INSET_X, y, w, 52),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);

  int icon_y = y + 60;
  draw_icon_lock(ctx,
    GRect(INSET_X, icon_y, bounds.size.w - INSET_X * 2, bounds.size.h - icon_y - 8),
    s_state.locked);
}

static void draw_page_charge_time(GContext *ctx, GRect bounds) {
  if (!s_state.is_charging) {
    int y = CONTENT_Y;
    int w = bounds.size.w - INSET_X * 2;
    graphics_context_set_text_color(ctx, COLOR_FG);
    draw_text(ctx, "NOT",
              fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
              GRect(INSET_X, y, w, 52),
              GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);
    draw_text(ctx, "charging",
              fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
              GRect(INSET_X, y + 52, w, 30),
              GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);
  } else {
    char num[8];
    snprintf(num, sizeof(num), "%d", s_state.charge_min);
    draw_big_stat(ctx, bounds, num, "min to full");
  }
  draw_icon_charger(ctx,
    GRect(bounds.size.w - INSET_X - 54, bounds.size.h - 64, 50, 56));
}

static void draw_page_charge_pct(GContext *ctx, GRect bounds) {
  char num[8];
  snprintf(num, sizeof(num), "%d%%", s_state.charge_pct);
  draw_big_stat(ctx, bounds, num, "charged");
  draw_car_bottom(ctx, bounds);
}

static void draw_page_range(GContext *ctx, GRect bounds) {
  char num[16];
  int val = s_state.use_metric
    ? s_state.range_km
    : (int)(s_state.range_km * 621 / 1000);
  snprintf(num, sizeof(num), "%d", val);
  draw_big_stat(ctx, bounds, num,
    s_state.use_metric ? "km range" : "mi range");
  draw_car_bottom(ctx, bounds);
}

static void draw_page_odo(GContext *ctx, GRect bounds) {
  char num[16];
  int val = s_state.use_metric
    ? s_state.odo_km
    : (int)(s_state.odo_km * 621 / 1000);
  snprintf(num, sizeof(num), "%d", val);
  draw_big_stat(ctx, bounds, num,
    s_state.use_metric ? "km driven" : "mi driven");

  int icon_sz = MIN(bounds.size.w - INSET_X * 2, 60);
  draw_icon_globe(ctx,
    GRect(bounds.size.w / 2 - icon_sz / 2, bounds.size.h - icon_sz - 8,
          icon_sz, icon_sz));
}

static void draw_page_location(GContext *ctx, GRect bounds) {
  int y = CONTENT_Y;
  int w = bounds.size.w - INSET_X * 2;
  graphics_context_set_text_color(ctx, COLOR_FG);
  draw_text(ctx, "current\nlocation",
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, y, w, 56),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, 5);
  draw_text(ctx, s_state.location,
            fonts_get_system_font(FONT_KEY_GOTHIC_18),
            GRect(INSET_X, y + 60, w, bounds.size.h - y - 68),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, 5);
  draw_text(ctx, "press for actions",
            fonts_get_system_font(FONT_KEY_GOTHIC_14),
            GRect(INSET_X, bounds.size.h - 18, w, 16),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, 0);
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
