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
#ifdef PBL_COLOR
  #define COLOR_BG    GColorChromeYellow
  #define COLOR_FG    GColorWhite
  #define COLOR_DARK  GColorBlack
  #define COLOR_DIM   GColorLightGray
#else
  #define COLOR_BG    GColorBlack
  #define COLOR_FG    GColorWhite
  #define COLOR_DARK  GColorBlack
  #define COLOR_DIM   GColorWhite
#endif

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
  #define CONTENT_Y 44
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
  int  distance_m;
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
  .distance_m   = -1,
};

static int               s_page = PAGE_CLIMATE;
static Window           *s_window;
static Layer            *s_canvas;
static StatusBarLayer   *s_status_bar;
static TextLayer        *s_page_label;
static char              s_page_buf[8];
static SimpleMenuLayer  *s_action_menu_layer;
static Window           *s_action_window;
static int               s_action_pending  = -1;
static bool              s_animating       = false;
static Layer            *s_anim_layer      = NULL;
static int               s_anim_from_page  = 0;
static int32_t           s_car_anim_x      = 0;
static Animation        *s_car_anim        = NULL;

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
  int wheel_r = wheel_r_override > 0 ? wheel_r_override : MAX(h / 4, 10);
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

// Cable hangs from the charge port (rear/left of car) down to ground,
// then runs off the left edge toward the charger (off-screen).
static void draw_charging_cable(GContext *ctx, int car_x, int car_y,
                                int car_w, int car_h, int bounds_w, int bounds_h) {
  int wheel_r = MAX(car_h / 4, 10);
  int body_h  = car_h - wheel_r;
  // Charge port: rear = left side of car
  int port_x  = car_x + 8 * car_w / 161;
  int port_y  = car_y + 32 * body_h / 54;
  // Ground level: bottom of wheels (also top of road strip on round screens)
#ifdef PBL_ROUND
  int ground_y = bounds_h - 22;
#else
  int ground_y = car_y + car_h;
#endif
  // Quadratic bezier: port → hangs vertically down → runs off left edge at ground
  int sx = port_x, sy = port_y;    // start at port
  int mx = port_x, my = ground_y; // control: directly below port at ground
  int ex = -4,     ey = ground_y; // end: off left edge at ground

  // Two-pass: black outline first, white fill on top (rounded ends from draw_line)
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 0) {
      graphics_context_set_stroke_color(ctx, COLOR_DARK);
      graphics_context_set_stroke_width(ctx, 8);
    } else {
      graphics_context_set_stroke_color(ctx, COLOR_FG);
      graphics_context_set_stroke_width(ctx, 4);
    }
    GPoint prev = GPoint(sx, sy);
    for (int i = 1; i <= 8; i++) {
      int t = i, mt = 8 - i;
      int px = (mt*mt*sx + 2*mt*t*mx + t*t*ex) / 64;
      int py = (mt*mt*sy + 2*mt*t*my + t*t*ey) / 64;
      graphics_draw_line(ctx, prev, GPoint(px, py));
      prev = GPoint(px, py);
    }
  }
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

  // Shackle: draw black outline layer first, then white fill on top.
  int ol = 4;  // outline thickness in px
  GRect arc_rect = GRect(arc_cx - arc_r, arc_cy - arc_r, arc_r * 2, arc_r * 2);

  // --- Black outline pass ---
  graphics_context_set_fill_color(ctx, COLOR_DARK);
  graphics_fill_rect(ctx,
    GRect(arc_cx - arc_r - arm_sw/2 - ol, arc_cy, arm_sw + ol*2, by - arc_cy + ol), 0, GCornerNone);
  graphics_fill_rect(ctx,
    GRect(arc_cx + arc_r - arm_sw/2 - ol, arc_cy, arm_sw + ol*2, by - arc_cy + ol), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, arm_sw + ol * 2);
  graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(270), DEG_TO_TRIGANGLE(360));
  graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(90));

  // --- White fill pass ---
  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_fill_rect(ctx,
    GRect(arc_cx - arc_r - arm_sw/2, arc_cy, arm_sw, by - arc_cy + ol), 0, GCornerNone);
  graphics_fill_rect(ctx,
    GRect(arc_cx + arc_r - arm_sw/2, arc_cy, arm_sw, by - arc_cy + ol), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, arm_sw);
  graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(270), DEG_TO_TRIGANGLE(360));
  graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(90));

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


