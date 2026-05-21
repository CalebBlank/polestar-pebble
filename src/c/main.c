#include <pebble.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
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
// GColorChromeYellow is the closest 64-color to the design's #F5820A orange.
// On 2-bit (Aplite) platforms, we fall back to white-on-black.
#ifdef PBL_COLOR
  #define COLOR_BG    GColorChromeYellow
  #define COLOR_FG    GColorWhite
  #define COLOR_DIM   GColorLightGray
#else
  #define COLOR_BG    GColorBlack
  #define COLOR_FG    GColorWhite
  #define COLOR_DIM   GColorLightGray
#endif

// ── Layout helpers ─────────────────────────────────────────────────────────
// Round watches need inset text areas to avoid the circular clip.
#ifdef PBL_ROUND
  #define INSET_X   24
  #define INSET_TOP 28
#else
  #define INSET_X    8
  #define INSET_TOP  8
#endif

#define HEADER_H  28

// ── App state ─────────────────────────────────────────────────────────────────
static struct {
  bool locked;
  bool climate_on;
  int  charge_min;     // minutes until full; -1 = not charging
  int  charge_pct;
  int  range_km;
  int  odo_km;
  char location[64];
  int  outside_temp;   // Fahrenheit from API
  bool is_charging;
  bool use_metric;
  bool loading;
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
  .loading      = false,
  .error        = false,
};

static int                s_page = PAGE_CLIMATE;
static Window            *s_window;
static Layer             *s_canvas;
static SimpleMenuLayer   *s_action_menu_layer;
static Window            *s_action_window;

// ── Icon drawing ──────────────────────────────────────────────────────────────
// All icons drawn in COLOR_FG into the bottom portion of the screen.
// The icon rect is typically GRect(INSET_X, mid_y, w - INSET_X*2, icon_h).

static void draw_icon_car(GContext *ctx, GRect r) {
  // Body: rounded rectangle
  int bx = r.origin.x, by = r.origin.y + r.size.h / 3;
  int bw = r.size.w,    bh = r.size.h * 2 / 3;
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_round_rect(ctx, GRect(bx, by, bw, bh - 14), 8);

  // Roof: raised section in center
  int rx = bx + bw / 5, ry = by - r.size.h / 4;
  int rw = bw * 3 / 5,  rh = r.size.h / 4 + 4;
  graphics_draw_round_rect(ctx, GRect(rx, ry, rw, rh), 6);

  // Wheels: two filled circles
  int wy = by + bh - 14;
  int wr = 10;
  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_fill_circle(ctx, GPoint(bx + bw / 4, wy), wr);
  graphics_fill_circle(ctx, GPoint(bx + bw * 3 / 4, wy), wr);
  // Wheel cutouts (black)
  graphics_context_set_fill_color(ctx, COLOR_BG);
  graphics_fill_circle(ctx, GPoint(bx + bw / 4, wy), wr - 3);
  graphics_fill_circle(ctx, GPoint(bx + bw * 3 / 4, wy), wr - 3);
}

static void draw_icon_charger(GContext *ctx, GRect r) {
  int cx = r.origin.x + r.size.w / 2;
  int top = r.origin.y;
  int bot = r.origin.y + r.size.h;
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, 3);

  // Plug handle
  graphics_draw_round_rect(ctx, GRect(cx - 20, top, 40, r.size.h * 2 / 3), 6);
  // Prongs
  graphics_draw_line(ctx, GPoint(cx - 8, top - 2), GPoint(cx - 8, top + 12));
  graphics_draw_line(ctx, GPoint(cx + 8, top - 2), GPoint(cx + 8, top + 12));
  // Cable
  graphics_draw_line(ctx, GPoint(cx, top + r.size.h * 2 / 3), GPoint(cx, bot));
}

static void draw_icon_lock(GContext *ctx, GRect r, bool locked) {
  int cx = r.origin.x + r.size.w / 2;
  int bx = cx - 20, bw = 40, bh = 30;
  int by = r.origin.y + r.size.h / 2 - 5;
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_context_set_fill_color(ctx, COLOR_FG);

  // Body
  graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 4, GCornersAll);
  // Shackle
  int shackle_y = locked ? (by - 20) : (by - 24);
  GRect shackle = GRect(cx - 12, shackle_y, 24, 24);
  graphics_context_set_fill_color(ctx, COLOR_BG);
  graphics_fill_rect(ctx, GRect(bx - 2, by, bw + 4, bh / 2), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_draw_arc(ctx, shackle,
                    GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(180));
  if (!locked) {
    // Open shackle shifted right
    graphics_draw_line(ctx,
      GPoint(cx + 12, shackle_y + 12),
      GPoint(cx + 20, shackle_y - 4));
  }
  // Keyhole dot
  graphics_context_set_fill_color(ctx, COLOR_BG);
  graphics_fill_circle(ctx, GPoint(cx, by + bh / 2), 5);
}

