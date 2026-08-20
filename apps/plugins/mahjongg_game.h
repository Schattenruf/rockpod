#ifndef MAHJONGG_GAME_H
#define MAHJONGG_GAME_H

#include "plugin.h"

/*
 * Mahjongg game logic adapted from xmahjongg 3.7 by Eddie Kohler and others.
 * Rockbox plugin adaptation.
 *
 * xmahjongg uses:
 *   TILE_ROWS 24
 *   TILE_COLS 38
 *   TILE_LEVS 12
 */

#define MJ_TILE_ROWS 24
#define MJ_TILE_COLS 38
#define MJ_TILE_LEVS 12

#define MJ_MAX_TILES 144
#define MJ_NMATCHES 36

struct mj_tile {
    bool real;
    bool removed;

    short number;
    short picture;
    short match;

    short row;
    short col;
    short lev;

    short coverage;
    short blocked;

    short mark;
};

enum mj_select_result {
    MJ_SELECT_NONE = 0,
    MJ_SELECT_CHANGED,
    MJ_SELECT_REMOVED_PAIR
};

void mj_game_init_default(unsigned int seed);

int mj_game_tile_count(void);
const struct mj_tile *mj_game_tile(int index);

int mj_game_remaining(void);
int mj_game_moves(void);
unsigned int mj_game_seed(void);
bool mj_game_save(int fd);
bool mj_game_load(int fd);

int mj_game_cursor_tile(void);
int mj_game_selected_tile(void);

bool mj_tile_open(const struct mj_tile *tile);
bool mj_game_tile_open(int index);

void mj_game_cursor_next(void);
void mj_game_cursor_prev(void);

enum mj_select_result mj_game_select_current(void);

bool mj_game_undo(void);

bool mj_game_find_hint(int *a, int *b);

int mj_game_possible_moves(void);

#endif