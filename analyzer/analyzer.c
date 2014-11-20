#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>

#include "csv.h"
#include "config.h"
#include "loadconfig.h"
#include "error.h"

#define RPM_GRIDSIZE ( GRID_RPM_RANGE / GRID_RPM_INTERVAL )
#define MAP_GRIDSIZE ( GRID_MAP_RANGE / GRID_MAP_INTERVAL )
#define MAF_GRIDSIZE ( GRID_MAF_RANGE / GRID_MAF_INTERVAL )

dfile_t *dconf; /* global configuration */

/* grid cell, float */
typedef struct _anl_fcell_t {
  float low,high;
  double avg;
  unsigned int count;
} anl_fcell_t;

/* grid cell, int */
typedef struct anl_icell_t {
  int low,high;
  double avg;
  unsigned int count;
} anl_icell_t;

/* blm analysis storage */
typedef struct _anl_t {
  anl_fcell_t rpm,map,maf;
  anl_icell_t blm; 
  unsigned int counts;
} anl_t;
anl_t *anl_blm;

/* knock table */
typedef struct _anl_knock_t {
  int t[RPM_GRIDSIZE + 1][MAP_GRIDSIZE + 1];
  int last;
  int total_events;
  int discarded;
  unsigned long total_counts;
} anl_knock_t;
anl_knock_t *anl_knock;

/* wideband analysis table */
typedef struct _anl_ve_t {
  anl_fcell_t t[RPM_GRIDSIZE + 1][MAP_GRIDSIZE + 1];
} anl_ve_t;
anl_ve_t *anl_afrve;

typedef struct _anl_maf_t {
  anl_fcell_t t[MAF_GRIDSIZE + 1];
} anl_maf_t;
anl_maf_t *anl_afrmaf;

typedef struct _anl_wbwot_t {
  anl_fcell_t t[RPM_GRIDSIZE + 1];
} anl_wbwot_t;
anl_wbwot_t *anl_wbwot;

/* configuration storage */
typedef struct _anl_conf_t {
  int n_cols; /* number of columns in a log file */
  /* valid row specifier */
  int valid_min_time; /* minimum timestamp */
  int valid_min_temp; /* minimum temperature */
  /* blm analyzer */
  int blm_on; /* activate the blm analyzer */
  int blm_n_cells; /* number of blm cells */
  int blm_min_count; /* minimum number of counts for a valid cell */
  /* knock analyzer */
  int knock_on; /* activate knock counter */
  int min_knock;
  /* afr mode */
  int sd_enable; /* enable speed density */
  int afr_counts; /* min counts for valid afrcell */
  /* wb analyzer */
  int wb_on; /* use wideband instead of narrowband */
  float wb_min,wb_max,wb_comp;
  /* column identifiers */ 
  int col_timestamp, col_rpm, col_temp, col_lblm, col_rblm, col_cell;
  int col_map, col_maf, col_cl, col_blm, col_wot, col_knock, col_wb;
} anl_conf_t;
anl_conf_t *anl_conf;

typedef struct _anl_stats_t {
  int badlines,goodlines;
} anl_stats_t;
anl_stats_t *stats;

void parse_file(char *data);
void parse_line(char *line);
int verify_line(char *line);

int csvint(char *line, int f);
float csvfloat(char *line, int f);

void prep_anl();

void anl_load_conf(char *filename);
int anl_get_col(char *copt, char *log);
void anl_reset_columns(char *log);

void log_blm(char *line);
void log_knock(char *line);
void log_afr(char *line);

void post_calc_blm();
void post_calc_afr();
void post_calc();

void print_results();
void print_results_blm();
void print_results_knock();
void print_results_afr();

int rpm_cell_offset(int value);
int map_cell_offset(int value);
int maf_cell_offset(int value);