static void draw_polyline(GContext *ctx, GPoint *pts, int n) {
  for (int i = 0; i < n - 1; i++) graphics_draw_line(ctx, pts[i], pts[i+1]);
}

// Draw two mountain peaks with chamfered tips (5-point polygon per peak).
static void draw_mountains(GContext *ctx, GRect bounds) {
  int w = bounds.size.w;
  int h = bounds.size.h;
  graphics_context_set_fill_color(ctx, COLOR_FG);
  graphics_context_set_stroke_color(ctx, COLOR_DARK);
  graphics_context_set_stroke_width(ctx, 3);

  {
    GPoint pts[] = {
      {-5,             h},
      {w*27/100,   h-70},
      {w*28/100,   h-72},
      {w*29/100,   h-73},
      {w*30/100,   h-72},
      {w*31/100,   h-70},
      {w*62/100,       h},
    };
    GPathInfo info = { .num_points = 7, .points = pts };
    GPath *path = gpath_create(&info);
    gpath_draw_filled(ctx, path);
    gpath_draw_outline(ctx, path);
    gpath_destroy(path);
  }
  {
    GPoint pts[] = {
      {w*33/100,       h},
      {w*68/100,   h-87},
      {w*69/100,   h-90},
      {w*70/100,   h-91},
      {w*71/100,   h-90},
      {w*72/100,   h-87},
      {w+5,            h},
    };
    GPathInfo info = { .num_points = 7, .points = pts };
    GPath *path = gpath_create(&info);
    gpath_draw_filled(ctx, path);
    gpath_draw_outline(ctx, path);
    gpath_destroy(path);
  }
}

// Globe with lines from Globe.svg. Center shifted right; car at 25° tilted position.
static void draw_globe_and_car(GContext *ctx, GRect bounds) {
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
  #define GP(sx,sy) GPoint((int16_t)(gx+((sx)-152)*gr/149), (int16_t)(gy+((sy)-152)*gr/149))
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

  // Car at 25° LEFT from top of globe, tilted -25° CCW (front/right tilts up)
  int car_w = w * 38 / 100;   // slightly larger than other pages
  int car_h = car_w * 72 / 161;
  int32_t a25 = TRIG_MAX_ANGLE * 25 / 360;
  int32_t rot = TRIG_MAX_ANGLE * 335 / 360;  // -25° CCW
  // Surface point at 25° LEFT from globe top
  int surf_x = gx - (int32_t)gr * sin_lookup(a25) / TRIG_MAX_RATIO;
  int surf_y = gy - (int32_t)gr * cos_lookup(a25) / TRIG_MAX_RATIO;
  int wheel_r = MAX(car_h / 4, 10);
  int body_h  = car_h - wheel_r;
  // Place so wheel-center row (body_h from rect top) sits AT surface point
  draw_icon_car(ctx,
    GRect(surf_x - car_w / 2, surf_y - body_h, car_w, car_h),
    rot, MAX(car_h / 6, 8));
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

// Helper: draw LECO number + label below it. large=true uses GOTHIC_28_BOLD.
static void draw_big_stat(GContext *ctx, GRect bounds,
                          const char *number, const char *label, bool large) {
  int y = CONTENT_Y;
  int w = bounds.size.w - INSET_X * 2;
  GFont lf = fonts_get_system_font(large ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD);
  int lh = large ? 80 : 68;

  graphics_context_set_text_color(ctx, COLOR_FG);
  draw_text(ctx, number, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49),
            GRect(INSET_X, y, w, 60),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);
  draw_text(ctx, label, lf,
            GRect(INSET_X, y + 40, w, lh),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, 0);
}

// Helper: draw car icon at bottom of screen, with road strip on round watches
static void draw_car_bottom(GContext *ctx, GRect bounds) {
#ifdef PBL_ROUND
  int car_w = bounds.size.w * 62 / 100;
  int car_h = car_w * 72 / 161;
  int car_x = bounds.size.w / 2 - car_w / 2;
  int car_y = bounds.size.h - 22 - car_h;
  draw_icon_car(ctx, GRect(car_x, car_y, car_w, car_h), 0, 0);
  draw_road_strip(ctx, bounds);
#else
  int car_w = MIN(bounds.size.w - INSET_X * 2, 150);
  int car_h = car_w * 72 / 161;
  int car_x = bounds.size.w / 2 - car_w / 2;
  draw_icon_car(ctx, GRect(car_x, bounds.size.h - car_h - 2, car_w, car_h), 0, 0);
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
            fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
            GRect(INSET_X, y + 52, w, 36),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);

  int div_y = y + 52 + 36 + 8;
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(INSET_X, div_y), GPoint(bounds.size.w - INSET_X, div_y));

  char temp_buf[24];
  if (s_state.use_metric) {
    int c = (s_state.outside_temp - 32) * 5 / 9;
    snprintf(temp_buf, sizeof(temp_buf), "%d\xC2\xB0""C outside", c);
  } else {
    snprintf(temp_buf, sizeof(temp_buf), "%d\xC2\xB0""F outside", s_state.outside_temp);
  }
  draw_text(ctx, temp_buf, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
            GRect(INSET_X, div_y + 8, w, 38),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 5);
}

