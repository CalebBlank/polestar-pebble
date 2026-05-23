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
#define KEY_STATE_DISTANCE_M   13
#define KEY_SETTING_LIGHT_TEXT 14

#define PERSIST_KEY_LIGHT_TEXT 1
#define PERSIST_KEY_USE_METRIC 2

#define CMD_REFRESH        1
#define CMD_TOGGLE_LOCK    2
#define CMD_TOGGLE_CLIMATE 3
#define CMD_HONK           4
#define CMD_NAVIGATE       5

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
static GColor s_color_fg;    // icon/illustration fill (always white on color devices)
static GColor s_color_text;  // text and UI lines (black when light-text mode is on)
static GColor s_color_bg;
static GColor s_color_dark;
static GColor s_color_dim;
#define COLOR_FG   s_color_fg
#define COLOR_TEXT s_color_text
#define COLOR_BG   s_color_bg
#define COLOR_DARK s_color_dark
#define COLOR_DIM  s_color_dim

// ── Layout ────────────────────────────────────────────────────────────────────
// Gabbro has rx=18 rounded corners — need wider inset to stay inside the mask
#ifdef PBL_ROUND
  #define INSET_X  34
#elif defined(PBL_PLATFORM_GABBRO)
  #define INSET_X  18
#else
  #define INSET_X  14
#endif
// On round screens the circle is too narrow near the top to fit INSET_X=22;
// drop content lower to where the circle left edge ≈ INSET_X.
#ifdef PBL_ROUND
  #define CONTENT_Y 44
#elif defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  #define CONTENT_Y (STATUS_BAR_LAYER_HEIGHT + 2)
#else
  #define CONTENT_Y (STATUS_BAR_LAYER_HEIGHT - 5)
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
  int  distance_m;
} s_state = {
  .locked       = true,
  .climate_on   = false,
  .charge_min   = 25,
  .charge_pct   = 80,
  .range_km     = 180,
  .odo_km       = 12305,
  .location     = "136 S Ash St\nPalatine, IL",
  .outside_temp = 60,
  .is_charging  = true,
  .use_metric   = false,
  .error        = false,
  .distance_m   = -1,
};

static int               s_page = PAGE_CLIMATE;
static Window           *s_window;
static Layer            *s_canvas;
static StatusBarLayer   *s_status_bar;
static TextLayer        *s_page_label;
static char              s_page_buf[8];
static bool              s_animating       = false;
static Layer            *s_anim_layer      = NULL;
static int               s_anim_from_page  = 0;
typedef struct { int32_t x, y, w, rot; } CarState;
static CarState          s_car_cur;          // current (animated) state
static CarState          s_car_phase[2];     // [0]=start [1]=target
static Layer            *s_car_layer        = NULL;
static bool              s_ground_morph     = false;
static int32_t           s_ground_morph_p   = 0;
static int32_t           s_cable_anim_p     = 0;
static bool              s_globe_spinning   = false;
static int32_t           s_globe_rot        = 0;
static bool              s_lock_shaking     = false;
static int32_t           s_lock_shake_p     = 0;
static bool              s_lock_toggling    = false;
static int32_t           s_lock_toggle_p    = 0;
static bool              s_lock_toggle_from = false;
static int32_t           s_transition_p     = 0;
static int               s_anim_dir         = 1;
// ── Icon drawing ──────────────────────────────────────────────────────────────

static void icon_fill(GContext *ctx) {
  graphics_context_set_fill_color(ctx, COLOR_FG);
}
static void icon_stroke(GContext *ctx) {
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 4);
}

// rot_angle: 0 = flat, TRIG_MAX_ANGLE*335/360 = 25° CCW (front/right tilts up).
// wheel_r_override: >0 overrides default wheel radius (MAX(h/4,10)); 0 = use default.
// Path points are centered around the body pivot so gpath_rotate_to works correctly.
static void draw_icon_car(GContext *ctx, GRect r, int32_t rot_angle, int wheel_r_override) {
  int x = r.origin.x, y = r.origin.y, w = r.size.w, h = r.size.h;
  int wheel_r = wheel_r_override > 0 ? wheel_r_override : MAX(h / 5, 5);
  int body_h  = h - wheel_r;
  // Pivot = center of car body rectangle
  int pcx = w / 2;
  int pcy = body_h / 2;
  int body_y_off = h / 18;  // shift body down slightly so it sits lower between wheels

  GPoint pts[] = {
    {(int16_t)(  5*w/161 - pcx), (int16_t)(19*body_h/54 - pcy)},
    {(int16_t)(  3*w/161 - pcx), (int16_t)(43*body_h/54 - pcy)},
    {(int16_t)( 30*w/161 - pcx), (int16_t)(51*body_h/54 - pcy)},
    {(int16_t)(136*w/161 - pcx), (int16_t)(51*body_h/54 - pcy)},
    {(int16_t)(158*w/161 - pcx), (int16_t)(48*body_h/54 - pcy)},
    {(int16_t)(156*w/161 - pcx), (int16_t)(27*body_h/54 - pcy)},
    {(int16_t)(119*w/161 - pcx), (int16_t)(19*body_h/54 - pcy)},
    {(int16_t)( 96*w/161 - pcx), (int16_t)( 7*body_h/54 - pcy)},
    {(int16_t)( 92*w/161 - pcx), (int16_t)( 5*body_h/54 - pcy)},
    {(int16_t)( 89*w/161 - pcx), (int16_t)( 4*body_h/54 - pcy)},
    {(int16_t)( 85*w/161 - pcx), (int16_t)( 3*body_h/54 - pcy)},
    {(int16_t)( 81*w/161 - pcx), (int16_t)( 3*body_h/54 - pcy)},
    {(int16_t)( 52*w/161 - pcx), (int16_t)( 3*body_h/54 - pcy)},
    {(int16_t)( 48*w/161 - pcx), (int16_t)( 3*body_h/54 - pcy)},
    {(int16_t)( 44*w/161 - pcx), (int16_t)( 4*body_h/54 - pcy)},
    {(int16_t)( 41*w/161 - pcx), (int16_t)( 5*body_h/54 - pcy)},
    {(int16_t)( 37*w/161 - pcx), (int16_t)( 6*body_h/54 - pcy)},
  };
  GPathInfo info = { .num_points = 17, .points = pts };
  GPath *path = gpath_create(&info);
  if (rot_angle != 0) gpath_rotate_to(path, rot_angle);
  gpath_move_to(path, GPoint(x + pcx, y + pcy + body_y_off));

  icon_fill(ctx);   gpath_draw_filled(ctx, path);
  icon_stroke(ctx); gpath_draw_outline(ctx, path);
  gpath_destroy(path);

  // Wheel positions: relative to pivot, then rotate if needed
  int fw_rx =  35 * w / 161 - pcx;
  int rw_rx = 126 * w / 161 - pcx;
  int  w_ry = body_h - pcy;
  int front_wx, front_wy, rear_wx, rear_wy;
  if (rot_angle != 0) {
    int32_t ca = cos_lookup(rot_angle);
    int32_t sa = sin_lookup(rot_angle);
    front_wx = x + pcx + (int32_t)fw_rx * ca / TRIG_MAX_RATIO - (int32_t)w_ry * sa / TRIG_MAX_RATIO;
    front_wy = y + pcy + (int32_t)fw_rx * sa / TRIG_MAX_RATIO + (int32_t)w_ry * ca / TRIG_MAX_RATIO;
    rear_wx  = x + pcx + (int32_t)rw_rx * ca / TRIG_MAX_RATIO - (int32_t)w_ry * sa / TRIG_MAX_RATIO;
    rear_wy  = y + pcy + (int32_t)rw_rx * sa / TRIG_MAX_RATIO + (int32_t)w_ry * ca / TRIG_MAX_RATIO;
  } else {
    front_wx = x + 35  * w / 161;  front_wy = y + body_h;
    rear_wx  = x + 126 * w / 161;  rear_wy  = y + body_h;
  }
  icon_fill(ctx);
  graphics_fill_circle(ctx, GPoint(front_wx, front_wy), wheel_r);
  graphics_fill_circle(ctx, GPoint(rear_wx,  rear_wy),  wheel_r);
  icon_stroke(ctx);
  graphics_draw_circle(ctx, GPoint(front_wx, front_wy), wheel_r);
  graphics_draw_circle(ctx, GPoint(rear_wx,  rear_wy),  wheel_r);
}