int main(int argc, char **argv) {
  printf("**** aldl-analyzer: CSV Log Analyzer %s ****\n",ANL_VERSION);
  printf("(c)2014 Steve Haslin\n\n");

  /* load config */
  anl_load_conf(ANL_CONFIGFILE);

  /* print config specs */
  printf("Global Config:\n");
  printf("Ignoring timestamps < %i\n", anl_conf->valid_min_time);
  printf("Ignoring temperature < %i\n",anl_conf->valid_min_temp);
  printf("\n");

  prep_anl();

  /* load files ... */
  if(argc < 2) err("No files specified...");
  int x;
  char *log;
  printf("Loading files...\n");
  for(x=1;x<argc;x++) {
    printf("Loading file %s: ",argv[x]);
    log = load_file(argv[x]);
    if(log == NULL) {
      printf("Couldn't load file, skipping.\n");
    } else {
      anl_reset_columns(log);
      parse_file(log);
      free(log);
    }
  }

  post_calc(); /* post-calculations (averaging mostly) */

  print_results();

  return 1;
}

void prep_anl() {
  /* alloc blm anl struct */
  anl_blm = malloc(sizeof(anl_t) * ( anl_conf->blm_n_cells +1 ));
  memset(anl_blm,0,sizeof(anl_t) * ( anl_conf->blm_n_cells +1 ));

  /* config blm struct */
  if(anl_conf->blm_on) {
    int cell;
    anl_t *cdata;
    for(cell=0;cell<anl_conf->blm_n_cells;cell++) {
      cdata = &anl_blm[cell];    
      cdata->map.low = 999999;
      cdata->rpm.low = 999999;
      cdata->blm.low = 999999;
      cdata->maf.low = 999999;
    }
  }

  /* alloc and config stats struct */
  stats = malloc(sizeof(anl_stats_t));
  stats->goodlines = 0;
  stats->badlines = 0;

  /* config knock struct */
  if(anl_conf->knock_on == 1) {
    anl_knock = malloc(sizeof(anl_knock_t));
    memset(anl_knock,0,sizeof(anl_knock_t));
  }

  /* config afr struct */
  if(anl_conf->sd_enable == 1) {
    anl_afrve = malloc(sizeof(anl_ve_t));
    memset(anl_afrve,0,sizeof(anl_ve_t));
  } else {
    anl_afrmaf = malloc(sizeof(anl_maf_t));
    memset(anl_afrmaf,0,sizeof(anl_maf_t));
  }

  /* config wideband pe afr struct */
  if(anl_conf->wb_on == 1) {
    anl_wbwot = malloc(sizeof(anl_wbwot_t));
    memset(anl_wbwot,0,sizeof(anl_wbwot_t));
  }
}

void parse_file(char *data) {
  printf("Parsing data... ");
  char *line = line_start(data,1); /* initial line pointer */
  while(line != NULL) { /* loop for all lines */
    parse_line(line);
    line = line_start(line,1);
  }
  printf("Done.\n");
}

void parse_line(char *line) {
  /* verify line integrity */
  if(verify_line(line) == 0) return;

  /* BRANCHING TO PER-LINE ANALYZERS HERE --------- */
  if(anl_conf->blm_on == 1) log_blm(line);
  if(anl_conf->knock_on == 1) log_knock(line);
  log_afr(line); /* always do afr stuff ... */
}

void print_results() {
  printf("Accepted %i/%i lines.\n",
      stats->goodlines - stats->badlines,
      stats->goodlines + stats->badlines);

  /* BRANCHING TO RESULT PARSERS HERE ----------*/
  if(anl_conf->blm_on == 1) print_results_blm();
  if(anl_conf->knock_on == 1) print_results_knock();
  print_results_afr();
}

void post_calc() {
  /* BRANCHING TO POST_CALCS HERE ---------------*/
  if(anl_conf->blm_on == 1) post_calc_blm();
  post_calc_afr();
}

/******** KNOCK COUNT GRID ANALYZER **************************/