static void draw_page_lock(GContext *ctx, GRect bounds) {
  int w = bounds.size.w - INSET_X * 2;
  int lbl_h = 36;
  int gap = 6;
  int icon_y = CONTENT_Y + 2;
  int icon_h = bounds.size.h - icon_y - lbl_h - gap - 8;

  draw_icon_lock(ctx,
    GRect(INSET_X, icon_y, w, icon_h),
    s_state.locked);

  graphics_context_set_text_color(ctx, COLOR_FG);
  draw_text(ctx, s_state.locked ? "locked" : "unlocked",
            fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
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
    draw_big_stat(ctx, bounds, num, "minutes\nuntil full", true);
  }

  // Cable drawn before car so the round cap at port is hidden under the car body
#ifdef PBL_ROUND
  {
    int car_w = bounds.size.w * 62 / 100;
    int car_h = car_w * 72 / 161;
    int car_x = bounds.size.w / 2 - car_w / 2;
    int car_y = bounds.size.h - 22 - car_h;
    draw_charging_cable(ctx, car_x, car_y, car_w, car_h, bounds.size.w, bounds.size.h);
    draw_icon_car(ctx, GRect(car_x, car_y, car_w, car_h), 0, 0);
    draw_road_strip(ctx, bounds);
  }
#else
  {
    int car_w = bounds.size.w * 65 / 100;
    int car_h = car_w * 72 / 161;
    int car_x = bounds.size.w / 2 - car_w / 2;
    int car_y = bounds.size.h - car_h - 2;
    draw_charging_cable(ctx, car_x, car_y, car_w, car_h, bounds.size.w, bounds.size.h);
    draw_icon_car(ctx, GRect(car_x, car_y, car_w, car_h), 0, 0);
  }
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
  draw_big_stat(ctx, bounds, num, "charged", true);
  // ROBOTO_BOLD_SUBSET_49 digits are ~30px wide; % glyph bottom aligned with number bottom
  int digits   = s_state.charge_pct >= 100 ? 3 : (s_state.charge_pct >= 10 ? 2 : 1);
  int glyph_sz = 18;
  draw_percent_glyph(ctx,
    GPoint(INSET_X + digits * 30 + 4, CONTENT_Y + 16),
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
    s_state.use_metric ? "kilometer\nrange" : "mile\nrange", true);
  draw_mountains(ctx, bounds);
  {
    int car_w = bounds.size.w * 55 / 100;
    int car_h = car_w * 72 / 161;
    int car_x = bounds.size.w / 2 - car_w / 2;
#ifdef PBL_ROUND
    int car_y = bounds.size.h - 22 - car_h;
#else
    int car_y = bounds.size.h - car_h - 2;
#endif
    draw_icon_car(ctx, GRect(car_x, car_y, car_w, car_h), 0, 0);
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
    s_state.use_metric ? "kilometers\ndriven" : "miles\ndriven", false);

  draw_globe_and_car(ctx, bounds);
}

static void draw_page_location(GContext *ctx, GRect bounds) {
  int y = CONTENT_Y;
  int w = bounds.size.w - INSET_X * 2;
  graphics_context_set_text_color(ctx, COLOR_FG);
  draw_text(ctx, s_state.location,
            fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
            GRect(INSET_X, y, w, 80),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, 0);
  draw_text(ctx, "current location",
            fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
            GRect(INSET_X, y + 80, w, 36),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);

  int div_y = y + 80 + 36 + 4;
  graphics_context_set_stroke_color(ctx, COLOR_FG);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(INSET_X, div_y), GPoint(bounds.size.w - INSET_X, div_y));

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
  draw_text(ctx, dist_buf,
            fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
            GRect(INSET_X, div_y + 8, w, 36),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, 0);
}