static void draw_icon_climate(GContext *ctx, GRect r, bool on) {
  int cx = r.origin.x + r.size.w / 2;
  int cy = r.origin.y + r.size.h / 2;
  int radius = MIN(r.size.w, r.size.h) / 2 - 4;
  graphics_context_set_stroke_color(ctx, on ? COLOR_FG : COLOR_DIM);
  graphics_context_set_stroke_width(ctx, 2);

  // Snowflake / fan: center circle + 6 lines
  graphics_draw_circle(ctx, GPoint(cx, cy), 6);
  for (int i = 0; i < 6; i++) {
    int32_t angle = DEG_TO_TRIGANGLE(i * 60);
    int ex = cx + (radius * sin_lookup(angle) / TRIG_MAX_RATIO);
    int ey = cy - (radius * cos_lookup(angle) / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, GPoint(cx, cy), GPoint(ex, ey));
  }
}

static void draw_icon_globe(GContext *ctx, GRect r) {
  int cx = r.origin.x + r.size.w / 2;
  int cy = r.origin.y + r.size.h / 2;
  int radius = MIN(r.size.w, r.size.h) / 2 - 4;
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, GPoint(cx, cy), radius);
  // Latitude lines
  for (int dy = -radius / 2; dy <= radius / 2; dy += radius / 2) {
    int half_w = (int)(radius * 0.866f); // approx sqrt(r^2 - dy^2)
    graphics_draw_line(ctx,
      GPoint(cx - half_w, cy + dy),
      GPoint(cx + half_w, cy + dy));
  }
  // Longitude (vertical)
  graphics_draw_line(ctx, GPoint(cx, cy - radius), GPoint(cx, cy + radius));
  // Car on globe
  int car_x = cx - 12, car_y = cy - radius / 3;
  graphics_draw_round_rect(ctx, GRect(car_x, car_y, 24, 10), 3);
}

// ── Common layout primitives ──────────────────────────────────────────────────