void log_knock(char *line) {
  /* check timestamp minimum */
  if(csvint(line,anl_conf->col_timestamp) < anl_conf->valid_min_time) {
    anl_knock->last = csvint(line,anl_conf->col_knock); /* keep-alive */
    return;
  }

  /* get knock count */
  int knock = csvint(line,anl_conf->col_knock);
  int knock_amount = knock - anl_knock->last;

  /* same knock count as last time */
  if(knock_amount == 0) return;

  /* if rolled counter, or restarting, we dont care which */
  /* (this does mean we're throwing away knock counts on rollover) */
  if(knock_amount < 0) {
    anl_knock->last = knock; /* start with new value */
    return;
  }

  /* at this point, there's a knock event. */

  /* discard statistically insignifigant events */
  if(knock_amount < anl_conf->min_knock) {
    anl_knock->discarded++;
    anl_knock->last = knock; /* reset and continue */
  }

  /* find correct cell */
  int rpmcell = rpm_cell_offset(csvfloat(line,anl_conf->col_rpm));
  int mapcell = map_cell_offset(csvfloat(line,anl_conf->col_map));

  /* incr by 1 */
  anl_knock->t[rpmcell][mapcell]++;

  anl_knock->total_events++; /* increment total counter */
  anl_knock->total_counts += knock_amount;

  /* reset counter */
  anl_knock->last = knock;
}

void print_results_knock() {
  printf("\n**** Knock Increment vs RPM vs MAP ****\n");
  printf("(Records with count incr. < %i ignored)\n",anl_conf->min_knock);

  printf("(This is a total of RECORDS with knock count, NOT ECM counts\n\n");
  int maprow = 0;
  int rpmrow = 0;
  for(maprow=0;maprow<MAP_GRIDSIZE;maprow++) {
    printf(" %4i ",maprow * GRID_MAP_INTERVAL);
  }
  printf("\n");
  for(rpmrow=0;rpmrow<RPM_GRIDSIZE;rpmrow++) {
    printf("%4i\n ",rpmrow * GRID_RPM_INTERVAL);
    printf("   ");
    for(maprow=0;maprow<MAP_GRIDSIZE;maprow++) {
      printf(" %4i ",anl_knock->t[rpmrow][maprow]);
    }
    printf("\n");
  }
  printf("total events: %i  total counts: %lu  discarded: %i\n",
             anl_knock->total_events + anl_knock->discarded,
            anl_knock->total_counts, anl_knock->discarded);
}

/********* BLM CELL ANALYZER ****************************/

void log_blm(char *line) {
  /* check timestamp minimum */
  if(csvint(line,anl_conf->col_timestamp) < anl_conf->valid_min_time) return;

  /* minimum rpm */
  #ifdef MIN_RPM
  if(csvint(line,anl_conf->col_rpm) < MIN_RPM) return;
  #endif

  /* get cell number and confirm it's in range */
  int cell = csvint(line,anl_conf->col_cell);
  if(cell < 0 || cell >= anl_conf->blm_n_cells) return;

  /* check temperature minimum */
  if(csvfloat(line,anl_conf->col_temp) < anl_conf->valid_min_temp) return;

  /* check CL/PE op */
  if(csvint(line,anl_conf->col_cl) != 1) return;
  if(csvint(line,anl_conf->col_blm) != 1) return;
  if(csvint(line,anl_conf->col_wot) == 1) return;

  /* point to cell index */
  anl_t *cdata = &anl_blm[cell];

  /* update counts */
  cdata->counts++;

  /* update blm */
  float blm = (csvfloat(line,anl_conf->col_lblm) +
             csvfloat(line,anl_conf->col_rblm)) / 2;
  cdata->blm.avg += blm; /* will div for avg later */
  if(blm < cdata->blm.low) cdata->blm.low = blm;
  if(blm > cdata->blm.high) cdata->blm.high = blm;

  /* update map */
  float map = csvfloat(line,anl_conf->col_map);
  if(map < cdata->map.low) cdata->map.low = map;
  if(map > cdata->map.high) cdata->map.high = map;
  cdata->map.avg += map;

  /* update maf */
  float maf = csvfloat(line,anl_conf->col_maf);
  if(maf < cdata->maf.low) cdata->maf.low = maf;
  if(maf > cdata->maf.high) cdata->maf.high = maf;
  cdata->maf.avg += maf;

  /* update rpm */
  float rpm = csvfloat(line,anl_conf->col_rpm);
  if(rpm < cdata->rpm.low) cdata->rpm.low = rpm;
  if(rpm > cdata->rpm.high) cdata->rpm.high = rpm;
  cdata->rpm.avg += rpm;
}

