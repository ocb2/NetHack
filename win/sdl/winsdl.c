#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#include "botl.h"
#include "hack.h"

extern short glyph2tile[];

#include "winsdl.h"
// globals
struct sdl_state sdl_state = {0};
pthread_mutex_t sdl_state_mtx;

// pipe
int sdl_thread_fd[2];
// the init barrier synchronizes SDL/OpenGL initialization in the drawing thread
// with the main thread, so that sdl_init_nhwindows doesn't return before
// everything is initialized
pthread_barrier_t sdl_thread_init;
// the menu cond/mutex are for returning data from a menu
pthread_cond_t sdl_thread_menu;
pthread_mutex_t sdl_thread_menu_mutex;
// menu function
volatile bool (*sdl_nk_func)(void*);
// argument to menu function
volatile void *sdl_nk_arg;

// in SDL proper, all names are prefixed with SDL (uppercase)
// in this (SDL window port of nethack), names are prefixed with sdl (lowercase)
// except for macros, which are uppercase, so they will be prefixed NH_SDL
// the other two prefixes are TTF for SDL_ttf library, and nk for nuklear
// the rest of nethack does not appear to keep a prefix

// TODO: fails randomly on SIGINT, fix this eventually
// specifically, SDL_PollEvent() does not return, for some reason

struct window_procs sdl_procs = {
  "sdl",
  (0
#ifdef MSDOS
   | WC_TILED_MAP | WC_ASCII_MAP
#endif
#if defined(WIN32CON)
   | WC_MOUSE_SUPPORT
#endif
   | WC_COLOR | WC_HILITE_PET | WC_INVERSE | WC_EIGHT_BIT_IN),
  (0
#if defined(SELECTSAVED)
   | WC2_SELECTSAVED
#endif
#if defined(STATUS_HILITES)
   | WC2_HILITE_STATUS | WC2_HITPOINTBAR | WC2_FLUSH_STATUS
#endif
   | WC2_DARKGRAY),
  sdl_init_nhwindows,
  sdl_player_selection,
  sdl_askname,
  sdl_get_nh_event,
  sdl_exit_nhwindows,
  sdl_suspend_nhwindows,
  sdl_resume_nhwindows,
  sdl_create_nhwindow,
  sdl_clear_nhwindow,
  sdl_display_nhwindow,
  sdl_destroy_nhwindow,
  sdl_curs,
  sdl_putstr,
  genl_putmixed,
  sdl_display_file,
  sdl_start_menu,
  sdl_add_menu,
  sdl_end_menu,
  sdl_select_menu,
  genl_message_menu,
  sdl_update_inventory,
  sdl_mark_synch,
  sdl_wait_synch,
#ifdef CLIPPING
  sdl_cliparound,
#endif
#ifdef POSITIONBAR
  sdl_update_positionbar,
#endif
  sdl_print_glyph,
  sdl_raw_print,
  sdl_raw_print_bold,
  sdl_nhgetch,
  sdl_nh_poskey,
  sdl_nhbell,
  sdl_doprev_message,
  sdl_yn_function,
  sdl_getlin,
  sdl_get_ext_cmd,
  sdl_number_pad,
  sdl_delay_output,
#ifdef CHANGE_COLOR /* the Mac uses a palette device */
  sdl_change_color,
#ifdef MAC
  sdl_change_background,
  set_sdl_font_name,
#endif
  sdl_get_color_string,
#endif

  sdl_start_screen,
  sdl_end_screen,
  genl_outrip,
  genl_preference_update,
  sdl_getmsghistory,
  sdl_putmsghistory,
  sdl_status_init,
  genl_status_finish,
  sdl_status_enablefield,
  sdl_status_update,
  genl_can_suspend_yes,
};

// nethack window API
void sdl_init_nhwindows(int *argcp, char **argv) {
  pthread_t thread;
  pthread_attr_t thread_attr;

  pipe(sdl_thread_fd);

  pthread_attr_init(&thread_attr);
  pthread_barrier_init(&sdl_thread_init, NULL, 2);
  printf("affffdsfs %p\n", &sdl_thread_init);
  pthread_cond_init(&sdl_thread_menu, NULL);
  pthread_mutex_init(&sdl_thread_menu_mutex, NULL);
  pthread_mutex_init(&sdl_state_mtx, NULL);

  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&thread, &thread_attr, &sdl_init, NULL);
  pthread_barrier_wait(&sdl_thread_init);
  pthread_barrier_destroy(&sdl_thread_init);
}

void sdl_player_selection(void) {
  UNDEFINED();
}

void sdl_askname(void) {
  UNDEFINED();
}

void sdl_get_nh_event(void) {
  UNDEFINED();
}

void sdl_exit_nhwindows(char *str) {
  UNDEFINED();
}

void sdl_suspend_nhwindows(char *str) {
  UNDEFINED();
}

void sdl_resume_nhwindows(void) {
  UNDEFINED();
}

