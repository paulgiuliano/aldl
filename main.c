#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

/* local objects */
#include "config.h"
#include "acquire.h"
#include "error.h"
#include "dfiler.h"
#include "aldl-io/config.h"
#include "aldl-io/aldl-io.h"

/* ------- GLOBAL----------------------- */

aldl_conf_t *aldl; /* aldl data structure */
aldl_commdef_t *comm; /* comm specs */
dfile_t *config; /* configuration */

/* ------- LOCAL FUNCTIONS ------------- */

/* run cleanup rountines for aldl and serial crap */
int aldl_finish();

/* self explanitory */
int configopt_int(char *str, int min, int max);
byte configopt_byte(char *str);

char *csvbreak(char *buf, char *str, int f);

/* get a REQURIED config option, fatal error if it's missing */
char *configopt(char *str);

/* convert a 0xFF format string to a 'byte', or 00 on error... */
byte hextobyte(char *str);

/* allocate all major structures and load config routines */
void aldl_setup();

/* initial memory allocation routines */
void aldl_alloc_a(); /* fixed structures */
void aldl_alloc_b(); /* definition arrays */
void aldl_alloc_c(); /* more data space */

/* config file loading */
void load_config_a(); /* load data to alloc_a structures */
void load_config_b(); /* load data to alloc_b structures */

int main() {
  /* initialize locking mechanisms */
  init_locks();

  /* parse config file ... never free this structure */
  config = dfile_load("lt1.conf");
  if(config == NULL) fatalerror(ERROR_CONFIG,"cant load config file");

  #ifdef VERBLOSITY
  print_config(config);
  #endif

  /* allocate structures and parse config data */
  aldl_setup();

  set_connstate(ALDL_LOADING,aldl); /* initial connection state */

  /* FIXME this needs to come from load_config or switch to autodetct */
  char *serialport = "d:002/002";
  serial_init(serialport); /* init serial port */

  aldl_acq(aldl); /* start main event loop */

  aldl_finish(comm);

  return 0;
}

void aldl_setup() {
  aldl_alloc_a();
  load_config_a();
  aldl_alloc_b();
  load_config_b();
  aldl_alloc_c();
}

void aldl_alloc_a() {
  /* primary aldl configuration structure */
  aldl = malloc(sizeof(aldl_conf_t));
  if(aldl == NULL) fatalerror(ERROR_MEMORY,"conf_t alloc");
  memset(aldl,0,sizeof(aldl_conf_t));

  /* communication definition */
  comm = malloc(sizeof(aldl_commdef_t));
  if(comm == NULL) fatalerror(ERROR_MEMORY,"commdef alloc");
  memset(comm,0,sizeof(aldl_commdef_t));
  aldl->comm = comm; /* link to conf */

  /* stats tracking structure */
  aldl->stats = malloc(sizeof(aldl_stats_t));
  if(aldl->stats == NULL) fatalerror(ERROR_MEMORY,"stats alloc");
  memset(aldl->stats,0,sizeof(aldl_stats_t));
}

void load_config_a() {
  comm->checksum_enable = configopt_int("CHECKSUM_ENABLE",0,1);;
  comm->pcm_address = configopt_byte("PCM_ADDRESS");
  comm->idledelay = configopt_int("IDLE_DELAY",0,5000);
  comm->chatterwait = configopt_int("IDLE_ENABLE",0,1);
  comm->shutupcommand = generate_mode(configopt_byte("SHUTUP_MODE"),comm);
  comm->returncommand = generate_mode(configopt_byte("RETURN_MODE"),comm);
  comm->shutuprepeat = configopt_int("SHUTUP_REPEAT",0,5000);
  comm->shutuprepeatdelay = configopt_int("SHUTUP_DELAY",0,5000);
  comm->n_packets = configopt_int("N_PACKETS",1,99);
  aldl->n_defs = configopt_int("N_DEFS",1,512);
}

void aldl_alloc_b() {
  /* allocate space to store packet definitions */
  comm->packet = malloc(sizeof(aldl_packetdef_t) * comm->n_packets);
  if(comm->packet == NULL) fatalerror(ERROR_MEMORY,"packet mem");
}

void load_config_b() {
  int x;
  char *pktname = malloc(9); /*PACKET99+0*/
  char *cfgline = NULL;
  char *tmp = NULL;
  for(x=0;x<comm->n_packets;x++) {
    /* packets in config file start at 1, array index starts at 0 ... */
    sprintf(pktname,"PACKET%i",x + 1);
    cfgline = configopt(pktname);
    tmp = malloc(strlen(cfgline)); /* allocate some storage space */
    comm->packet[x].commandlength = 5; /* FIXME remove from spec ... */
    comm->packet[x].id = hextobyte(csvbreak(tmp,cfgline,0));
    comm->packet[x].length = atoi(csvbreak(tmp,cfgline,1));
    comm->packet[x].offset = atoi(csvbreak(tmp,cfgline,2));
    comm->packet[x].frequency = atoi(csvbreak(tmp,cfgline,3));
    generate_pktcommand(&comm->packet[x],comm);
  };
  free(pktname);

  /* sanity checks for single packet mode */
  #ifndef ALDL_MULTIPACKET
  if(comm->packet[0].frequency == 0) {
    fatalerror(ERROR_CONFIG,"the only packet is disabled");
  };
  if(comm->n_packets != 1) {
    fatalerror(ERROR_CONFIG,"this config requires multipacket capabilities");
  };
  #endif
}

void aldl_alloc_c() {
  /* storage for raw packet data */
  int x = 0;
  for(x=0;x<comm->n_packets;x++) {
    comm->packet[x].data = malloc(comm->packet[x].length);
    if(comm->packet[x].data == NULL) fatalerror(ERROR_MEMORY,"pkt data");
  };

  /* storage for data definitions */
  aldl->def = malloc(sizeof(aldl_define_t) * aldl->n_defs);
  if(aldl->def == NULL) fatalerror(ERROR_MEMORY,"definition");

}

char *configopt(char *str) {
  char *val = value_by_parameter(str, config);
  if(val == NULL) fatalerror(ERROR_CONFIG_MISSING,str);
  return val;
};

char *csvbreak(char *buf, char *str, int f) {
  brk_field(buf,f,str);
  if(buf == NULL) fatalerror(ERROR_CONFIG_MISSING,str);
  return buf;
};

int configopt_int(char *str, int min, int max) {
  int x = atoi(configopt(str));
  if(x < min || x > max) fatalerror(ERROR_RANGE,str);
  return x;
};

byte configopt_byte(char *str) {
  return hextobyte(configopt(str));
};

byte hextobyte(char *str) {
  /* FIXME this kinda sucks */
  return (int)strtol(str,NULL,16);
};

int aldl_finish() {
  serial_close();
  return 0;
}