static void fill_bg(GContext *ctx, GRect bounds) {
  graphics_context_set_fill_color(ctx, COLOR_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
}

static void draw_header(GContext *ctx, GRect bounds, const char *page_label) {
  char time_buf[8];
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  const char *time_str = (time_buf[0] == '0') ? time_buf + 1 : time_buf;

#ifdef PBL_ROUND
  // On round, draw both items just below the top arc
  graphics_context_set_text_color(ctx, COLOR_FG);
  graphics_draw_text(ctx, time_str,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(INSET_X, 10, bounds.size.w / 2, HEADER_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, page_label,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(bounds.size.w / 2, 10, bounds.size.w / 2 - INSET_X, HEADER_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
#else
  // Rectangular: dark strip across the top
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, HEADER_H), 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, time_str,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(6, 4, bounds.size.w / 2, HEADER_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, page_label,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(bounds.size.w / 2, 4, bounds.size.w / 2 - 6, HEADER_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
#endif
}

// content_y is the y to start content (below header)
static int content_start(void) {
  return HEADER_H + INSET_TOP;
}

static void draw_stat(GContext *ctx, GRect bounds,
                      const char *big, const char *label) {
  int y = content_start();
  int w = bounds.size.w - INSET_X * 2;

  graphics_context_set_text_color(ctx, COLOR_FG);

  // Big number
  graphics_draw_text(ctx, big,
                     fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
                     GRect(INSET_X, y, w, 52),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Label below
  graphics_draw_text(ctx, label,
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(INSET_X, y + 52, w, 56),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static void draw_action_hint(GContext *ctx, GRect bounds, const char *hint) {
  graphics_context_set_text_color(ctx, COLOR_FG);
  graphics_draw_text(ctx, hint,
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(INSET_X, bounds.size.h - 20, bounds.size.w - INSET_X * 2, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ── Page renderers ────────────────────────────────────────────────────────────

static void draw_page_climate(GContext *ctx, GRect bounds) {
  draw_header(ctx, bounds, "1/7");

  int y = content_start();
  int w = bounds.size.w - INSET_X * 2;
  graphics_context_set_text_color(ctx, COLOR_FG);

  graphics_draw_text(ctx, s_state.climate_on ? "ON" : "OFF",
                     fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
                     GRect(INSET_X, y, w, 52),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, "climate",
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(INSET_X, y + 52, w, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Outside temperature bottom-left
  char temp_buf[24];
  if (s_state.use_metric) {
    int c = (s_state.outside_temp - 32) * 5 / 9;
    snprintf(temp_buf, sizeof(temp_buf), "outside\n%d°C", c);
  } else {
    snprintf(temp_buf, sizeof(temp_buf), "outside\n%d°F", s_state.outside_temp);
  }
  graphics_draw_text(ctx, temp_buf,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(INSET_X, bounds.size.h - 50, w / 2, 46),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // Climate icon bottom-right
  draw_icon_climate(ctx, GRect(bounds.size.w / 2, bounds.size.h - 54, w / 2 - INSET_X + 8, 50),
                    s_state.climate_on);

  draw_action_hint(ctx, bounds, "\x16 toggle");  // centre dot = middle button hint
}

static void draw_page_lock(GContext *ctx, GRect bounds) {
  draw_header(ctx, bounds, "2/7");

  int y = content_start();
  int w = bounds.size.w - INSET_X * 2;
  graphics_context_set_text_color(ctx, COLOR_FG);

  graphics_draw_text(ctx, s_state.locked ? "locked" : "unlocked",
                     fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(INSET_X, y, w, 36),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Lock icon center
  int icon_y = y + 44;
  int icon_h = bounds.size.h - icon_y - 24;
  draw_icon_lock(ctx, GRect(INSET_X + w / 4, icon_y, w / 2, icon_h), s_state.locked);

  draw_action_hint(ctx, bounds, "\x16 toggle");
}

static void draw_page_charge_time(GContext *ctx, GRect bounds) {
  draw_header(ctx, bounds, "3/7");

  int w = bounds.size.w - INSET_X * 2;
  if (!s_state.is_charging) {
    int y = content_start();
    graphics_context_set_text_color(ctx, COLOR_FG);
    graphics_draw_text(ctx, "NOT",
                       fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
                       GRect(INSET_X, y, w, 52),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, "charging",
                       fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(INSET_X, y + 52, w, 30),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  } else {
    char num[8];
    snprintf(num, sizeof(num), "%d", s_state.charge_min);
    draw_stat(ctx, bounds, num, "minutes\nuntil full");
  }

  // Charger icon
  int icon_y = bounds.size.h - 60;
  draw_icon_charger(ctx, GRect(bounds.size.w - INSET_X - 54, icon_y, 54, 52));
}

static void draw_page_charge_pct(GContext *ctx, GRect bounds) {
  draw_header(ctx, bounds, "4/7");
  char num[8];
  snprintf(num, sizeof(num), "%d%%", s_state.charge_pct);
  draw_stat(ctx, bounds, num, "charged");

  // Car icon bottom
  int icon_y = bounds.size.h - 68;
  draw_icon_car(ctx, GRect(INSET_X, icon_y, bounds.size.w - INSET_X * 2, 60));
}

static void draw_page_range(GContext *ctx, GRect bounds) {
  draw_header(ctx, bounds, "5/7");
  char num[16];
  int val = s_state.use_metric
    ? s_state.range_km
    : (int)(s_state.range_km * 0.621371f);
  snprintf(num, sizeof(num), "%d", val);
  draw_stat(ctx, bounds, num,
            s_state.use_metric ? "kilometer\nrange" : "mile\nrange");

  // Car icon bottom
  int icon_y = bounds.size.h - 68;
  draw_icon_car(ctx, GRect(INSET_X, icon_y, bounds.size.w - INSET_X * 2, 60));
}

static void draw_page_odo(GContext *ctx, GRect bounds) {
  draw_header(ctx, bounds, "6/7");
  char num[16];
  int val = s_state.use_metric
    ? s_state.odo_km
    : (int)(s_state.odo_km * 0.621371f);
  snprintf(num, sizeof(num), "%d", val);
  draw_stat(ctx, bounds, num,
            s_state.use_metric ? "kilometers\ndriven" : "miles\ndriven");

  // Globe icon bottom-right
  int icon_y = bounds.size.h - 68;
  draw_icon_globe(ctx, GRect(bounds.size.w / 2, icon_y, bounds.size.w / 2 - INSET_X, 60));
}

static void draw_page_location(GContext *ctx, GRect bounds) {
  draw_header(ctx, bounds, "7/7");

  int y = content_start();
  int w = bounds.size.w - INSET_X * 2;
  graphics_context_set_text_color(ctx, COLOR_FG);

  graphics_draw_text(ctx, "current\nlocation",
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(INSET_X, y, w, 56),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, s_state.location,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(INSET_X, y + 60, w, bounds.size.h - y - 80),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  draw_action_hint(ctx, bounds, "\x16 actions");
}

// ── Canvas update ─────────────────────────────────────────────────────────────

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  fill_bg(ctx, bounds);

  if (s_state.error) {
    graphics_context_set_text_color(ctx, COLOR_FG);
    graphics_draw_text(ctx, "Error\nCheck phone",
                       fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(INSET_X, bounds.size.h / 2 - 28, bounds.size.w - INSET_X * 2, 56),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
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

  if ((t = dict_find(iter, KEY_ERROR))) {
    s_state.error   = true;
    s_state.loading = false;
    layer_mark_dirty(s_canvas);
    return;
  }

  s_state.error   = false;
  s_state.loading = false;

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

static void up_click(ClickRecognizerRef recognizer, void *context) {
  s_page = (s_page - 1 + PAGE_COUNT) % PAGE_COUNT;
  layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  s_page = (s_page + 1) % PAGE_COUNT;
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
      // Any data page: force a refresh
      s_state.loading = true;
      layer_mark_dirty(s_canvas);
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

  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);
  window_set_click_config_provider(window, click_config_provider);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
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