void post_calc_blm() {
  int cell;
  anl_t *cdata;
  for(cell=0;cell<anl_conf->blm_n_cells;cell++) {
    cdata = &anl_blm[cell]; /* ptr to cell */
    cdata->blm.avg = cdata->blm.avg / (float)cdata->counts;
    cdata->maf.avg = cdata->maf.avg / (float)cdata->counts;
    cdata->map.avg = cdata->map.avg / (float)cdata->counts;
    cdata->rpm.avg = cdata->rpm.avg / (float)cdata->counts;
  }
}

void print_results_blm() {
  int x;
  anl_t *cdata;
  float overall_blm_avg = 0;
  float overall_blm_count = 0;

  printf("\n**** BLM CELL vs TRIM, MAF, MAP, RPM RANGE/AVG ****\n");
  printf("(Igoring records with records < %i)\n",anl_conf->blm_min_count);

  for(x=0;x<anl_conf->blm_n_cells;x++) {
    cdata = &anl_blm[x];
    if(cdata->counts > anl_conf->blm_min_count) {
      overall_blm_count++;
      overall_blm_avg += cdata->blm.avg;
      printf("\n* Cell %i (%i Hits)\n",x,cdata->counts);
      printf("\tBLM: %i - %i (Avg %.1f)\n",
              cdata->blm.low,cdata->blm.high,cdata->blm.avg);
      printf("\tRPM: %.1f - %.1f RPM (Avg %.1f)\n",
              cdata->rpm.low,cdata->rpm.high,cdata->rpm.avg);
      printf("\tMAP: %.1f - %.1f KPA, (Avg %.1f)\n",
              cdata->map.low,cdata->map.high,cdata->map.avg);
      printf("\tMAF: %.1f - %.1f AFGS, (Avg %.1f)\n",
              cdata->maf.low,cdata->maf.high,cdata->maf.avg);
      if(cdata->blm.avg > 138) {
        printf("\t!!!! This cell is tuned too lean !!!!\n");
      }
      if(cdata->blm.avg < 110) {
        printf("\t!!!! This cell is tuned too rich !!!!\n");
      }
    } else {
      printf("\n* Cell %i (%i Hits) - Not reliable.\n",x,cdata->counts);
    }
  }
  printf("\nOverall useful BLM Average: %.2f (%.3f percent)\n",
        overall_blm_avg / overall_blm_count,
        ( overall_blm_avg / overall_blm_count) / 128);
  if(overall_blm_avg / overall_blm_count < 118) {
    printf("!!!! Overall tune too rich !!!!\n");
  }
  if(overall_blm_avg / overall_blm_count > 138) {
    printf("!!!! Overall tune too lean !!!!\n");
  }
}

/************* AFR ANALYZER ******************************/

