#include "hack.h"

#ifndef NH_SDL_INCLUDE
#define NH_SDL_INCLUDE

// defines
// TODO: delet this
#define NH_SDL_STACK_SIZE 1024 * 1024
#define NH_SDL_WIN_MAX 16
#define NH_SDL_PATH_TILE "/home/patchouli/src/tiletest/tilemap.png"
#define NH_SDL_PATH_FONT "/home/patchouli/src/fonts/manaspc.ttf"
#define NH_SDL_PATH_SKIN "/home/patchouli/src/skin.png"

// https://nethackwiki.com/wiki/Forum:Level_dimentions%3F
#define NH_SDL_MAP_WIDTH 79
#define NH_SDL_MAP_HEIGHT 21

#define NH_SDL_TILE_WIDTH 16
#define NH_SDL_TILE_HEIGHT 16
#define NH_SDL_TILEMAP_WIDTH 64
#define NH_SDL_TILEMAP_HEIGHT 64
#define NH_SDL_TILEMAP_X NH_SDL_TILE_WIDTH*NH_SDL_TILEMAP_WIDTH
#define NH_SDL_TILEMAP_Y NH_SDL_TILE_HEIGHT*NH_SDL_TILEMAP_HEIGHT

#define NH_SDL_GL_TEXTURES 2
#define NH_SDL_GL_ZOOM 1.1

#define NH_SDL_UI_SCALE_W 0.4
#define NH_SDL_UI_SCALE_H 0.8

#define NH_SDL_ERROR_GL -1
#define NH_SDL_ERROR_PARSE -2
#define NH_SDL_ERROR_TTF -3
#define NH_SDL_ERROR_WIN_TYPE -4

#define UNDEFINED() {printf("Undefined: %s:%d in %s\n", __FILE__, __LINE__, __FUNCTION__);}
#define BLOCK() {for(;;){}}
#define TRY(S) {if (S) {fprintf(stderr, "Error: %s:%d in %s\n", __FILE__, __LINE__, __FUNCTION__);exit(-1);}}
#define NTRY(S) {if (!S) {fprintf(stderr, "Error: %s:%d in %s\n", __FILE__, __LINE__, __FUNCTION__);exit(-1);}}

typedef int windid;
typedef int wintype;

// types
// note to self, always allocate with calloc, so next is NULL
struct sdl_list {
  void *data;
  struct sdl_list *next;
};

static struct sdl_list *sdl_list_last = NULL;

static int sdl_list_len(struct sdl_list *list) {
  int i = 0;

  while (list) {
    list = list->next;
    i++;
  }

  printf("list len returning %d\n", i);
  return i;
}

static void sdl_list_append(struct sdl_list *list, void *data) {
  sdl_list_last->next = calloc(1, sizeof(struct sdl_list));
  sdl_list_last->next->data = data;
  sdl_list_last = sdl_list_last->next;
}

static void sdl_list_free(struct sdl_list *list, void (*data_free)(void*)) {
  if (!list) {
    return;
  } else {
    sdl_list_free(list->next, data_free);
  }
  if (list->data && data_free) {
    data_free(list->data);
  }
  free(list);
}

struct sdl_win {
  // offset by 1 so we know if a menu is not in use (type==0)
  wintype type;
  int len;
  union {
    struct sdl_list *lines;
    struct sdl_list *entries;
  };
};

struct sdl_win_entry {
  int glyph;
  anything identifier;
  char accelerator;
  char groupacc;
  int attr;
  char *str;
  boolean preselected;
  bool selected;
};

// structures for sdl_nk_arg
struct sdl_nk_yn {
  const char *ques;
  const char *choices;
  char def;
  char ret;
};

struct sdl_nk_menu {
  windid window;
  int how;
};

// this is probably defined somewhere else
typedef enum {
  SDL_ALIGN_CHAOTIC,
  SDL_ALIGN_NEUTRAL,
  SDL_ALIGN_LAWFUL,
} sdl_alignment;

// defined in enum statusfields in botl.h
struct sdl_status_state {
  char *title;
  int str;
  int dx;
  int co;
  int in;
  int wi;
  int ch;
  sdl_alignment align;
  long score;
  long cap;
  long gold;
  int ene;
  int enemax;
  long xp;
  int ac;
  int hd;
  int time;
  unsigned int hunger;
  int hp;
  int hpmax;
  char *leveldesc;
  long exp;
  long condition;
};

