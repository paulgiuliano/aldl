#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ncurses.h>
#include <pthread.h>
#include <string.h>

#include "error.h"
#include "aldl-io.h"
#include "config.h"
#include "loadconfig.h"
#include "useful.h"

enum {
  RED_ON_BLACK = 1,
  BLACK_ON_RED = 2,
  GREEN_ON_BLACK = 3,
  CYAN_ON_BLACK = 4,
  WHITE_ON_BLACK = 5,
  WHITE_ON_RED = 6
};

/* some global cached indexes */
int index_rpm, index_map, index_speed;

#define COLOR_STATUSSCREEN RED_ON_BLACK

/* --- variables ---------------------------- */

int w_height, w_width; /* width and height of window */

aldl_conf_t *aldl; /* global pointer to aldl conf struct */

char *bigbuf; /* a large temporary string construction buffer */

aldl_record_t *rec; /* current record */

byte mfb[15]; /* mode four buffer */

/* --- local functions ------------------------*/

/* center half-width of an element on the screen */
int m4_xcenter(int width);
int m4_ycenter(int height);
 
/* print a centered string */
void m4_statusmessage(char *str);

/* clear screen and display waiting for connection messages */
void m4_cons_wait_for_connection();

/* handle ncurses input */
void m4_consoleif_handle_input();

/* gauges -------------------*/
void m4_draw_data(int x, int y, int gauge_index);

/* tuning stuff */
void set_spark_delta(char advance);

/* --------------------------------------------*/

void *mode4_init(void *aldl_in) {
  aldl = (aldl_conf_t *)aldl_in;

  bigbuf = smalloc(512);

  /* initialize root window */
  WINDOW *root;
  if((root = initscr()) == NULL) {
    error(1,ERROR_NULL,"could not init ncurses");
  }

  curs_set(0); /* remove cursor */
  cbreak(); /* dont req. line break for input */
  nodelay(root,true); /* non-blocking input */
  noecho();

  start_color();
  init_pair(RED_ON_BLACK,COLOR_RED,COLOR_BLACK);
  init_pair(BLACK_ON_RED,COLOR_BLACK,COLOR_RED);
  init_pair(GREEN_ON_BLACK,COLOR_GREEN,COLOR_BLACK);
  init_pair(CYAN_ON_BLACK,COLOR_CYAN,COLOR_BLACK);
  init_pair(WHITE_ON_BLACK,COLOR_WHITE,COLOR_BLACK);
  init_pair(WHITE_ON_RED,COLOR_WHITE,COLOR_RED);

  /* get initial screen size */
  getmaxyx(stdscr,w_height,w_width);

  /* some globals */
  index_rpm = get_index_by_name(aldl,"RPM");
  index_map = get_index_by_name(aldl,"MAP");
  index_speed = get_index_by_name(aldl,"SPEED");

  m4_cons_wait_for_connection();

  while(1) {
    rec = newest_record_wait(aldl,rec);
    if(rec == NULL) { /* disconnected */
      m4_cons_wait_for_connection();
      continue;
    }
    m4_consoleif_handle_input();
    /* DRAW HERE */

    refresh();
    usleep(500);
  }

  sleep(4);
  delwin(root);
  endwin();
  refresh();

  pthread_exit(NULL);
  return NULL;
}

int m4_xcenter(int width) {
  return ( w_width / 2 ) - ( width / 2 );
}

int m4_ycenter(int height) {
  return ( w_height / 2 ) - ( height / 2 );
}

void m4_statusmessage(char *str) {
  clear();
  attron(COLOR_PAIR(COLOR_STATUSSCREEN));
  mvaddstr(m4_ycenter(0),m4_xcenter(strlen(str)),str);
  mvaddstr(1,1,VERSION);
  attroff(COLOR_PAIR(COLOR_STATUSSCREEN));
  refresh();
  usleep(5000);
}

void m4_cons_wait_for_connection() {
  aldl_state_t s = ALDL_LOADING;
  aldl_state_t s_cache = ALDL_CONNECTED; /* cache to avoid redraws */
  while(s > 10) { /* messages >10 are non-connected */
    s = get_connstate(aldl);
    if(s != s_cache) { /* status message has changed */
      m4_statusmessage(get_state_string(s)); /* disp. msg */
      s_cache = s; /* reset cache */
    } else {
      usleep(10000); /* checking conn state too fast is bad */
    }
  }

  m4_statusmessage("Buffering...");
  pause_until_buffered(aldl);

  clear();
}

/* --- GAUGES ---------------------------------- */

void m4_draw_data(int x, int y, int gauge_index) {
  aldl_define_t *def = &aldl->def[gauge_index];
  aldl_data_t *data = &rec->data[gauge_index];
  switch(def->type) {
    case ALDL_FLOAT:
      mvprintw(y,x,"%s: %.*f",
              def->name,1,data->f);
      break;
    case ALDL_INT:
    case ALDL_BOOL:
      mvprintw(y,x,"%s: %i",
          def->name,data->i);
      break;
    default:
      return;
  }
}

void m4_mode4_exit() {
  endwin();
}

void m4_consoleif_handle_input() {
  int c;
  if((c = getch()) != ERR) {
    /* do stuff here */
  }
}

void set_spark_delta(char advance) {
  /* init mode string */
  int x;
  for(x=0;x<15;x++) mfb[x] = 0x00;
  /* add prefix */
  mfb[0] = 0xF4;
  mfb[1] = 0x62;
  mfb[2] = 0x04;
  /* add type */
  if(advance > 0) { /* advance */
    mfb[13] = 0x05;
    mfb[14] = advance;
  } else if(advance < 0) { /* retard */
    mfb[13] = 0x07;
    mfb[14] = advance;
  }
  /* if zero, the string is already prepared for reset . */
  aldl_add_command(mfb, 15, 15);
}