void log_afr(char *line) {
  float afr;
  
  /* thresholds */
  if(csvfloat(line,anl_conf->col_temp) < anl_conf->valid_min_temp) return;
  if(csvint(line,anl_conf->col_timestamp) < anl_conf->valid_min_time) return;

  if(anl_conf->wb_on == 1) {
    /* get wb value */
    afr = csvfloat(line,anl_conf->col_wb) - anl_conf->wb_comp;
    if(afr < anl_conf->wb_min || afr > anl_conf->wb_max) return;
  } else { /* use narrowband */
    if(csvint(line,anl_conf->col_cl) == 0) return;
    /* avg of left and right blm */
    afr = ((csvfloat(line,anl_conf->col_lblm) +
            csvfloat(line,anl_conf->col_rblm)) / 2);
  }
  
  if(csvint(line,anl_conf->col_wot) == 0) { /* analyze non-pe records */

    /* minimum rpm */
    #ifdef MIN_RPM
    if(csvint(line,anl_conf->col_rpm) < MIN_RPM) return;
    #endif

    /* a routine for the LT1 that rejects anything in blm cell 17, to detect
       decel (which shouldnt really be factored into ve or maf tables) */
    #ifdef REJECTDECEL
    if(csvint(line,anl_conf->col_cell) == 17) return;
    #endif

    if(anl_conf->sd_enable == 1) {
      /* ve analysis */
      int rpmcell = rpm_cell_offset(csvfloat(line,anl_conf->col_rpm));
      int mapcell = map_cell_offset(csvfloat(line,anl_conf->col_map));
      anl_afrve->t[rpmcell][mapcell].avg += afr;
      anl_afrve->t[rpmcell][mapcell].count++;
    } else {
      /* maf analysis */
      float maf = csvfloat(line,anl_conf->col_maf);
      int mafcell = maf_cell_offset(maf);
      anl_afrmaf->t[mafcell].avg += afr;
      anl_afrmaf->t[mafcell].count++;
    }
  } else if(anl_conf->wb_on == 1) { /* analyze wot record */
    int rpmcell = rpm_cell_offset(csvfloat(line,anl_conf->col_rpm));
    anl_wbwot->t[rpmcell].avg += afr;
    anl_wbwot->t[rpmcell].count++;
  }
}

void post_calc_afr() {
  int maprow = 0;
  int rpmrow = 0;
  int mafrow = 0;
  for(rpmrow=0;rpmrow<RPM_GRIDSIZE;rpmrow++) {
    for(maprow=0;maprow<MAP_GRIDSIZE;maprow++) {
      if(anl_conf->sd_enable == 1) {
        anl_afrve->t[rpmrow][maprow].avg = anl_afrve->t[rpmrow][maprow].avg /
                          anl_afrve->t[rpmrow][maprow].count;
      }
    }
    /* embed wot in this loop */
    if(anl_conf->wb_on == 1) {
      anl_wbwot->t[rpmrow].avg = anl_wbwot->t[rpmrow].avg / 
                                anl_wbwot->t[rpmrow].count;
    }
  }
  if(anl_conf->sd_enable == 0) {
    for(mafrow=0;mafrow<MAF_GRIDSIZE;mafrow++) {
      anl_afrmaf->t[mafrow].avg = anl_afrmaf->t[mafrow].avg /
                         anl_afrmaf->t[mafrow].count;
    }
  }
}

