#include <pebble.h>

#include "comm.h"
#include "layout.h"

#define NO_ICON -1

static Window *s_window;
static Layer *s_graph_layer;
static TextLayer *s_time_text;
static TextLayer *s_last_bg_text;
static GBitmap *s_trend_bitmap;
static BitmapLayer *s_trend_layer;
static TextLayer *s_delta_text;
static TextLayer *s_iob_text;
static TextLayer *s_data_recency_text;

static int *s_bgs;
static char *s_last_bg_str;
static char *s_delta_str;
static int s_recency_wrt_phone;
static time_t s_last_phone_update_time;

static void update_last_bg(int last);
static void update_trend(int trend);
static void update_iob(char* str);
static void update_delta();
static void update_data_recency();

const int TREND_ICONS[] = {
  NO_ICON,
  RESOURCE_ID_ARROW_DOUBLE_UP,
  RESOURCE_ID_ARROW_SINGLE_UP,
  RESOURCE_ID_ARROW_FORTY_FIVE_UP,
  RESOURCE_ID_ARROW_FLAT,
  RESOURCE_ID_ARROW_FORTY_FIVE_DOWN,
  RESOURCE_ID_ARROW_SINGLE_DOWN,
  RESOURCE_ID_ARROW_DOUBLE_DOWN,
  NO_ICON,
  NO_ICON
};

static void data_callback(DictionaryIterator *received) {
  s_last_phone_update_time = time(NULL);

  // TODO use appKeys
  Tuple *sgv_tuple = dict_find(received, 0);
  char* bgs = sgv_tuple->value->cstring;

  // TODO this is temporary
  int last_bg = 0;
  for(int i = 0; i < 36; i++) {
    s_bgs[i] = bgs[i];
    if(bgs[i] != 0) {
      last_bg = bgs[i];
    }
  }

  layer_mark_dirty(s_graph_layer);
  update_last_bg(last_bg);
  update_delta();

  Tuple *trend_tuple = dict_find(received, 1);
  int trend = trend_tuple->value->int32;
  update_trend(trend);

  // TODO use timestamp instead after figuring out timezones
  Tuple *recency_tuple = dict_find(received, 2);
  s_recency_wrt_phone = recency_tuple->value->int32;

  // TODO send as number, not string
  Tuple *iob_tuple = dict_find(received, 3);
  char* iob_str = iob_tuple->value->cstring;
  update_iob(iob_str);

  update_data_recency();
}

static void plot_point(int x, int y, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(x, y, 3, 3), 0, GCornerNone);
}

static int y_from_bg(int bg) {
  // TODO max displayable should be 300
  // TODO round, don't use int division
  return 100 - bg / 3;
}

static void graph_update_proc(Layer *layer, GContext *ctx) {
  unsigned int i, x, y;

  unsigned int bg_len = 36;
  for(i = 0; i < bg_len; i++) {
    int bg = s_bgs[i];
    if(bg == 0) {
      continue;
    }
    x = 3 * i;
    y = y_from_bg(bg);

    plot_point(x, y, ctx);
  }

  static int limits[] = {75, 200};
  for(i = 0; i < ARRAY_LENGTH(limits); i++) {
    y = y_from_bg(limits[i]);
    for(x = 0; x < 3 * 36; x += 4) {
      graphics_draw_line(ctx, GPoint(x, y), GPoint(x + 2, y));
    }
  }

  // TODO don't draw gridline if it's a limit
  static int gridlines[] = {50, 100, 150, 200, 250};
  for(i = 0; i < ARRAY_LENGTH(gridlines); i++) {
    y = y_from_bg(gridlines[i]);
    for(x = 0; x < 3 * 36; x += 6) {
      graphics_draw_line(ctx, GPoint(x, y), GPoint(x + 1, y));
    }
  }
}

static void update_last_bg(int last) {
  snprintf(s_last_bg_str, 4, "%d", last);
  text_layer_set_text(s_last_bg_text, s_last_bg_str);
}

static void update_trend(int trend) {
  static int last_trend = -1;
  if (trend == last_trend) {
    return;
  }
  last_trend = trend;

  if (TREND_ICONS[trend] == NO_ICON) {
    layer_set_hidden(bitmap_layer_get_layer(s_trend_layer), true);
  } else {
    layer_set_hidden(bitmap_layer_get_layer(s_trend_layer), false);
    if (s_trend_bitmap != NULL) {
      gbitmap_destroy(s_trend_bitmap);
    }
    s_trend_bitmap = gbitmap_create_with_resource(TREND_ICONS[trend]);
    bitmap_layer_set_bitmap(s_trend_layer, s_trend_bitmap);
  }
}

static void update_iob(char* str) {
  text_layer_set_text(s_iob_text, str);
}