// Cable hangs from the charge port (rear/left of car): exits leftward, droops
// down-left, then exits off the left edge at ground level.
static void draw_charging_cable(GContext *ctx, int car_x, int car_y,
                                int car_w, int car_h, int bounds_w, int bounds_h,
                                int32_t morph_p) {
  (void)bounds_w;
#define CABLE_LERP(a,b,p) ((a) + (int32_t)((int64_t)((b)-(a)) * (p) / ANIMATION_NORMALIZED_MAX))
  int wheel_r = MAX(car_h / 4, 10);
  int body_h  = car_h - wheel_r;
  // Charge port: rear-left side of car body
  int port_x  = car_x + 8 * car_w / 161 + 20;
  int port_y  = car_y + 32 * body_h / 54;
#ifdef PBL_ROUND
  int ground_y = bounds_h - 22;
#else
  int ground_y = car_y + car_h;
#endif
  // Start: port position (plug falls to ground as morph_p increases)
  int sx = port_x;
  int sy = (int)CABLE_LERP(port_y, ground_y, morph_p);
  // End: off-screen left at ground level
  int ex = (int)CABLE_LERP(-8, -(car_x + car_w + 20), morph_p);
  int ey = ground_y;
  // c1: pull left from start so cable initially exits leftward
  int c1x = sx - (int)CABLE_LERP(40, 10, morph_p);
  int c1y = sy;
  // c2: approach end from above so cable curves down before going flat
  int c2x = ex + (int)CABLE_LERP(30, 8, morph_p);
  int c2y = ey - (int)CABLE_LERP(28, 4, morph_p);

  // Two-pass: black outline first, white fill on top
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 0) {
      graphics_context_set_stroke_color(ctx, COLOR_DARK);
      graphics_context_set_stroke_width(ctx, 12);
    } else {
      graphics_context_set_stroke_color(ctx, COLOR_FG);
      graphics_context_set_stroke_width(ctx, 4);
    }
    GPoint prev = GPoint(sx, sy);
    for (int i = 1; i <= 12; i++) {
      int t_ = i * 64 / 12, mt_ = 64 - t_;
      // Cubic bezier: (1-t)³P0 + 3(1-t)²tP1 + 3(1-t)t²P2 + t³P3  (t in [0,64])
      int px = (int)(((int64_t)mt_*mt_*mt_*sx + 3*(int64_t)mt_*mt_*t_*c1x +
                       3*(int64_t)mt_*t_*t_*c2x + (int64_t)t_*t_*t_*ex) / (64*64*64));
      int py = (int)(((int64_t)mt_*mt_*mt_*sy + 3*(int64_t)mt_*mt_*t_*c1y +
                       3*(int64_t)mt_*t_*t_*c2y + (int64_t)t_*t_*t_*ey) / (64*64*64));
      graphics_draw_line(ctx, prev, GPoint(px, py));
      prev = GPoint(px, py);
    }
  }
}

// unlock_p: 0 = fully locked, ANIMATION_NORMALIZED_MAX = fully unlocked
static void draw_icon_lock(GContext *ctx, GRect r, int32_t unlock_p, int x_offset) {
  int cx = r.origin.x + r.size.w / 2 + x_offset;
  int N  = ANIMATION_NORMALIZED_MAX;

  int max_bw = r.size.h * 5 / 7;
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  int bw = MIN(MIN(r.size.w * 7 / 10, 72), max_bw);
#else
  int bw = MIN(MIN(r.size.w * 7 / 10, 72), max_bw) * 4 / 5;
#endif
  int bh     = bw * 8 / 9;
  int arm_h  = bw * 16 / 100;
  int arc_r  = bw * 22 / 100;
  int arm_sw = bw * 19 / 100;
  arm_sw = MAX(arm_sw, 4);

  int max_lift = MAX(arc_r - arm_h + 8, 4);
  int lift     = (int)((int64_t)max_lift * unlock_p / N);

  int total_h = arc_r + arm_h + lift + bh;
  int by = r.origin.y + (r.size.h - total_h) / 2 + arc_r + arm_h + lift;
  int bx = cx - bw / 2;

  int arc_cx  = cx;
  int arc_cy  = by - arm_h - lift;
  int ol      = 4;
  int left_bot  = by + ol;
  int right_bot = (by + ol) + (int)((int64_t)(arc_cy - (by + ol)) * unlock_p / N);
  int unlock_ol = (int)((int64_t)ol * unlock_p / N);
  GRect arc_rect = GRect(arc_cx - arc_r, arc_cy - arc_r, arc_r * 2, arc_r * 2);

  icon_fill(ctx);
  graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 5, GCornersAll);
  icon_stroke(ctx);
  graphics_draw_round_rect(ctx, GRect(bx, by, bw, bh), 5);

  graphics_context_set_fill_color(ctx, COLOR_DARK);
  graphics_fill_rect(ctx,
    GRect(arc_cx - arc_r - arm_sw/2 - ol, arc_cy, arm_sw + ol*2, left_bot - arc_cy), 0, GCornerNone);
  graphics_fill_rect(ctx,
    GRect(arc_cx + arc_r - arm_sw/2 - ol, arc_cy, arm_sw + ol*2, right_bot - arc_cy + unlock_ol), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, arm_sw + ol * 2);
  graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(270), DEG_TO_TRIGANGLE(360));
  graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(90));

  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_fill_rect(ctx,
    GRect(arc_cx - arc_r - arm_sw/2, arc_cy, arm_sw, left_bot - arc_cy), 0, GCornerNone);
  graphics_fill_rect(ctx,
    GRect(arc_cx + arc_r - arm_sw/2, arc_cy, arm_sw, right_bot - arc_cy), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, arm_sw);
  graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(270), DEG_TO_TRIGANGLE(360));
  graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(90));

  int kl = bw * 9 / 100;
  int ky = by + bh * 52 / 100;
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, MAX(arm_sw * 2 / 5, 3));
  if (unlock_p < N / 2) {
    graphics_draw_line(ctx, GPoint(cx, ky - kl), GPoint(cx, ky + kl));
  } else {
    graphics_draw_line(ctx, GPoint(cx - kl, ky), GPoint(cx + kl, ky));
  }
}


