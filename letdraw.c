/***
 *  letdraw: command-line drawing tool
 *    - see README.mdown for instructions
 *
 *  Copyright (C) 2014  Raphael Sousa Santos, http://www.raphaelss.com/
 *
 *  This file is part of letdraw.
 *
 *  Letdraw is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ***/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <getopt.h>
#include "draw.h"

#define STACK_ALLOC 20

struct state {
  double x, y;
  unsigned angle;
};

struct global_state {
  struct state *stack;
  struct state current;
  unsigned stack_n, stack_max, d_count, repeat_count;
};

struct instruction {
  char ch;
  int (*operation)(struct global_state*, struct draw_dev*);
};

void usage(void);
const char* read_opts(struct draw_dev_conf* conf, struct global_state* gs,
                      const char** filename, int argc, char** argv);
int init_global_state(struct global_state* gs);
void release_global_state(struct global_state* gs);
int push_state(struct global_state* gs);
int pop_state(struct global_state* gs);
void update_state(struct state* s, double dist);
void update_state_draw(struct global_state* gs, struct draw_dev* dr);
int do_char(int ch, struct global_state* gs, struct draw_dev* dr);

//Operations
int op_line(struct global_state* gs, struct draw_dev* dr);
int op_move(struct global_state* gs, struct draw_dev* dr);
int op_reset(struct global_state* gs, struct draw_dev* dr);
int op_move_to_origin(struct global_state* gs, struct draw_dev* dr);
int op_push_stack(struct global_state* gs, struct draw_dev* dr);
int op_pop_stack(struct global_state* gs, struct draw_dev* dr);
int op_15deg_counterclockwise(struct global_state* gs, struct draw_dev* dr);
int op_15deg_clockwise(struct global_state* gs, struct draw_dev* dr);

struct instruction instructions [] = {
  {'d', op_line},
  {'u', op_move},
  {'r', op_reset},
  {'o', op_move_to_origin},
  {'[', op_push_stack},
  {']', op_pop_stack},
  {'<', op_15deg_counterclockwise},
  {'>', op_15deg_clockwise},
  {0}
};

int main(int argc, char **argv)
{
  struct global_state gs;
  struct draw_dev_conf conf;
  draw_dev_conf_default(&conf);
  const char* infile = NULL;
  FILE *fin = stdin;
  const char* filepath = read_opts(&conf, &gs, &infile, argc, argv);
  if(!filepath) {
    usage();
    return 1;
  }
  if(infile) {
    if(!(fin = fopen(infile, "r"))) {
      fprintf(stderr, "Error opening file: %s\n", infile);
      return 1;
    }
  }
  if(init_global_state(&gs)) {
    fputs("Memory allocation error while initializing global state", stderr);
    if(infile) {
      fclose(fin);
    }
    return 1;
  }
  struct draw_dev* dr = draw_init(&conf);
  if(!dr) {
    fputs("Error initializing drawing system", stderr);
    release_global_state(&gs);
    if(infile) {
      fclose(fin);
    }
    return 1;
  }
  int ch;
  while((ch = getc(fin)) != EOF && !(do_char(ch, &gs, dr)));
  update_state_draw(&gs, dr);
  if(draw_finish(dr, filepath)) {
    fprintf(stderr, "Error writing image (%s)\n", filepath);
  }
  release_global_state(&gs);
  if(infile) {
    fclose(fin);
  }
  return 0;
}