static void update_data_recency() {
  time_t now = time(NULL);
  int phone_min = (now - s_last_phone_update_time) / 60;
  int data_min = (s_recency_wrt_phone + now - s_last_phone_update_time) / 60;

  if(phone_min >= 10 || data_min >= 10) {
    text_layer_set_font(s_data_recency_text, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  } else {
    text_layer_set_font(s_data_recency_text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  }

  static char s_recency_buffer[16];
  snprintf(s_recency_buffer, sizeof(s_recency_buffer), "(%d/%d)", phone_min, data_min);

  text_layer_set_text(s_data_recency_text, s_recency_buffer);
}

static void update_delta() {
  static char s_delta_as_i_buffer[6];

  if(s_bgs[36 - 1] == 0 || s_bgs[36 - 2] == 0) {
    strcpy(s_delta_str, "-");
  } else {
    int delta = s_bgs[36 - 1] - s_bgs[36 - 2];
    snprintf(s_delta_as_i_buffer, sizeof(s_delta_as_i_buffer), "%d", delta);

    if(delta >= 0) {
      strcpy(s_delta_str, "+");
    } else {
      strcpy(s_delta_str, "");
    }
    strcat(s_delta_str, s_delta_as_i_buffer);
  }

  text_layer_set_text(s_delta_text, s_delta_str);
}

static void update_time(struct tm *time_now) {
  if(time_now == NULL) {
    time_t now = time(NULL);
    time_now = localtime(&now);
  }

  static char s_time_buffer[16];
  strftime(s_time_buffer, sizeof(s_time_buffer), "%l:%M", time_now);
  // Remove leading space if present
  if(s_time_buffer[0] == ' ') {
    memmove(s_time_buffer, &s_time_buffer[1], sizeof(s_time_buffer)-1);
  };
  text_layer_set_text(s_time_text, s_time_buffer);
}

static void minute_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  update_data_recency();
}

static void window_load(Window *s_window) {
  LayoutLayers layout = init_layout(s_window);

  // ensure the time is drawn before anything else
  GRect time_area_bounds = layer_get_bounds(layout.time_area);

  int time_margin_r = 2;
  s_time_text = text_layer_create(GRect(0, 4, time_area_bounds.size.w - time_margin_r, time_area_bounds.size.h));
  text_layer_set_background_color(s_time_text, GColorClear);
  text_layer_set_font(s_time_text, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_time_text, GTextAlignmentRight);
  layer_add_child(layout.time_area, text_layer_get_layer(s_time_text));

  update_time(NULL);


  GRect graph_bounds = layer_get_bounds(layout.graph);

  s_graph_layer = layer_create(GRect(0, 0, graph_bounds.size.w, graph_bounds.size.h));
  layer_add_child(layout.graph, s_graph_layer);
  layer_set_update_proc(s_graph_layer, graph_update_proc);


  GRect sidebar_bounds = layer_get_bounds(layout.sidebar);

  s_last_bg_text = text_layer_create(GRect(0, 3, sidebar_bounds.size.w, 24));
  text_layer_set_font(s_last_bg_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_background_color(s_last_bg_text, GColorClear);
  text_layer_set_text_alignment(s_last_bg_text, GTextAlignmentCenter);
  layer_add_child(layout.sidebar, text_layer_get_layer(s_last_bg_text));

  int trend_arrow_width = 25;
  s_trend_layer = bitmap_layer_create(GRect((sidebar_bounds.size.w - trend_arrow_width) / 2, 3 + 28, trend_arrow_width, trend_arrow_width));
  layer_add_child(layout.sidebar, bitmap_layer_get_layer(s_trend_layer));

  s_delta_text = text_layer_create(GRect(0, 3 + 22 + 28, sidebar_bounds.size.w, 24));
  text_layer_set_font(s_delta_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_background_color(s_delta_text, GColorClear);
  text_layer_set_text_alignment(s_delta_text, GTextAlignmentCenter);
  layer_add_child(layout.sidebar, text_layer_get_layer(s_delta_text));


  GRect row_bounds = layer_get_bounds(layout.row);

  int sm_text_margin = 2;
  s_iob_text = text_layer_create(GRect(sm_text_margin, 0, row_bounds.size.w / 2 - sm_text_margin, 22));
  text_layer_set_background_color(s_iob_text, GColorClear);
  text_layer_set_font(s_iob_text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_iob_text, GTextAlignmentLeft);
  layer_add_child(layout.row, text_layer_get_layer(s_iob_text));

  s_data_recency_text = text_layer_create(GRect(row_bounds.size.w / 2 - sm_text_margin, 0, row_bounds.size.w / 2, 22));
  text_layer_set_background_color(s_data_recency_text, GColorClear);
  text_layer_set_font(s_data_recency_text, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_data_recency_text, GTextAlignmentRight);
  layer_add_child(layout.row, text_layer_get_layer(s_data_recency_text));


  tick_timer_service_subscribe(MINUTE_UNIT, minute_handler);
}

static void window_unload(Window *s_window) {
  TextLayer* to_destroy[] = {
    s_time_text,
    s_last_bg_text,
    s_delta_text,
    s_iob_text,
    s_data_recency_text
  };
  for(unsigned int i = 0; i < ARRAY_LENGTH(to_destroy); i++) {
    text_layer_destroy(to_destroy[i]);
  }

  if (s_trend_bitmap != NULL) {
    gbitmap_destroy(s_trend_bitmap);
  }
  bitmap_layer_destroy(s_trend_layer);

  layer_destroy(s_graph_layer);
  deinit_layout();
}

static void init(void) {
  init_comm(data_callback);

  s_bgs = malloc(36 * sizeof(int));
  s_last_bg_str = malloc(8 * sizeof(char));
  s_delta_str = malloc(4 * sizeof(char));
  s_last_phone_update_time = time(NULL);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
  free(s_bgs);
  free(s_last_bg_str);
  free(s_delta_str);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}