static void draw_polyline(GContext *ctx, GPoint *pts, int n) {
  for (int i = 0; i < n - 1; i++) graphics_draw_line(ctx, pts[i], pts[i+1]);
}

// Draw two mountain peaks with chamfered tips and white fill.
// Slope run is derived from peak height (not screen width) so the angle
// is identical on every platform  (rise/run = 15/13 ≈ 49°).
static void draw_mountains(GContext *ctx, GRect bounds) {
  int w = bounds.size.w;
  int h = bounds.size.h;
  int mh1 = h * 24 / 100;
  int mh2 = h * 40 / 100;
  int px1 = w * 29 / 100;
  int px2 = w * 70 / 100;
  // Cap slopes so they stay within canvas bounds — prevents outline clipping at edges
  int run1 = MIN(mh1 * 15 / 13, px1 - 2);
  int run2 = MIN(mh2 * 15 / 13, w - px2 - 2);

  graphics_context_set_fill_color(ctx, COLOR_FG);

  {
    GPoint pts[] = {
      {(int16_t)(-w),           (int16_t)(h+4)},
      {(int16_t)(px1 - run1),   (int16_t)(h+4)},
      {(int16_t)(px1 - 4),      (int16_t)(h-mh1+3)},
      {(int16_t)(px1),          (int16_t)(h-mh1)},
      {(int16_t)(px1 + 4),      (int16_t)(h-mh1+3)},
      {(int16_t)(px1 + run1),   (int16_t)(h+4)},
    };
    GPathInfo info = { .num_points = 6, .points = pts };
    GPath *path = gpath_create(&info);
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  }
  {
    GPoint pts[] = {
      {(int16_t)(px2 - run2),   (int16_t)(h+4)},
      {(int16_t)(px2 - 4),      (int16_t)(h-mh2+3)},
      {(int16_t)(px2),          (int16_t)(h-mh2)},
      {(int16_t)(px2 + 4),      (int16_t)(h-mh2+3)},
      {(int16_t)(px2 + run2),   (int16_t)(h+4)},
      {(int16_t)(w*2),          (int16_t)(h+4)},
    };
    GPathInfo info = { .num_points = 6, .points = pts };
    GPath *path = gpath_create(&info);
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  }

  // Draw only the slope outlines (capped within canvas, no edge artifacts)
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 4);
  graphics_draw_line(ctx, GPoint(px1-run1, h+4), GPoint(px1-4,  h-mh1+3));
  graphics_draw_line(ctx, GPoint(px1-4, h-mh1+3), GPoint(px1,   h-mh1));
  graphics_draw_line(ctx, GPoint(px1,   h-mh1),   GPoint(px1+4, h-mh1+3));
  graphics_draw_line(ctx, GPoint(px1+4, h-mh1+3), GPoint(px1+run1, h+4));
  graphics_draw_line(ctx, GPoint(px2-run2, h+4), GPoint(px2-4,  h-mh2+3));
  graphics_draw_line(ctx, GPoint(px2-4, h-mh2+3), GPoint(px2,   h-mh2));
  graphics_draw_line(ctx, GPoint(px2,   h-mh2),   GPoint(px2+4, h-mh2+3));
  graphics_draw_line(ctx, GPoint(px2+4, h-mh2+3), GPoint(px2+run2, h+4));
}

static GPoint globe_pt(int gx, int gy, int gr, int sx, int sy, int32_t ca, int32_t sa) {
  int rx = (sx - 152) * gr / 149;
  int ry = (sy - 152) * gr / 149;
  if (sa == 0) return GPoint(gx + rx, gy + ry);
  int rotx = (int)((int64_t)rx * ca / TRIG_MAX_RATIO - (int64_t)ry * sa / TRIG_MAX_RATIO);
  int roty = (int)((int64_t)rx * sa / TRIG_MAX_RATIO + (int64_t)ry * ca / TRIG_MAX_RATIO);
  return GPoint(gx + rotx, gy + roty);
}

// Globe with lines from Globe.svg. Center shifted right.
static void draw_globe(GContext *ctx, GRect bounds) {
  int w = bounds.size.w;
  int h = bounds.size.h;

  // Globe radius and center: shifted right and down to match Figma layout
  int gr = w * 52 / 100;
  int gx = w * 75 / 100;
  int gy = h + gr / 4;  // center below screen so upper arc is visible

  // Globe fill + outline
  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_fill_circle(ctx, GPoint(gx, gy), gr);
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 4);
  graphics_draw_circle(ctx, GPoint(gx, gy), gr);

  // Globe lines from Globe.svg (303×303, center 152,152, radius 149)
  // Screen point: gx + (sx-152)*gr/149, gy + (sy-152)*gr/149
  int32_t g_ca = cos_lookup(s_globe_rot);
  int32_t g_sa = sin_lookup(s_globe_rot);
  #define GP(sx,sy) globe_pt(gx, gy, gr, sx, sy, g_ca, g_sa)
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 4);
  { GPoint p[] = { GP(28,68), GP(62,97), GP(65,126), GP(90,150), GP(93,182), GP(27,228) };
    draw_polyline(ctx, p, 6); }
  { GPoint p[] = { GP(157,3), GP(156,36), GP(126,60), GP(125,90), GP(202,160), GP(266,116), GP(264,84), GP(279,75) };
    draw_polyline(ctx, p, 8); }
  { GPoint p[] = { GP(300,148), GP(240,193), GP(234,275) };
    draw_polyline(ctx, p, 3); }
  { GPoint p[] = { GP(121,8), GP(121,32), GP(88,57), GP(60,37) };
    draw_polyline(ctx, p, 4); }
  #undef GP

}