// ── Canvas update ─────────────────────────────────────────────────────────────

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int   page   = (layer == s_anim_layer) ? s_anim_from_page : s_page;

  graphics_context_set_fill_color(ctx, COLOR_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_state.error && layer != s_anim_layer) {
    graphics_context_set_text_color(ctx, COLOR_FG);
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
  if ((t = dict_find(iter, KEY_SETTING_UNITS)))      s_state.use_metric   = (bool)t->value->int32;
  if ((t = dict_find(iter, KEY_STATE_LOCATION)))
    snprintf(s_state.location, sizeof(s_state.location), "%s", t->value->cstring);
  if ((t = dict_find(iter, KEY_STATE_DISTANCE_M))) s_state.distance_m = t->value->int32;

  layer_mark_dirty(s_canvas);
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ── Location action menu ──────────────────────────────────────────────────────

static void do_action_cb(void *data) {
  if (!s_action_window) { s_action_pending = -1; return; }  // already dismissed
  switch (s_action_pending) {
    case PAGE_CLIMATE:
      s_state.climate_on = !s_state.climate_on;
      send_cmd(CMD_TOGGLE_CLIMATE);
      break;
    case PAGE_LOCK:
      s_state.locked = !s_state.locked;
      send_cmd(CMD_TOGGLE_LOCK);
      break;
    case PAGE_LOCATION:
      vibes_short_pulse();
      send_cmd(CMD_HONK);
      break;
    default:
      break;
  }
  s_action_pending = -1;
  window_stack_pop(false);   // non-animated: window unloads synchronously
  layer_mark_dirty(s_canvas); // force redraw with new state
}

static void action_menu_select_cb(int index, void *context) {
  s_action_pending = s_page;
  app_timer_register(50, do_action_cb, NULL);
}

static void do_navigate_cb(void *data) {
  if (!s_action_window) return;
  send_cmd(CMD_NAVIGATE);
  window_stack_pop(false);
}

static void navigate_select_cb(int index, void *context) {
  app_timer_register(50, do_navigate_cb, NULL);
}

static void action_window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  static SimpleMenuItem climate_items[] = {
    { .title = "Toggle Climate", .subtitle = "Turn climate on/off", .callback = action_menu_select_cb },
  };
  static SimpleMenuItem lock_items[] = {
    { .title = "Toggle Lock", .subtitle = "Lock or unlock car", .callback = action_menu_select_cb },
  };
  static SimpleMenuItem location_items[] = {
    { .title = "Honk + Flash", .subtitle = "Locate your car",     .callback = action_menu_select_cb },
    { .title = "Navigate",     .subtitle = "Open maps on phone",  .callback = navigate_select_cb   },
  };
  static SimpleMenuSection sections[1];

  switch (s_page) {
    case PAGE_CLIMATE:
      sections[0] = (SimpleMenuSection){ .title = "Climate", .items = climate_items,  .num_items = 1 };
      break;
    case PAGE_LOCK:
      sections[0] = (SimpleMenuSection){ .title = "Lock",    .items = lock_items,     .num_items = 1 };
      break;
    default:
      sections[0] = (SimpleMenuSection){ .title = "Actions", .items = location_items, .num_items = 2 };
      break;
  }

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

// ── Page transition animation ─────────────────────────────────────────────────

static void update_page_indicator(void) {
  snprintf(s_page_buf, sizeof(s_page_buf), "%d/%d", s_page + 1, PAGE_COUNT);
  text_layer_set_text(s_page_label, s_page_buf);
}

static void transition_stopped(Animation *anim, bool finished, void *context) {
  s_animating = false;
  if (s_anim_layer) {
    layer_remove_from_parent(s_anim_layer);
    layer_destroy(s_anim_layer);
    s_anim_layer = NULL;
  }
  GRect r = layer_get_bounds(window_get_root_layer(s_window));
  layer_set_frame(s_canvas, r);
}

static void navigate(int dir) {
  if (s_animating) return;
  Layer *root   = window_get_root_layer(s_window);
  GRect  bounds = layer_get_bounds(root);
  int    w = bounds.size.w, h = bounds.size.h;

  s_animating      = true;
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
  animation_set_duration((Animation*)pa_in,  280);
  animation_set_curve((Animation*)pa_in,  AnimationCurveEaseInOut);
  animation_set_duration((Animation*)pa_out, 280);
  animation_set_curve((Animation*)pa_out, AnimationCurveEaseInOut);

  Animation *spawn = animation_spawn_create((Animation*)pa_in, (Animation*)pa_out, NULL);
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