void print_results_afr() {
  int maprow = 0;
  int rpmrow = 0;
  int mafrow = 0;

  int wb = anl_conf->wb_on; /* shortcut */ 

  if(anl_conf->sd_enable == 1) {
    if(wb == 1) {
      printf("\n**** Wideband AFR AVERAGE vs RPM vs MAP ****\n");
    } else {
      printf("\n**** Narrowband Trim vs RPM vs MAP ****\n");  
    }
    printf("(Igoring cells with counts < %i)\n",anl_conf->afr_counts);
    if(wb == 1) {
      printf("(Ignoring wideband AFR < %f and > %f)\n",
               anl_conf->wb_min,anl_conf->wb_max);
      printf("(Adding compensation of %f)\n\n",anl_conf->wb_comp);
    }
    for(maprow=0;maprow<MAP_GRIDSIZE;maprow++) {
      printf(" %4i ",maprow * GRID_MAP_INTERVAL);
    }
    printf("\n");
    for(rpmrow=0;rpmrow<RPM_GRIDSIZE;rpmrow++) {
      printf("%4i\n ",rpmrow * GRID_RPM_INTERVAL);
      printf("   ");
      for(maprow=0;maprow<MAP_GRIDSIZE;maprow++) {
        if(anl_afrve->t[rpmrow][maprow].count <= anl_conf->afr_counts) {
          printf(" .... ");
        } else {
          if(wb == 1) {
            printf(" %4.1f ",anl_afrve->t[rpmrow][maprow].avg);
          } else {
            printf(" %4.0f ",anl_afrve->t[rpmrow][maprow].avg);  
          }
        }
      }
      printf("\n");
    }
  } else {
    if(wb == 1) {
      printf("\n**** Wideband AFR AVERAGE vs MAF AFGS ****\n\n");
      printf("  MAF AFGS    AFR\n");
    } else {
      printf("\n**** Narrowband Trim vs MAF AFGS ****\n\n");
      printf("  MAF AFGS    TRIM    MULT\n");
    }
    for(mafrow=0;mafrow<MAF_GRIDSIZE;mafrow++) {
      printf(" %3i - %3i ",mafrow * GRID_MAF_INTERVAL,
          GRID_MAF_INTERVAL * (mafrow +1));
      if(anl_afrmaf->t[mafrow].count <= anl_conf->afr_counts) {
        printf("   -----");
      } else {
        printf("   %4.1f",anl_afrmaf->t[mafrow].avg);
        if(anl_conf->wb_on == 0) { /* print percentage */
          printf("   %1.3f",anl_afrmaf->t[mafrow].avg / 128);
        }
      }
      printf("\n");
    }
  }

  if(wb == 1) {
    printf("\n**** Wideband AFR (Average) vs RPM during PE ACTIVE ****\n\n");
    for(rpmrow=0;rpmrow<RPM_GRIDSIZE;rpmrow++) {
      if(anl_wbwot->t[rpmrow].count == 0) {
         anl_wbwot->t[rpmrow].avg = 0;
      }
      printf("RPM %4i - %4i    ",rpmrow * GRID_RPM_INTERVAL,
          GRID_RPM_INTERVAL * (rpmrow +1));
      printf("   %4.1f    %i Counts\n",
            anl_wbwot->t[rpmrow].avg, anl_wbwot->t[rpmrow].count);
    }
  }
}

/*--------------------------------------------------------------*/

int verify_line(char *line) {
  if(line == NULL) return 0;

  /* check for too many columns */
  if(field_start(line,anl_conf->n_cols - 1) == NULL) {
    stats->badlines++;
    return 0;
  }

  /* check for not enough columns */
  if(field_start(line,anl_conf->n_cols) != NULL) {
    stats->badlines++;
    return 0;
  }

  stats->goodlines++;
  return 1;
}

int csvint(char *line, int f) {
  return csv_get_int(field_start(line,f));
}

float csvfloat(char *line, int f) {
  return csv_get_float(field_start(line,f));
}

int anl_get_col(char *copt, char *log) {
  char *cname = configopt_fatal(dconf,copt); /* get col name from conf file */
  char *line = line_start(log,0); /* should always be first line ... */  
  char *in;
  int x = 0; /* column index */
  int y; /* slicer */
  for(x=0;x<anl_conf->n_cols;x++) {
    in = csv_get_string(field_start(line,x)); 
    y = 0; 
    /* fix terminator (to ignore bracketed suffix) */
    while(in[y] != 0 && in[y] != '(') y++; in[y] = 0;
    if(faststrcmp(cname,in) == 1) {
      return x; /* found column */
    }
    free(in);
  }
  err("Couldn't find column for %s, named %s",copt,cname);
  return 0;
}