// Road strip: white fill with black top edge at screen bottom (round only).
#ifdef PBL_ROUND
static void draw_road_strip(GContext *ctx, GRect bounds) {
  int strip_h = 22;
  int y = bounds.size.h - strip_h;
  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_fill_rect(ctx, GRect(0, y, bounds.size.w, strip_h), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 4);
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

// Helper: draw value + label below it. Same BITHAM_42_BOLD font as climate ON/OFF.
static void draw_big_stat(GContext *ctx, GRect bounds,
                          const char *number, const char *label, bool large,
                          int x_extra, int y_extra) {
  int y = CONTENT_Y + y_extra;
  int x = INSET_X + x_extra;
  int w = bounds.size.w - INSET_X * 2 - x_extra;
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  GFont num_font;
  int num_h, lbl_y;
  if (large) {
    num_font = fonts_get_system_font(FONT_KEY_LECO_60_NUMBERS_AM_PM);
    num_h = 68; lbl_y = y + 60;
  } else {
    num_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    num_h = 52; lbl_y = y + 44;
  }
#else
  GFont num_font = fonts_get_system_font(large ? FONT_KEY_LECO_42_NUMBERS : FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
  int num_h  = large ? 52 : 36;
  int lbl_y  = y + (large ? 44 : 28);
#endif
  graphics_context_set_text_color(ctx, COLOR_TEXT);
  draw_text(ctx, number, num_font,
            GRect(x, y, w, num_h),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);
  draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(x, lbl_y, w, 70),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, 0);
}


// ── Car animation helpers ─────────────────────────────────────────────────────

static bool is_car_page(int page) {
  return page >= PAGE_CHARGE_TIME && page <= PAGE_ODO;
}

// Per-page car target: position, size, rotation.
// Pages 2-5 shrink progressively (64%→60%→55%→globe-based).
static CarState car_target_for_page(int page, GRect bounds) {
  int w = bounds.size.w, h = bounds.size.h;
  CarState t = {0, 0, 0, 0};

  switch (page) {
    case PAGE_CHARGE_TIME: {
      int cw = w * 110 / 100;
      int ch = cw * 72 / 161;
      t.w = cw;
      t.x = w - cw / 2;  // center of car at right edge — half off screen
#ifdef PBL_ROUND
      t.y = h - 22 - ch;
#else
      t.y = h - ch - 2;
#endif
      break;
    }
    case PAGE_CHARGE_PCT: {
#if defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_DIORITE)
      int cw = w * 72 / 100;
#else
      int cw = w * 60 / 100;
#endif
      int ch = cw * 72 / 161;
      t.w = cw;
      t.x = w / 2 - cw / 2;
#ifdef PBL_ROUND
      t.y = h - 22 - ch;
#else
      t.y = h - ch - 2;
#endif
      break;
    }
    case PAGE_RANGE: {
#if defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_DIORITE)
      int cw = w * 46 / 100;
#else
      int cw = w * 42 / 100;
#endif
      int ch = cw * 72 / 161;
      t.w = cw;
      t.x = w / 2 - cw / 2;
#ifdef PBL_ROUND
      t.y = h - 22 - ch;
#else
      t.y = h - ch - 2;
#endif
      break;
    }
    case PAGE_ODO: {
#if defined(PBL_PLATFORM_EMERY)
      int cw = w * 26 / 100;
#elif defined(PBL_PLATFORM_CHALK)
      int cw = w * 26 / 100;
#else
      int cw = w * 36 / 100;
#endif
      int ch  = cw * 72 / 161;
      int gr  = w * 52 / 100;
      int gx  = w * 75 / 100;
      int gy  = h + gr / 4;
      int32_t a25 = TRIG_MAX_ANGLE * 25 / 360;
      int sx  = gx - (int32_t)gr * sin_lookup(a25) / TRIG_MAX_RATIO;
      int sy  = gy - (int32_t)gr * cos_lookup(a25) / TRIG_MAX_RATIO;
      int bh  = ch - MAX(ch / 4, 10);
      t.w   = cw;
      t.x   = sx - cw / 2 - 6;
#if defined(PBL_PLATFORM_EMERY)
      t.y   = sy - bh + 2;
#else
      t.y   = sy - bh - 6;
#endif
      t.rot = -(int32_t)(TRIG_MAX_ANGLE * 25 / 360);
      break;
    }
    default: {
      // Off-screen below for non-car pages
      int cw = w * 62 / 100;
      int ch = cw * 72 / 161;
      t.w = cw;
      t.x = w / 2 - cw / 2;
      t.y = h + ch + 10;
      break;
    }
  }
  return t;
}

#define LERP_P(a, b, p) ((a) + (int32_t)((int64_t)((b)-(a)) * (p) / ANIMATION_NORMALIZED_MAX))

static void car_anim_update(Animation *anim, const AnimationProgress progress) {
  s_transition_p = progress;
  s_car_cur.x   = LERP_P(s_car_phase[0].x, s_car_phase[1].x, progress);
  s_car_cur.y   = LERP_P(s_car_phase[0].y, s_car_phase[1].y, progress);
  s_car_cur.w   = LERP_P(s_car_phase[0].w, s_car_phase[1].w, progress);
  s_car_cur.rot = LERP_P(s_car_phase[0].rot, s_car_phase[1].rot, progress);
  if (s_ground_morph) {
    // Delay ground morph 250ms into the 280ms animation before it starts morphing
    int32_t gm_delay = ANIMATION_NORMALIZED_MAX * 250 / 280;
    s_ground_morph_p = progress <= gm_delay ? 0
      : (int32_t)((int64_t)(progress - gm_delay) * ANIMATION_NORMALIZED_MAX
                  / (ANIMATION_NORMALIZED_MAX - gm_delay));
    // Keep car grounded (flat bottom) until ground morph is 75% done, then blend
    // smoothly to the ODO globe position in the final quarter of the morph.
    GRect bnd = layer_get_bounds(window_get_root_layer(s_window));
    int ch = (int)s_car_cur.w * 72 / 161;
    int32_t N = ANIMATION_NORMALIZED_MAX;
    int flat_y = bnd.size.h - ch - 2;
    if (s_ground_morph_p <= N * 3 / 4) {
      if ((int)s_car_cur.y < flat_y) s_car_cur.y = (int16_t)flat_y;
      s_car_cur.rot = 0;
    } else {
      int32_t cw_3q = LERP_P(s_car_phase[0].w, s_car_phase[1].w, N * 3 / 4);
      int ch_3q  = (int)cw_3q * 72 / 161;
      int y_at_3q = bnd.size.h - ch_3q - 2;
      int32_t sub_p = MIN((int32_t)(s_ground_morph_p - N * 3 / 4) * 4, N);
      s_car_cur.y   = (int16_t)LERP_P(y_at_3q, s_car_phase[1].y, sub_p);
      s_car_cur.rot = (int32_t)LERP_P(0, s_car_phase[1].rot, sub_p);
    }
  }
  if (s_anim_from_page == PAGE_CHARGE_TIME) {
    s_cable_anim_p = progress;
  }
  layer_mark_dirty(s_car_layer);
}

static const AnimationImplementation s_car_anim_impl = { .update = car_anim_update };

static void car_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Hint indicator (semi-circle) on right edge for non-car pages; slides in/out
  bool on_hint  = (s_page == PAGE_CLIMATE || s_page == PAGE_LOCK || s_page == PAGE_LOCATION);
  bool from_hint = s_animating && (s_anim_from_page == PAGE_CLIMATE ||
                                   s_anim_from_page == PAGE_LOCK    ||
                                   s_anim_from_page == PAGE_LOCATION);
  if (on_hint || from_hint) {
    int32_t hint_p;
    if (!s_animating || (on_hint && from_hint)) {
      hint_p = ANIMATION_NORMALIZED_MAX;
    } else if (on_hint) {
      hint_p = s_transition_p;
    } else {
      hint_p = ANIMATION_NORMALIZED_MAX - s_transition_p;
    }
    int hint_x = (int)LERP_P(bounds.size.w + 26, bounds.size.w + 10, hint_p);
    graphics_context_set_fill_color(ctx, COLOR_DARK);
    graphics_fill_circle(ctx, GPoint(hint_x, bounds.size.h / 2), 16);
  }

  // Globe lives on the car_layer (full-screen, non-sliding) to avoid canvas edge clipping
  bool on_odo   = (s_page == PAGE_ODO);
  bool from_odo = s_animating && (s_anim_from_page == PAGE_ODO);
  if ((on_odo || from_odo) && !s_ground_morph) {
    draw_globe(ctx, bounds);
  }

  if (s_car_cur.w <= 0) return;
  if ((int)s_car_cur.y >= bounds.size.h) return;  // parked off-screen below
  int cw = (int)s_car_cur.w;
  int ch = cw * 72 / 161;

  if (s_ground_morph) {
    int w = bounds.size.w, h = bounds.size.h;
    int gr   = w * 52 / 100;
    int gx   = w * 75 / 100;
    int gy   = h + gr - 22;  // top of globe == ground line; morph top stays there always
    int32_t flat_r  = (int32_t)h * 8;
    int32_t flat_cx = w / 2;
    int32_t flat_cy = (int32_t)(h - 22) + flat_r;
    int cur_cx = (int)LERP_P(flat_cx, (int32_t)gx, s_ground_morph_p);
    int cur_cy = (int)LERP_P(flat_cy, (int32_t)gy, s_ground_morph_p);
    int cur_r  = (int)LERP_P(flat_r,  (int32_t)gr,  s_ground_morph_p);
    graphics_context_set_fill_color(ctx, COLOR_FG);
    graphics_fill_rect(ctx, GRect(0, h - 22, w, 22), 0, GCornerNone);
    // Explicit flat border so it's visible at the start of the morph
    graphics_context_set_stroke_color(ctx, COLOR_DARK);
    graphics_context_set_stroke_width(ctx, 4);
    graphics_draw_line(ctx, GPoint(0, h - 24), GPoint(w, h - 24));
    // Morphing circle fill covers the flat line as the globe grows in
    graphics_context_set_fill_color(ctx, COLOR_FG);
    graphics_fill_circle(ctx, GPoint(cur_cx, cur_cy), (uint16_t)cur_r);
    graphics_draw_circle(ctx, GPoint(cur_cx, cur_cy), (uint16_t)cur_r);
  }
#ifdef PBL_ROUND
  else {
    int car_bottom = (int)s_car_cur.y + ch;
    if (car_bottom >= bounds.size.h - 30 && (int)s_car_cur.y < bounds.size.h) {
      draw_road_strip(ctx, bounds);
    }
  }
#endif

  bool show_cable = s_state.is_charging &&
    (s_page == PAGE_CHARGE_TIME ||
     (s_animating && s_anim_from_page == PAGE_CHARGE_TIME));

  draw_icon_car(ctx,
    GRect((int16_t)s_car_cur.x, (int16_t)s_car_cur.y,
          (int16_t)cw, (int16_t)ch),
    s_car_cur.rot, 0);

  if (show_cable) {
    draw_charging_cable(ctx,
      (int)s_car_cur.x, (int)s_car_cur.y, cw, ch,
      bounds.size.w, bounds.size.h, s_cable_anim_p);
  }
}