struct sdl_state {
  int x;
  int y;
  bool center;

  int win_id;
  struct sdl_win window[NH_SDL_WIN_MAX];
  struct sdl_status_state status;
  unsigned int map[NH_SDL_MAP_HEIGHT][NH_SDL_MAP_WIDTH];
};

// globals
extern struct sdl_state sdl_state;
extern pthread_mutex_t sdl_state_mtx;

// pipe
extern int sdl_thread_fd[2];
// the init barrier synchronizes SDL/OpenGL initialization in the drawing thread
// with the main thread, so that sdl_init_nhwindows doesn't return before
// everything is initialized
extern pthread_barrier_t sdl_thread_init;
// the menu cond/mutex are for returning data from a menu
extern pthread_cond_t sdl_thread_menu;
extern pthread_mutex_t sdl_thread_menu_mutex;
// menu function
extern volatile bool (*sdl_nk_func)(void*);
// argument to menu function
extern volatile void *sdl_nk_arg;

static sdl_alignment sdl_parse_alignment(char *ptr) {
  // strcmp instead of strncmp because nethack doesn't give us the string length
         if (!strcmp("Chaotic", ptr)) {
    return SDL_ALIGN_CHAOTIC;
  } else if (!strcmp("Neutral", ptr)) {
    return SDL_ALIGN_NEUTRAL;
  } else if (!strcmp("Lawful", ptr)) {
    return SDL_ALIGN_LAWFUL;
  } else {
    fprintf(stderr, "SDL: parse: error parsing alignment: %s\n", ptr);
    exit(NH_SDL_ERROR_PARSE);
  }
}

//windid sdl_win_id = 0;
//struct sdl_win sdl_state.window[NH_SDL_WIN_MAX] = {0};
//struct sdl_status_state sdl_status_state;

// nethack window functions, defined in winsdl.c
extern void sdl_init_nhwindows(int* argcp, char** argv);
extern void sdl_player_selection(void);
extern void sdl_askname(void);
extern void sdl_get_nh_event(void);
extern void sdl_exit_nhwindows(char *str);
extern void sdl_suspend_nhwindows(char *str);
extern void sdl_resume_nhwindows(void);
extern windid sdl_create_nhwindow(wintype type);
extern void sdl_clear_nhwindow(windid window);
extern void sdl_display_nhwindow(windid window, boolean blocking);
extern void sdl_destroy_nhwindow(windid window);
extern void sdl_curs(windid window, int x, int y);
extern void sdl_putstr(windid window, int attr, char *str);
extern void sdl_display_file(char *str, boolean complain);
extern void sdl_start_menu(windid window);
extern void sdl_add_menu(windid window, int glyph, const anything identifier,
                         char accelerator, char groupacc,
                         int attr, char *str, boolean preselected);
extern void sdl_end_menu(windid window, char *prompt);
extern int sdl_select_menu(windid window, int how, menu_item **selected);
extern void sdl_update_inventory(void);
extern void sdl_mark_synch(void);
extern void sdl_wait_synch(void);
extern void sdl_cliparound(int x, int y);
extern void sdl_print_glyph(windid window, int x, int y, int glyph, int bkglyph);
extern void sdl_raw_print(char *str);
extern void sdl_raw_print_bold(char *str);
extern int sdl_nhgetch(void);
extern int sdl_nh_poskey(int *x, int *y, int *mod);
extern void sdl_nhbell(void);
extern void sdl_doprev_message(void);
extern char sdl_yn_function(const char *ques, const char *choices, char def);
extern void sdl_getlin(const char *ques, char *input);
extern int sdl_get_ext_cmd(void);
extern void sdl_number_pad(void *state); // what is state?
extern void sdl_delay_output(void);
extern void sdl_start_screen(void);
extern void sdl_end_screen(void);
extern char *sdl_getmsghistory(boolean init);
extern void sdl_putmsghistory(char *msg);
extern void sdl_status_init(void);
extern void sdl_status_enablefield(int fldindex, char *fldname, char *fieldfmt, boolean enable);
extern void sdl_status_update(int fldindex, genericptr_t ptr, int chg, int percentage, int color, long *colormasks);

// drawing functions, defined in draw.c
extern int sdl_init(void);

extern bool sdl_nk_yn(struct sdl_nk_yn *yn);
extern bool sdl_nk_menu(struct sdl_nk_menu *arg);
extern void sdl_nk_text();

extern int sdl_init(void);
#endif