windid sdl_create_nhwindow(wintype type) {
  pthread_mutex_lock(&sdl_state_mtx);

  windid id = sdl_state.win_id++;
  sdl_state.window[id].type = ++type;

  pthread_mutex_unlock(&sdl_state_mtx);
  return id;
}

void sdl_clear_nhwindow(windid window) {
  UNDEFINED();
}

void sdl_display_nhwindow(windid window, boolean blocking) {
  printf("displaywindow %d %d\n", window, blocking);
  UNDEFINED();
}

void sdl_destroy_nhwindow(windid window) {
  UNDEFINED();
  sdl_list_free(sdl_state.window[window].entries, free);
  memset(&sdl_state.window[window], 0, sizeof(struct sdl_win));
}

void sdl_curs(windid window, int x, int y) {
  UNDEFINED();
}

void sdl_putstr(windid window, int attr, char *str) {
  printf("%s\n", str);
  UNDEFINED();
}

void sdl_display_file(char *str, boolean complain) {
  printf("%s\n", str);
  UNDEFINED();
}

void sdl_start_menu(windid window) {
  UNDEFINED();
  if (sdl_state.window[window].entries) {
    sdl_list_free(sdl_state.window[window].entries, free);
    sdl_state.window[window].len = 0;
    sdl_state.window[window].entries = NULL;
    sdl_list_last = NULL;
  }
}

void sdl_add_menu(windid window,
                  int glyph,
                  const anything identifier,
                  char accelerator,
                  char groupacc,
                  int attr,
                  char *str,
                  boolean preselected) {
  UNDEFINED();
  struct sdl_list *list = (struct sdl_list*)calloc(1, sizeof(struct sdl_list));
  struct sdl_win_entry *entry = (struct sdl_win_entry*)calloc(1, sizeof(struct sdl_win_entry));

  list->data = (void*)entry;

  entry->glyph = glyph;
  entry->identifier = identifier;
  entry->accelerator = accelerator;
  entry->groupacc = groupacc;
  entry->attr = attr;
  entry->str = str;
  entry->preselected = preselected;
  entry->selected = false;

  if (!sdl_state.window[window].entries) {
    sdl_state.window[window].entries = list;
    sdl_list_last = list;
  } else {
    sdl_list_append(sdl_state.window[window].entries, entry);
  }

  sdl_state.window[window].len++;
}

void sdl_end_menu(windid window, char *prompt) {
  // the documentation seems unclear on what exactly this is supposed to do, so
  // i'm going to leave it as a nop
  UNDEFINED();
}

int sdl_select_menu(windid window, int how, menu_item **selected) {
  struct sdl_list *list;
  struct sdl_win_entry *entry;
  menu_item *item;
  int o=0;
  UNDEFINED();
  if (sdl_state.window[window].type-1 == NHW_MENU) {
    volatile struct sdl_nk_menu arg = {
      .window = window,
      .how = how
    };
    sdl_nk_arg = &arg;
    sdl_nk_func = sdl_nk_menu;
  } else if (sdl_state.window[window].type-1 == NHW_TEXT) {
    printf("undefined in select-menu\n");
    exit(0);
    sdl_nk_arg = sdl_list_str(sdl_state.window[window].lines);
    sdl_nk_func = sdl_nk_text;
  } else {
    fprintf(stderr, "SDL: error: invalid window type\n");
    exit(NH_SDL_ERROR_WIN_TYPE);
  }

  pthread_mutex_lock(&sdl_thread_menu_mutex);
  pthread_cond_wait(&sdl_thread_menu, &sdl_thread_menu_mutex);
  pthread_mutex_unlock(&sdl_thread_menu_mutex);

  // FIXME: we iterate over this twice?
  // partially copied from wintty.c's tty_select_menu
  for (list = sdl_state.window[window].entries; list; list=list->next) {
    entry = (struct sdl_win_entry *)list->data;
    if (entry->selected) {
      o++;
    }
  }

  if (o) {
    *selected = (menu_item *)malloc(sizeof(menu_item) * o);
    for (item = *selected,
         list = sdl_state.window[window].entries; list; list=list->next) {
      entry = (struct sdl_win_entry *)list->data;
      if (entry->selected) {
        item->item = entry->identifier;
        item->count = -1;
        item++;
      }
    }
  } else {
    selected = NULL;
  }

  return o;
}

void sdl_update_inventory(void) {
  UNDEFINED();
}

void sdl_mark_synch(void) {
  UNDEFINED();
}

void sdl_wait_synch(void) {
  UNDEFINED();
}

void sdl_cliparound(int x, int y) {
  pthread_mutex_lock(&sdl_state_mtx);
  sdl_state.x = x;
  sdl_state.y = y;
  sdl_state.center = true;
  pthread_mutex_unlock(&sdl_state_mtx);
}