// ── Page renderers ────────────────────────────────────────────────────────────

static void draw_page_climate(GContext *ctx, GRect bounds) {
  int y = CONTENT_Y + 4;
  int w = bounds.size.w - INSET_X * 2;

  graphics_context_set_text_color(ctx, COLOR_TEXT);
  draw_text(ctx, s_state.climate_on ? "ON" : "OFF",
            fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
            GRect(INSET_X, y, w, 52),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);
  draw_text(ctx, "climate",
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, y + 44, w, 36),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);

  int div_y = bounds.size.h - 52;
  graphics_context_set_stroke_color(ctx, COLOR_TEXT);
  graphics_context_set_stroke_width(ctx, 2);
#ifdef PBL_ROUND
  graphics_draw_line(ctx, GPoint(INSET_X + 8, div_y), GPoint(bounds.size.w - INSET_X - 8, div_y));
#else
  graphics_draw_line(ctx, GPoint(INSET_X, div_y), GPoint(bounds.size.w - INSET_X, div_y));
#endif

  char temp_buf[24];
  if (s_state.use_metric) {
    int c = (s_state.outside_temp - 32) * 5 / 9;
    snprintf(temp_buf, sizeof(temp_buf), "%d\xC2\xB0""C outside", c);
  } else {
    snprintf(temp_buf, sizeof(temp_buf), "%d\xC2\xB0""F outside", s_state.outside_temp);
  }
#ifdef PBL_ROUND
  draw_text(ctx, temp_buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, div_y + 8, w, 36),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, 8);
#else
  draw_text(ctx, temp_buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, div_y + 8, w, 36),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);
#endif
}

static void draw_page_lock(GContext *ctx, GRect bounds) {
  int w = bounds.size.w - INSET_X * 2;
  int lbl_h = 36;
  int gap = 6;
  int icon_y = CONTENT_Y + (bounds.size.h - CONTENT_Y) / 12 + 6;
  int icon_h = bounds.size.h - icon_y - lbl_h - gap - 8;

  int lock_x = 0;
  if (s_lock_shaking) {
    int32_t angle = (int32_t)((int64_t)TRIG_MAX_ANGLE * 3 * s_lock_shake_p / ANIMATION_NORMALIZED_MAX);
    lock_x = 6 * sin_lookup(angle) / TRIG_MAX_RATIO;
  }
  int32_t unlock_p;
  if (s_lock_toggling) {
    unlock_p = s_lock_toggle_from ? s_lock_toggle_p : (ANIMATION_NORMALIZED_MAX - s_lock_toggle_p);
  } else {
    unlock_p = s_state.locked ? 0 : ANIMATION_NORMALIZED_MAX;
  }
  draw_icon_lock(ctx, GRect(INSET_X, icon_y, w, icon_h), unlock_p, lock_x);

  graphics_context_set_text_color(ctx, COLOR_TEXT);
  draw_text(ctx, s_state.locked ? "locked" : "unlocked",
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, icon_y + icon_h + gap, w, lbl_h),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, 0);
}