void usage(void)
{
  puts
    (
     "Usage: letdraw --out=FILE [OPTION]\n"
     "  -h --help                Display this message\n"
     "  -o --out=*.(pdf|png)     Output file (required)\n"
     "  -i --in=FILE             Input file containing sequence of characters\n"
     "                           If no file is specified, letdraw reads from\n"
     "                           STDIN\n"
     "  -w --width=NATURAL       Width of image canvas (default: 800)\n"
     "  -H --height=NATURAL      Height of image canvas (default: 600)\n"
     "  -x --origin_x=REAL       X of starting point (default: width/2)\n"
     "  -y --origin_y=REAL       Y of starting point (default: height/2)\n"
     "  -s --scale=REAL>0        Scale drawing lines (default: 1.0)\n"
     "  -l --line_width=REAL>0   Width of line stroke (default: 2.0)\n"
     "  -c --line_cap=(normal|round|square) Line end shape (default: normal)\n"
     "Letdraw reads characters and treats some of them as instructions for a \n"
     "drawing machine while ignoring the others.\n"
     "Supported characters:\n"
     "  d : Move forward drawing line\n"
     "  u : Move forward without drawing\n"
     "  < : Turn 15 degrees counterclockwise\n"
     "  > : Turn 15 degrees clockwise\n"
     "  [ : Push state (position and direction) into stack\n"
     "  ] : Pop state (position and direction) from stack\n"
     "  o : Move to origin without drawing\n"
     "  r : Move to origin without drawing and reset angle to 0 degrees\n"
     "  # : Execute next instruction # times\n"
     "# = any single digit number.\n"
     "# instruction is cumulative. Ex.: 2d = dd, 3d = ddd, 23d = 6d.\n"
     "Stack usage must be balanced (can't pop an empty stack)."
     );
}

const char* read_opts(struct draw_dev_conf* conf, struct global_state* gs,
                      const char** infile, int argc, char **argv)
{
  static struct option opts [] = {
    {"help", no_argument, NULL, 1},
    {"out", required_argument, NULL, 2},
    {"in", required_argument, NULL, 3},
    {"width", required_argument, NULL, 4},
    {"height", required_argument, NULL, 5},
    {"scale", required_argument, NULL, 6},
    {"origin_x", required_argument, NULL, 7},
    {"origin_y", required_argument, NULL, 8},
    {"line_width", required_argument, NULL, 9},
    {"line_cap", required_argument, NULL, 10},
    {0,0,0,0}
  };
  const char* filepath = NULL;
  int c;
  int x_unset = 1, y_unset = 1;
  int tmp;
  while((c = getopt_long(argc,argv,"ho:i:w:H:s:x:y:l:c:",opts,NULL)) != -1) {
    switch(c) {
    case 'h':
    case 1:
      return NULL;
    case 'o':
    case 2:
      filepath = optarg;
      break;
    case 'i':
    case 3:
      *infile = optarg;
      break;
    case 'w':
    case 4:
      //width
      tmp = atoi(optarg);
      if(tmp <= 0) {
        fprintf(stderr, "Invalid width: %s\n", optarg);
        return NULL;
      }
      conf->width = (unsigned) tmp;
      break;
    case 'H':
    case 5:
      //height
      tmp = atoi(optarg);
      if(tmp <= 0) {
        fprintf(stderr, "Invalid height: %s\n", optarg);
        return NULL;
      }
      conf->height = tmp;
      break;
    case 's':
    case 6:
      conf->scale = atof(optarg);
      if(conf->scale <= 0) {
        fprintf(stderr, "Invalid scale: %s\n", optarg);
        return NULL;
      }
      break;
    case 'x':
    case 7:
      conf->origin_x = atof(optarg);
      x_unset = 0;
      break;
    case 'y':
    case 8:
      conf->origin_y = atof(optarg);
      y_unset = 0;
      break;
    case 'l':
    case 9:
      conf->line_width = atof(optarg);
      if(conf->line_width <= 0) {
        fprintf(stderr, "Invalid line_width: %s\n", optarg);
        return NULL;
      }
      break;
    case 'c':
    case 10:
      if(!(strncmp("normal", optarg, 6))) {
        conf->line_cap = DRAW_DEV_CAP_BUTT;
      } else if(!(strncmp("round", optarg, 5))) {
        conf->line_cap = DRAW_DEV_CAP_ROUND;
      } else if(!(strncmp("square", optarg, 5))) {
        conf->line_cap = DRAW_DEV_CAP_SQUARE;
      } else {
        fprintf(stderr, "Invalid line_cap: %s "
                "(possible values: normal round square)\n",
                optarg);
        return NULL;
      }
      break;
    }
  }
  if(x_unset) {
    conf->origin_x = conf->width/2.0;
  }
  if(y_unset) {
    conf->origin_y = conf->height/2.0;
  }
  return filepath;
}

int init_global_state(struct global_state *gs)
{
  if(!(gs->stack = malloc(sizeof(struct state) * STACK_ALLOC))) {
    return 1;
  }
  gs->current.x = 0;
  gs->current.y = 0;
  gs->current.angle = 0;
  gs->stack_n = 0;
  gs->stack_max = STACK_ALLOC;
  gs->d_count = 0;
  gs->repeat_count = 1;
  return 0;
}