void anl_reset_columns(char *log) {
  /* get number of columns */
  char *line = line_start(log,0);
  int x = 0;
  while(field_start(line,x) != NULL) x++;
  anl_conf->n_cols = x;
  /* get column numbers */
  anl_conf->col_lblm = anl_get_col("COL_LBLM",log);
  anl_conf->col_cell = anl_get_col("COL_CELL",log);
  anl_conf->col_rblm = anl_get_col("COL_RBLM",log);
  if(anl_conf->knock_on == 1) {
    anl_conf->col_knock = anl_get_col("COL_KNOCK",log);
  }
  if(anl_conf->wb_on == 1) {
    anl_conf->col_wb = anl_get_col("COL_WB",log);
  }
  anl_conf->col_timestamp = anl_get_col("COL_TIMESTAMP",log);
  anl_conf->col_rpm = anl_get_col("COL_RPM",log);
  anl_conf->col_temp = anl_get_col("COL_TEMP",log);
  anl_conf->col_map = anl_get_col("COL_MAP",log);
  anl_conf->col_maf = anl_get_col("COL_MAF",log);
  anl_conf->col_cl = anl_get_col("COL_CL",log);
  anl_conf->col_blm = anl_get_col("COL_BLM",log);
  anl_conf->col_wot = anl_get_col("COL_WOT",log);
}

void anl_load_conf(char *filename) {
  anl_conf = malloc(sizeof(anl_conf_t));
  dconf = dfile_load(filename);
  if(dconf == NULL) err("Couldn't load config %s",filename);
  anl_conf->valid_min_time = configopt_int_fatal(dconf,"MIN_TIME",0,999999);
  anl_conf->valid_min_temp  = configopt_int_fatal(dconf,"MIN_TEMP",-20,99999);
  anl_conf->blm_on = configopt_int_fatal(dconf,"BLM_ON",0,1);
  anl_conf->knock_on = configopt_int_fatal(dconf,"KNOCK_ON",0,1);
  if(anl_conf->blm_on == 1) {
    anl_conf->blm_n_cells = configopt_int_fatal(dconf,"N_CELLS",1,255);
    anl_conf->blm_min_count = configopt_int_fatal(dconf,"BLM_MIN_COUNTS",
                                  1,10000);
  }
  if(anl_conf->knock_on == 1) {
    anl_conf->min_knock = configopt_int_fatal(dconf,"KNOCK_MIN",1,65535);
  }
  anl_conf->sd_enable = configopt_int(dconf,"SD_ENABLE",0,1,0);
  anl_conf->wb_on = configopt_int_fatal(dconf,"WB_ON",0,1);
  anl_conf->afr_counts = configopt_int_fatal(dconf,"AFR_MIN_COUNTS",1,65535);
  if(anl_conf->wb_on == 1) {
    anl_conf->wb_min = configopt_float_fatal(dconf,"WB_MIN");
    anl_conf->wb_max = configopt_float_fatal(dconf,"WB_MAX");
    anl_conf->wb_comp = configopt_float(dconf,"WB_COMP",0);
  }
}

int rpm_cell_offset(int value) {
  if(value > GRID_RPM_RANGE) return GRID_RPM_RANGE / GRID_RPM_INTERVAL;
  if(value < 0) return 0;
  if(value < GRID_RPM_INTERVAL) return 0;
  int cell = ((float)value/GRID_RPM_RANGE)*(GRID_RPM_RANGE/GRID_RPM_INTERVAL);
  return cell;
}

int map_cell_offset(int value) {
  if(value > GRID_MAP_RANGE) return GRID_MAP_RANGE / GRID_MAP_INTERVAL;
  if(value < 0) return 0;
  if(value < GRID_MAP_INTERVAL) return 0;
  int cell = ((float)value/GRID_MAP_RANGE)*(GRID_MAP_RANGE/GRID_MAP_INTERVAL);
  return cell;
}

int maf_cell_offset(int value) {
  if(value > GRID_MAF_RANGE) return GRID_MAP_RANGE / GRID_MAP_INTERVAL;
  if(value < 0) return 0;
  if(value < GRID_MAP_INTERVAL) return 0;
  int cell = ((float)value/GRID_MAF_RANGE)*(GRID_MAF_RANGE/GRID_MAF_INTERVAL);
  return cell;
}