static void draw_page_charge_time(GContext *ctx, GRect bounds) {
  if (!s_state.is_charging) {
    int y = CONTENT_Y;
    int w = bounds.size.w - INSET_X * 2;
    graphics_context_set_text_color(ctx, COLOR_TEXT);
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
    draw_big_stat(ctx, bounds, num, "minutes\nuntil full", true, 0, 0);
  }

}

// Draws a % glyph sized to sit beside LECO_42_NUMBERS digits.
// LECO_42_NUMBERS only contains digit glyphs (no %) so we draw it manually.
// Blocky % glyph — two filled squares + thick diagonal — matches LECO's geometric weight.
static void draw_percent_glyph(GContext *ctx, GPoint origin, int size) {
  int sq = MAX(size * 3 / 10, 4);  // square size (~8px at size=28)
  int sw = MAX(size / 6,      3);  // diagonal stroke width (~5px at size=28)
  graphics_context_set_fill_color(ctx, COLOR_TEXT);
  graphics_fill_rect(ctx, GRect(origin.x,                  origin.y,                  sq, sq), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + size - sq,      origin.y + size - sq,      sq, sq), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_TEXT);
  graphics_context_set_stroke_width(ctx, sw);
  graphics_draw_line(ctx,
    GPoint(origin.x + size - 1, origin.y + 1),
    GPoint(origin.x + 1,        origin.y + size - 1));
}

static void draw_page_charge_pct(GContext *ctx, GRect bounds) {
  char num[8];
  snprintf(num, sizeof(num), "%d", s_state.charge_pct);
  draw_big_stat(ctx, bounds, num, "charged", true, 0, 0);
  int digits = s_state.charge_pct >= 100 ? 3 : (s_state.charge_pct >= 10 ? 2 : 1);
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  int digit_w  = 40;
  int glyph_sz = 24;
  int glyph_y  = CONTENT_Y + 17;
#else
  int digit_w  = 30;
  int glyph_sz = 18;
  int glyph_y  = CONTENT_Y + 13;
#endif
  draw_percent_glyph(ctx,
    GPoint(INSET_X + digits * digit_w - 2, glyph_y),
    glyph_sz);
}

static void draw_page_range(GContext *ctx, GRect bounds) {
  char num[16];
  int val = s_state.use_metric
    ? s_state.range_km
    : (int)(s_state.range_km * 621 / 1000);
  snprintf(num, sizeof(num), "%d", val);
  draw_big_stat(ctx, bounds, num,
    s_state.use_metric ? "kilometer\nrange" : "mile range", true, 0, 0);
  draw_mountains(ctx, bounds);
}

static void draw_page_odo(GContext *ctx, GRect bounds) {
  char num[16];
  int val = s_state.use_metric
    ? s_state.odo_km
    : (int)(s_state.odo_km * 621 / 1000);
  snprintf(num, sizeof(num), "%07d", val);
#ifdef PBL_ROUND
  draw_big_stat(ctx, bounds, num,
    s_state.use_metric ? "kilometers\ndriven" : "miles driven", false, 0, 4);
#else
  draw_big_stat(ctx, bounds, num,
    s_state.use_metric ? "kilometers\ndriven" : "miles driven", false, 0, 4);
#endif

}

static void draw_page_location(GContext *ctx, GRect bounds) {
  int y = CONTENT_Y + 4;
  int w = bounds.size.w - INSET_X * 2;
  graphics_context_set_text_color(ctx, COLOR_TEXT);
  GRect addr_box = GRect(INSET_X, y, w, 80);
  GFont addr_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GSize addr_sz = graphics_text_layout_get_content_size(
    s_state.location, addr_font, addr_box, GTextOverflowModeWordWrap, GTextAlignmentLeft);
  if (addr_sz.h > addr_box.size.h) {
    addr_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    addr_sz = graphics_text_layout_get_content_size(
      s_state.location, addr_font, addr_box, GTextOverflowModeWordWrap, GTextAlignmentLeft);
    if (addr_sz.h > addr_box.size.h) {
      addr_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    }
  }
  draw_text(ctx, s_state.location, addr_font, addr_box,
            GTextOverflowModeWordWrap, GTextAlignmentLeft, 8);
  draw_text(ctx, "current location",
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, y + 60, w, 32),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);

  int div_y = bounds.size.h - 52;
  graphics_context_set_stroke_color(ctx, COLOR_TEXT);
  graphics_context_set_stroke_width(ctx, 2);
#ifdef PBL_ROUND
  graphics_draw_line(ctx, GPoint(INSET_X + 8, div_y), GPoint(bounds.size.w - INSET_X - 8, div_y));
#else
  graphics_draw_line(ctx, GPoint(INSET_X, div_y), GPoint(bounds.size.w - INSET_X, div_y));
#endif

  char dist_buf[32];
  if (s_state.distance_m < 0) {
    snprintf(dist_buf, sizeof(dist_buf), "distance unknown");
  } else if (s_state.use_metric) {
    if (s_state.distance_m < 1000) {
      snprintf(dist_buf, sizeof(dist_buf), "%d m away", s_state.distance_m);
    } else {
      snprintf(dist_buf, sizeof(dist_buf), "%d.%d km away",
               s_state.distance_m / 1000, (s_state.distance_m % 1000) / 100);
    }
  } else {
    int ft = (int)((int64_t)s_state.distance_m * 3281 / 1000);
    if (ft < 5280) {
      snprintf(dist_buf, sizeof(dist_buf), "%d ft away", ft);
    } else {
      int mi_whole = ft / 5280;
      int mi_frac  = (ft % 5280) * 10 / 5280;
      snprintf(dist_buf, sizeof(dist_buf), "%d.%d mi away", mi_whole, mi_frac);
    }
  }
#ifdef PBL_ROUND
  draw_text(ctx, dist_buf,
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, div_y + 8, w, 36),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, 8);
#else
  draw_text(ctx, dist_buf,
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(INSET_X, div_y + 8, w, 36),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);
#endif
}