void release_global_state(struct global_state *gs)
{
  free(gs->stack);
}

int push_state(struct global_state *gs)
{
  if(gs->stack_n == gs->stack_max) {
    struct state *tmp = realloc(gs->stack, sizeof(*tmp)*gs->stack_max*2);
    if(tmp) {
      gs->stack = tmp;
      gs->stack_max *= 2;
    } else {
      fputs("Memory allocation error while expanding stack\n", stderr);
      return 1;
    }
  }
  gs->stack[gs->stack_n++] = gs->current;
  return 0;
}

int pop_state(struct global_state *gs)
{
  if(gs->stack_n > 0) {
    gs->current = gs->stack[--gs->stack_n];
    return 0;
  }
  fputs("Stack error", stderr);
  return 1;
}

void update_state(struct state *s, double dist)
{
  static double trig15 [] = {0, 0.25882, 0.5, 0.70711, 0.86603, 0.96593, 1};
  if(s->angle < 7) {
    s->x -= dist * trig15[s->angle];
    s->y -= dist * trig15[6-s->angle];
  } else if (s->angle < 13) {
    s->x -= dist * trig15[12-s->angle];
    s->y += dist * trig15[s->angle-6];
  } else if (s->angle < 19) {
    s->x += dist * trig15[s->angle-12];
    s->y += dist * trig15[18-s->angle];
  } else {
    s->x += dist * trig15[24-s->angle];
    s->y -= dist * trig15[s->angle-18];
  }
}

void update_state_draw(struct global_state *gs, struct draw_dev *dr)
{
  if(gs->d_count > 0) {
    double sx = gs->current.x, sy = gs->current.y;
    update_state(&gs->current, gs->d_count);
    draw_line(dr, sx, sy, gs->current.x, gs->current.y);
    gs->d_count = 0;
  }
}

int do_char(int ch, struct global_state *gs, struct draw_dev *dr)
{
  if(isdigit(ch)) {
    gs->repeat_count *= ch - '0';
    return 0;
  }
  struct instruction* ptr = instructions;
  while(ptr->ch) {
    if(ch == ptr->ch) {
      if(ptr->operation(gs, dr)) {
        return 1;
      }
      gs->repeat_count = 1;
      return 0;
    }
    ++ptr;
  }
  return 0;
}

int op_line(struct global_state* gs, struct draw_dev* dr)
{
  gs->d_count += gs->repeat_count;
  return 0;
}

int op_move(struct global_state* gs, struct draw_dev* dr)
{
  update_state_draw(gs, dr);
  update_state(&gs->current, gs->repeat_count);
  return 0;
}

int op_reset(struct global_state* gs, struct draw_dev* dr)
{
  update_state_draw(gs, dr);
  gs->current.angle = 0;
  gs->current.x = 0;
  gs->current.y = 0;
  return 0;
}

int op_move_to_origin(struct global_state* gs, struct draw_dev* dr)
{
  update_state_draw(gs, dr);
  gs->current.x = 0;
  gs->current.y = 0;
  return 0;
}

int op_push_stack(struct global_state* gs, struct draw_dev* dr)
{
  update_state_draw(gs, dr);
  while(gs->repeat_count--) {
    if(push_state(gs)) {
      return 1;
    }
  }
  return 0;
}

int op_pop_stack(struct global_state* gs, struct draw_dev* dr)
{
  update_state_draw(gs, dr);
  while(gs->repeat_count--) {
    if(pop_state(gs)) {
      return 1;
    }
  }
  return 0;
}

int op_15deg_counterclockwise(struct global_state* gs, struct draw_dev* dr)
{
  update_state_draw(gs, dr);
  gs->current.angle = (gs->current.angle + gs->repeat_count) % 24;
  return 0;
}

int op_15deg_clockwise(struct global_state* gs, struct draw_dev* dr)
{
  update_state_draw(gs, dr);
  if(gs->current.angle >= gs->repeat_count) {
    gs->current.angle -= gs->repeat_count;
  } else {
    gs->current.angle = 24 - (gs->repeat_count - gs->current.angle);
  }
  return 0;
}
