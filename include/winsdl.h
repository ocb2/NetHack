#include "hack.h"

typedef int windid;
typedef int wintype;

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