// ── Canvas update ─────────────────────────────────────────────────────────────

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int   page   = (layer == s_anim_layer) ? s_anim_from_page : s_page;

  graphics_context_set_fill_color(ctx, COLOR_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_state.error && layer != s_anim_layer) {
    graphics_context_set_text_color(ctx, COLOR_TEXT);
    draw_text(ctx, "Error\nCheck phone",
              fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
              GRect(INSET_X, bounds.size.h / 2 - 28, bounds.size.w - INSET_X * 2, 56),
              GTextOverflowModeWordWrap, GTextAlignmentCenter, 5);
    return;
  }

  switch (page) {
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
  if ((t = dict_find(iter, KEY_SETTING_UNITS))) {
    s_state.use_metric = (bool)t->value->int32;
    persist_write_bool(PERSIST_KEY_USE_METRIC, s_state.use_metric);
  }
  if ((t = dict_find(iter, KEY_STATE_LOCATION)))
    snprintf(s_state.location, sizeof(s_state.location), "%s", t->value->cstring);
  if ((t = dict_find(iter, KEY_STATE_DISTANCE_M))) s_state.distance_m = t->value->int32;
  if ((t = dict_find(iter, KEY_SETTING_LIGHT_TEXT))) {
#ifdef PBL_COLOR
    bool lt = (bool)t->value->int32;
    persist_write_bool(PERSIST_KEY_LIGHT_TEXT, lt);
    s_color_text = lt ? GColorBlack : GColorWhite;
    if (s_status_bar) status_bar_layer_set_colors(s_status_bar, COLOR_BG, COLOR_TEXT);
    if (s_page_label) text_layer_set_text_color(s_page_label, COLOR_TEXT);
#endif
  }

  layer_mark_dirty(s_canvas);
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ── Action menu ───────────────────────────────────────────────────────────────

static void lock_toggle_update(Animation *anim, const AnimationProgress progress) {
  s_lock_toggle_p = progress;
  layer_mark_dirty(s_canvas);
}
static void lock_toggle_stopped(Animation *anim, bool finished, void *context) {
  s_lock_toggling = false;
  s_lock_toggle_p = 0;
  layer_mark_dirty(s_canvas);
}
static const AnimationImplementation s_lock_toggle_impl = { .update = lock_toggle_update };

static void action_performed(ActionMenu *menu, const ActionMenuItem *item, void *context) {
  vibes_short_pulse();
  int act = (int)(intptr_t)action_menu_item_get_action_data(item);
  switch (act) {
    case 0: s_state.climate_on = !s_state.climate_on; send_cmd(CMD_TOGGLE_CLIMATE); break;
    case 1:
      if (!s_lock_toggling) {
        s_lock_toggle_from = s_state.locked;
        s_lock_toggling    = true;
        s_lock_toggle_p    = 0;
        Animation *ltanim  = animation_create();
        animation_set_implementation(ltanim, &s_lock_toggle_impl);
        animation_set_duration(ltanim, 400);
        animation_set_curve(ltanim, AnimationCurveEaseInOut);
        animation_set_handlers(ltanim, (AnimationHandlers){ .stopped = lock_toggle_stopped }, NULL);
        animation_schedule(ltanim);
      }
      s_state.locked = !s_state.locked;
      send_cmd(CMD_TOGGLE_LOCK);
      break;
    case 2: send_cmd(CMD_HONK); break;
    case 3: send_cmd(CMD_NAVIGATE); break;
  }
  layer_mark_dirty(s_canvas);
}

static void action_menu_did_close(ActionMenu *menu, const ActionMenuItem *item, void *context) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
}

static void open_action_menu(void) {
  ActionMenuLevel *root;
  switch (s_page) {
    case PAGE_CLIMATE:
      root = action_menu_level_create(1);
      action_menu_level_add_action(root,
        s_state.climate_on ? "Turn Off" : "Turn On",
        action_performed, (void*)(intptr_t)0);
      break;
    case PAGE_LOCK:
      root = action_menu_level_create(1);
      action_menu_level_add_action(root,
        s_state.locked ? "Unlock" : "Lock",
        action_performed, (void*)(intptr_t)1);
      break;
    default: // PAGE_LOCATION
      root = action_menu_level_create(2);
      action_menu_level_add_action(root, "Honk + Flash", action_performed, (void*)(intptr_t)2);
      action_menu_level_add_action(root, "Navigate",     action_performed, (void*)(intptr_t)3);
      break;
  }
  ActionMenuConfig config = {
    .root_level = root,
    .did_close  = action_menu_did_close,
    .colors     = {
      .background = PBL_IF_COLOR_ELSE(COLOR_BG, GColorBlack),
      .foreground = PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite),
    },
    .align = ActionMenuAlignTop,
  };
  action_menu_open(&config);
}

// ── Globe spin ────────────────────────────────────────────────────────────────

