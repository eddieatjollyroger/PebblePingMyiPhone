#include <pebble.h>

/* Find iPhone: press Select and your iPhone dings, through your Home Assistant server.
   No Apple ID needed. Terminal style, dark or light (chosen on the settings page). */

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  #define FONT_S   FONT_KEY_GOTHIC_18
  #define FONT_SB  FONT_KEY_GOTHIC_18_BOLD
  #define FONT_M   FONT_KEY_GOTHIC_24_BOLD
  #define LH_S 21
  #define LH_M 28
  #define PAD 6
#else
  #define FONT_S   FONT_KEY_GOTHIC_14
  #define FONT_SB  FONT_KEY_GOTHIC_14_BOLD
  #define FONT_M   FONT_KEY_GOTHIC_18_BOLD
  #define LH_S 16
  #define LH_M 22
  #define PAD 4
#endif

#define PERSIST_KEY_THEME 1

// AppMessage keys
enum { KEY_CMD = 0, KEY_STATUS = 1, KEY_THEME = 2, KEY_RINGING = 3 };
// Commands to the phone
enum { CMD_PING = 1, CMD_RING = 2, CMD_STOP = 3 };

// Dark: green on black. Light: black on white.
static GColor s_col_bg, s_col_fg, s_col_dim, s_col_warn;
static bool s_light_theme = false;
#define C_BG     s_col_bg
#define C_BRIGHT s_col_fg
#define C_DIM    s_col_dim
#define C_WARN   s_col_warn

static Window *s_window;
static Layer *s_layer;

static char s_status[48] = "ready";
static bool s_ringing = false;

static void set_theme_colors(bool light) {
  s_light_theme = light;
  if (light) {
    s_col_bg = GColorWhite; s_col_fg = GColorBlack; s_col_dim = GColorDarkGray; s_col_warn = GColorRed;
  } else {
    s_col_bg = GColorBlack; s_col_fg = GColorGreen; s_col_dim = GColorIslamicGreen; s_col_warn = GColorYellow;
  }
}

/* On round watches the corners are cut off: draw inside the largest square in the circle. */
static GRect safe_bounds(GRect full) {
#if defined(PBL_ROUND)
  int side = (full.size.w * 707) / 1000;
  return GRect((full.size.w - side) / 2, (full.size.h - side) / 2, side, side);
#else
  return full;
#endif
}

/* ---------- Drawing ---------- */

static void draw_text(GContext *ctx, const char *text, GFont font, GRect rect, GTextAlignment align, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, rect, GTextOverflowModeTrailingEllipsis, align, NULL);
}

static void draw_hint(GContext *ctx, int w, int y, const char *key, const char *action, int key_w) {
  GFont fsb = fonts_get_system_font(FONT_SB);
  GFont fs = fonts_get_system_font(FONT_S);
  draw_text(ctx, key, fsb, GRect(PAD, y, key_w, LH_S), GTextAlignmentLeft, C_BRIGHT);
  draw_text(ctx, action, fs, GRect(PAD + key_w, y, w - PAD - key_w - PAD, LH_S), GTextAlignmentLeft, C_DIM);
}

static void ui_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int w = b.size.w, h = b.size.h;
  GFont fs = fonts_get_system_font(FONT_S);
  GFont fm = fonts_get_system_font(FONT_M);
  GFont fsb = fonts_get_system_font(FONT_SB);

  // Header and dashed divider
  draw_text(ctx, "# find_iphone_", fsb, GRect(PAD, 0, w - 2 * PAD, LH_S), GTextAlignmentLeft, C_BRIGHT);
  graphics_context_set_stroke_color(ctx, C_DIM);
  for (int x = PAD; x < w - PAD; x += 4) {
    graphics_draw_line(ctx, GPoint(x, LH_S + 3), GPoint(x + 1, LH_S + 3));
  }

  // What it does
  int y = LH_S + 8;
  draw_text(ctx, "$ ping --critical", fs, GRect(PAD, y, w - 2 * PAD, LH_S), GTextAlignmentLeft, C_DIM);

  // Status, in bigger bold text. Problems show in the warning color.
  y += LH_S + 4;
  bool problem = strncmp(s_status, "err", 3) == 0 || strncmp(s_status, "no ", 3) == 0;
  char line[64];
  snprintf(line, sizeof(line), "> %s", s_status);
  draw_text(ctx, line, fm, GRect(PAD, y, w - 2 * PAD, LH_M * 2), GTextAlignmentLeft, problem ? C_WARN : C_BRIGHT);

  // Button hints, anchored to the bottom
  int key_w = graphics_text_layout_get_content_size("SELECT ", fsb, GRect(0, 0, 300, 40),
                                                    GTextOverflowModeWordWrap, GTextAlignmentLeft).w + 6;
  int hy = h - 3 * LH_S - 2;
  if (s_ringing) {
    draw_hint(ctx, w, hy + LH_S, "SELECT", "stop ringing", key_w);
  } else {
    draw_hint(ctx, w, hy, "SELECT", "ping once", key_w);
    draw_hint(ctx, w, hy + LH_S, "HOLD", "keep ringing", key_w);
  }
  draw_hint(ctx, w, hy + 2 * LH_S, "BACK", "exit", key_w);
}

/* ---------- Buttons ---------- */

static void send_cmd(int cmd) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK || !iter) return;
  dict_write_int8(iter, KEY_CMD, cmd);
  app_message_outbox_send();
}

static void set_status(const char *text) {
  snprintf(s_status, sizeof(s_status), "%s", text);
  layer_mark_dirty(s_layer);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  vibes_short_pulse();
  if (s_ringing) {
    send_cmd(CMD_STOP);
    set_status("stopping...");
  } else {
    send_cmd(CMD_PING);
    set_status("pinging...");
  }
}

static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  if (s_ringing) return;
  vibes_double_pulse();
  send_cmd(CMD_RING);
  set_status("ringing...");
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long_click, NULL);
  // Back is left alone, so it exits right away
}

/* ---------- Messages from the phone ---------- */

static void apply_theme(bool light, bool save) {
  set_theme_colors(light);
  if (save) persist_write_bool(PERSIST_KEY_THEME, light);
  window_set_background_color(s_window, C_BG);
  layer_mark_dirty(s_layer);
}

static void in_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *theme = dict_find(iter, KEY_THEME);
  if (theme) {
    bool light = theme->value->int32 != 0;
    if (light != s_light_theme) apply_theme(light, true);
  }
  Tuple *ringing = dict_find(iter, KEY_RINGING);
  if (ringing) s_ringing = ringing->value->int32 != 0;
  Tuple *status = dict_find(iter, KEY_STATUS);
  if (status) snprintf(s_status, sizeof(s_status), "%s", status->value->cstring);
  layer_mark_dirty(s_layer);
}

/* ---------- Window ---------- */

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(safe_bounds(layer_get_bounds(root)));
  layer_set_update_proc(s_layer, ui_update_proc);
  layer_add_child(root, s_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_layer);
}

static void init(void) {
  set_theme_colors(persist_exists(PERSIST_KEY_THEME) ? persist_read_bool(PERSIST_KEY_THEME) : false);

  s_window = window_create();
  window_set_background_color(s_window, C_BG);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) { .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(in_received_handler);
  app_message_open(128, 32);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