// TODO: deal with bkglyph
void sdl_print_glyph(windid window, int x, int y, int glyph, int bkglyph) {
  #ifdef DEBUG
  assert(x < NH_SDL_MAP_WIDTH);
  assert(y < NH_SDL_MAP_HEIGHT);
  #endif

  pthread_mutex_lock(&sdl_state_mtx);
  // offset of 1, due to 0 being blank space
  sdl_state.map[y][x] = glyph2tile[glyph] + 1;
  pthread_mutex_unlock(&sdl_state_mtx);
}

void sdl_raw_print(char *str) {
  printf("%s\n", str);
  //  UNDEFINED();
}

void sdl_raw_print_bold(char *str) {
  printf("%s\n", str);
  //  UNDEFINED();
}

int sdl_nhgetch(void) {
  UNDEFINED();
  BLOCK();
}

int sdl_nh_poskey(int *x, int *y, int *mod) {
  int c;
  read(sdl_thread_fd[0], &c, sizeof(int));
  return c;
}

void sdl_nhbell(void) {
  UNDEFINED();
}

void sdl_doprev_message(void) {
  UNDEFINED();
}

char sdl_yn_function(const char *ques, const char *choices, char def) {
  struct sdl_nk_yn yn = {
    .ques = ques,
    .choices = choices,
    .def = def
  };

  sdl_nk_arg = (void *)&yn;
  sdl_nk_func = sdl_nk_yn;

  pthread_mutex_lock(&sdl_thread_menu_mutex);
  pthread_cond_wait(&sdl_thread_menu, &sdl_thread_menu_mutex);
  pthread_mutex_unlock(&sdl_thread_menu_mutex);

  return yn.ret;
}

void sdl_getlin(const char *ques, char *input) {
  UNDEFINED();
}

int sdl_get_ext_cmd(void) {
  UNDEFINED();
}

void sdl_number_pad(void *state) {
  UNDEFINED();
}

void sdl_delay_output(void) {
  UNDEFINED();
}

void sdl_start_screen(void) {
  UNDEFINED();
}

void sdl_end_screen(void) {
  UNDEFINED();
}

char *sdl_getmsghistory(boolean init) {
  UNDEFINED();
  return NULL;
}

void sdl_putmsghistory(char *msg) {
  UNDEFINED();
}

void sdl_status_init(void) {
  // we do all of the init for this in our window init function, so there is not
  // really anything to do here
  return;
}

void sdl_status_enablefield(int fldindex,
                            char *fldname,
                            char *fieldfmt,
                            boolean enable) {
  UNDEFINED();
}

void sdl_status_update(int fldindex,
                       genericptr_t ptr,
                       int chg,
                       int percentage,
                       int color,
                       long *colormasks) {
  printf("status: %d %d %s\n", fldindex, chg, ptr);
  // TODO: use strto* functions instead of ato*
  switch (fldindex) {
  case BL_CHARACTERISTICS:
  case BL_FLUSH:
    // we force a redraw after this anyway
    break;
  case BL_TITLE:
    sdl_state.status.title = (char*)ptr;
    break;
  case BL_STR:
    //TODO: shouldnt this be error checked?
    sdl_state.status.str = atoi(ptr);
    break;
  case BL_DX:
    sdl_state.status.dx = atoi(ptr);
    break;
  case BL_CO:
    sdl_state.status.co = atoi(ptr);
    break;
  case BL_IN:
    sdl_state.status.in = atoi(ptr);
    break;
  case BL_WI:
    sdl_state.status.wi = atoi(ptr);
    break;
  case BL_CH:
    sdl_state.status.ch = atoi(ptr);
    break;
  case BL_ALIGN:
    sdl_state.status.align = sdl_parse_alignment(ptr);
    break;
  case BL_SCORE:
    sdl_state.status.score = atol(ptr);
    break;
  case BL_CAP:
    sdl_state.status.cap = atol(ptr);
    break;
  case BL_GOLD:
    // as per window.doc, gold quantity preceded by currency symbol and a colon
    sdl_state.status.gold = atol(ptr+2);
    break;
  case BL_ENE:
    sdl_state.status.ene = atoi(ptr);
    break;
  case BL_ENEMAX:
    sdl_state.status.enemax = atoi(ptr);
    break;
  case BL_XP:
    sdl_state.status.xp = atol(ptr);
    break;
  case BL_AC:
    sdl_state.status.ac = atoi(ptr);
    break;
  case BL_HD:
    sdl_state.status.hd = atoi(ptr);
    break;
  case BL_TIME:
    sdl_state.status.time = atoi(ptr);
    break;
  case BL_HUNGER:
    // atoi returns a signed integer
    sdl_state.status.hunger = (unsigned int)atol(ptr);
    break;
  case BL_HP:
    sdl_state.status.hp = atoi(ptr);
    break;
  case BL_HPMAX:
    sdl_state.status.hpmax = atoi(ptr);
    break;
  case BL_LEVELDESC:
    sdl_state.status.leveldesc = (char*)ptr;
    break;
  case BL_EXP:
    sdl_state.status.exp = atol(ptr);
    break;
  case BL_CONDITION:
    sdl_state.status.condition |= (long)ptr;
    break;
  default:
    break;
  }
}