static void globe_spin_update(Animation *anim, const AnimationProgress progress) {
  s_globe_rot = -(int32_t)((int64_t)TRIG_MAX_ANGLE * progress / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(s_car_layer);
}
static void globe_spin_stopped(Animation *anim, bool finished, void *context) {
  s_globe_spinning = false;
  s_globe_rot = 0;
  layer_mark_dirty(s_car_layer);
}
static const AnimationImplementation s_globe_spin_impl = { .update = globe_spin_update };

static void lock_shake_update(Animation *anim, const AnimationProgress progress) {
  s_lock_shake_p = progress;
  layer_mark_dirty(s_canvas);
}
static void lock_shake_stopped(Animation *anim, bool finished, void *context) {
  s_lock_shaking = false;
  s_lock_shake_p = 0;
  layer_mark_dirty(s_canvas);
}
static const AnimationImplementation s_lock_shake_impl = { .update = lock_shake_update };

static void globe_intro_update(Animation *anim, const AnimationProgress progress) {
  s_globe_rot = -(int32_t)((int64_t)(TRIG_MAX_ANGLE / 12) * progress / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(s_car_layer);
}
static const AnimationImplementation s_globe_intro_impl = { .update = globe_intro_update };

static void globe_intro_rev_update(Animation *anim, const AnimationProgress progress) {
  s_globe_rot = (int32_t)((int64_t)(TRIG_MAX_ANGLE / 12) * progress / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(s_car_layer);
}
static const AnimationImplementation s_globe_intro_rev_impl = { .update = globe_intro_rev_update };

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  if (s_animating) return;
  if (s_page == PAGE_ODO && !s_globe_spinning) {
    s_globe_spinning = true;
    Animation *anim = animation_create();
    animation_set_implementation(anim, &s_globe_spin_impl);
    animation_set_duration(anim, 1500);
    animation_set_curve(anim, AnimationCurveEaseInOut);
    animation_set_handlers(anim, (AnimationHandlers){ .stopped = globe_spin_stopped }, NULL);
    animation_schedule(anim);
  } else if (s_page == PAGE_LOCK && !s_lock_shaking) {
    s_lock_shaking = true;
    Animation *anim = animation_create();
    animation_set_implementation(anim, &s_lock_shake_impl);
    animation_set_duration(anim, 500);
    animation_set_curve(anim, AnimationCurveEaseInOut);
    animation_set_handlers(anim, (AnimationHandlers){ .stopped = lock_shake_stopped }, NULL);
    animation_schedule(anim);
  }
}

// ── Page transition animation ─────────────────────────────────────────────────


static void update_page_indicator(void) {
  snprintf(s_page_buf, sizeof(s_page_buf), "%d/%d", s_page + 1, PAGE_COUNT);
  text_layer_set_text(s_page_label, s_page_buf);
}

static void transition_stopped(Animation *anim, bool finished, void *context) {
  s_animating = false;
  s_transition_p = 0;
  s_ground_morph = false;
  s_cable_anim_p = 0;
  if (s_anim_layer) {
    layer_remove_from_parent(s_anim_layer);
    layer_destroy(s_anim_layer);
    s_anim_layer = NULL;
  }
  GRect r = layer_get_bounds(window_get_root_layer(s_window));
  layer_set_frame(s_canvas, r);
  s_car_cur = car_target_for_page(s_page, r);
  layer_mark_dirty(s_car_layer);
}

static void navigate(int dir) {
  if (s_animating) return;
  Layer *root   = window_get_root_layer(s_window);
  GRect  bounds = layer_get_bounds(root);
  int    w = bounds.size.w, h = bounds.size.h;

  s_animating      = true;
  s_anim_dir       = dir;
  s_anim_from_page = s_page;
  s_page           = (s_page + dir + PAGE_COUNT) % PAGE_COUNT;
  update_page_indicator();

  // Old page layer: starts in place, slides out in -dir direction
  s_anim_layer = layer_create(GRect(0, 0, w, h));
  layer_set_update_proc(s_anim_layer, canvas_update_proc);
  layer_insert_below_sibling(s_anim_layer, s_canvas);

  GRect canvas_start = GRect(dir * w, 0, w, h);
  GRect canvas_end   = GRect(0, 0, w, h);
  layer_set_frame(s_canvas, canvas_start);
  layer_mark_dirty(s_canvas);

  GRect anim_start = GRect(0, 0, w, h);
  GRect anim_end   = GRect(-dir * w, 0, w, h);

  PropertyAnimation *pa_in  = property_animation_create_layer_frame(s_canvas, &canvas_start, &canvas_end);
  PropertyAnimation *pa_out = property_animation_create_layer_frame(s_anim_layer, &anim_start, &anim_end);
  animation_set_duration((Animation*)pa_in,  380);
  animation_set_curve((Animation*)pa_in,  AnimationCurveEaseInOut);
  animation_set_duration((Animation*)pa_out, 380);
  animation_set_curve((Animation*)pa_out, AnimationCurveEaseInOut);

  bool from_car = is_car_page(s_anim_from_page);
  bool to_car   = is_car_page(s_page);
  CarState target = car_target_for_page(s_page, bounds);
  s_ground_morph   = false;
  s_ground_morph_p = 0;
  bool odo_to_loc = (s_anim_from_page == PAGE_ODO && s_page == PAGE_LOCATION);
  bool loc_to_odo = (s_anim_from_page == PAGE_LOCATION && s_page == PAGE_ODO);
  if (odo_to_loc) {
    // Car slides left with the globe (stays attached to outgoing canvas)
    s_car_phase[0] = s_car_cur;
    target = s_car_cur;
    target.x -= w;
  } else if (loc_to_odo) {
    // Keep car hidden during slide; transition_stopped snaps it to the globe
    s_car_phase[0] = (CarState){ target.x, (int32_t)(h + 100), target.w, target.rot };
    s_car_phase[1] = s_car_phase[0];
  } else if (!from_car && to_car) {
    // Car drives in from the navigation edge (e.g. lock → charge_time)
    int32_t enter_x = dir > 0
      ? -(int32_t)(target.w + 4)
      : (int32_t)(w + target.w + 4);
    s_car_phase[0] = (CarState){ enter_x, target.y, target.w, target.rot };
  } else {
    s_car_phase[0] = s_car_cur;
  }
  if (!loc_to_odo) s_car_phase[1] = target;

  Animation *car_anim = animation_create();
  animation_set_implementation(car_anim, &s_car_anim_impl);
  animation_set_duration(car_anim, 380);
  animation_set_curve(car_anim, AnimationCurveEaseInOut);

  // Globe intro spin when arriving at odo: forward from range, reverse from location
  if (s_page == PAGE_ODO && (s_anim_from_page == PAGE_RANGE || s_anim_from_page == PAGE_LOCATION)) {
    s_globe_rot = 0;
    Animation *intro = animation_create();
    animation_set_implementation(intro,
      s_anim_from_page == PAGE_LOCATION ? &s_globe_intro_rev_impl : &s_globe_intro_impl);
    animation_set_duration(intro, 380);
    animation_set_curve(intro, AnimationCurveEaseInOut);
    animation_set_handlers(intro, (AnimationHandlers){ .stopped = globe_spin_stopped }, NULL);
    animation_schedule(intro);
  }

  Animation *spawn = animation_spawn_create((Animation*)pa_in, (Animation*)pa_out, car_anim, NULL);
  animation_set_handlers(spawn, (AnimationHandlers){ .stopped = transition_stopped }, NULL);
  animation_schedule(spawn);
}

// ── Button handlers ───────────────────────────────────────────────────────────

static void up_click(ClickRecognizerRef recognizer, void *context) {
  navigate(-1);
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  navigate(1);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  switch (s_page) {
    case PAGE_CLIMATE:
    case PAGE_LOCK:
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

  // Runtime color initialization
#ifdef PBL_COLOR
  s_color_bg   = GColorChromeYellow;
  s_color_dark = GColorBlack;
  s_color_dim  = GColorLightGray;
  s_color_fg   = GColorWhite;
  s_color_text = persist_read_bool(PERSIST_KEY_LIGHT_TEXT) ? GColorBlack : GColorWhite;
  s_state.use_metric = persist_exists(PERSIST_KEY_USE_METRIC)
    ? persist_read_bool(PERSIST_KEY_USE_METRIC) : true;
#else
  s_color_bg   = GColorBlack;
  s_color_fg   = GColorWhite;
  s_color_text = GColorWhite;
  s_color_dark = GColorBlack;
  s_color_dim  = GColorWhite;
#endif

  // Full-screen canvas (drawn behind the status bar layer)
  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);

  // Car animation layer: sits above canvas, below status bar; does not slide
  s_car_cur = car_target_for_page(s_page, bounds);
  s_car_layer = layer_create(bounds);
  layer_set_update_proc(s_car_layer, car_layer_update_proc);
  layer_add_child(root, s_car_layer);

  // Native status bar — auto-updates time, orange bg, white fg, no separator
  s_status_bar = status_bar_layer_create();
  status_bar_layer_set_colors(s_status_bar, COLOR_BG, COLOR_TEXT);
  status_bar_layer_set_separator_mode(s_status_bar, StatusBarLayerSeparatorModeNone);
  layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

  // Page indicator overlaid on the right side of the status bar row
  int lbl_w = 36;
  s_page_label = text_layer_create(
    GRect(bounds.size.w - lbl_w - 2, 0, lbl_w, STATUS_BAR_LAYER_HEIGHT));
  text_layer_set_background_color(s_page_label, GColorClear);
  text_layer_set_text_color(s_page_label, COLOR_TEXT);
  text_layer_set_font(s_page_label, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_page_label, GTextAlignmentRight);
  layer_add_child(root, text_layer_get_layer(s_page_label));

  update_page_indicator();
  window_set_click_config_provider(window, click_config_provider);
  accel_tap_service_subscribe(accel_tap_handler);
}

static void window_unload(Window *window) {
  accel_tap_service_unsubscribe();
  layer_destroy(s_canvas);
  layer_destroy(s_car_layer);
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
