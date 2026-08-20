#include "mahjongg_game.h"
#include "mahjongg_gnome_layouts.h"

struct mj_move {
    int a;
    int b;
};

struct mj_game_state {
    struct mj_tile tiles[MJ_MAX_TILES];

    int grid[MJ_TILE_ROWS][MJ_TILE_COLS][MJ_TILE_LEVS];

    int tile_count;
    int remaining;
    int moves;
    unsigned int seed;

    int cursor_tile;
    int selected_tile;

    struct mj_move undo_stack[MJ_MAX_TILES / 2];
    int undo_count;
};

static int mj_current_layout_id = 0;

static struct mj_game_state game;

static unsigned int mj_rand_state;

static void mj_srand(unsigned int seed)
{
    if (seed == 0) {
        seed = 1;
    }

    mj_rand_state = seed;
}

static unsigned int mj_rand(void)
{
    mj_rand_state = mj_rand_state * 1103515245u + 12345u;
    return (mj_rand_state >> 16) & 0x7fffu;
}

/* --------------------*----------------------------------*---------------- */
/* Utility    *                                  *                           */
/* -*----------------------------------*----------------------------------* */

static int mj_tile_picture_from_number(int number)
{
    if (number < 0) {
        return 0;
    }

    if (number < 136) {
        return number / 4;
    }

    if (number < 140) {
        return 34 + (number - 136);
    }

    if (number < 144) {
        return 38 + (number - 140);
    }

    return 0;
}

static int mj_tile_match_from_number(int number)
{
    if (number < 0) {
        return 0;
    }

    if (number < 136) {
        return number / 4;
    }

    if (number < 140) {
        return 34;
    }

    if (number < 144) {
        return 35;
    }

    return 0;
}

static void clear_grid(void)
{
    int r;
    int c;
    int l;

    for (r = 0; r < MJ_TILE_ROWS; r++) {
        for (c = 0; c < MJ_TILE_COLS; c++) {
            for (l = 0; l < MJ_TILE_LEVS; l++) {
                game.grid[r][c][l] = -1;
            }
        }
    }
}

static bool grid_in_bounds(int r, int c, int l)
{
    return r >= 0 && r < MJ_TILE_ROWS &&
           c >= 0 && c < MJ_TILE_COLS &&
           l >= 0 && l < MJ_TILE_LEVS;
}

static int grid_tile(int r, int c, int l)
{
    if (!grid_in_bounds(r, c, l)) {
        return -1;
    }

    return game.grid[r][c][l];
}

static bool cell_has_real_tile(int r, int c, int l)
{
    int idx = grid_tile(r, c, l);

    if (idx < 0) {
        return false;
    }

    return game.tiles[idx].real && !game.tiles[idx].removed;
}

bool mj_tile_open(const struct mj_tile *tile)
{
    if (tile == NULL) {
        return false;
    }

    return tile->real &&
           !tile->removed &&
           tile->blocked == 0 &&
           tile->coverage == 0;
}

bool mj_game_tile_open(int index)
{
    const struct mj_tile *tile;
    bool left_blocked = false;
    bool right_blocked = false;
    int x;
    int y_above;
    int y_bottom_half;
    int layer;
    int i;

    if (index < 0 || index >= game.tile_count) {
        return false;
    }

    tile = &game.tiles[index];

    if (!tile->real || tile->removed) {
        return false;
    }

    x = tile->col;
    y_above = tile->row - 1;
    y_bottom_half = tile->row + 1;
    layer = tile->lev;

    for (i = 0; i < game.tile_count; i++) {
        const struct mj_tile *other;
        int other_x;
        int other_y;
        int other_layer;

        if (i == index) {
            continue;
        }

        other = &game.tiles[i];

        if (!other->real || other->removed) {
            continue;
        }

        other_y = other->row;

        /*
         * Exact GNOME Mahjongg geometry:
         * only tiles whose vertical footprint overlaps this tile can
         * block its sides or cover its top face.
         */
        if (other_y < y_above ||
            other_y > y_bottom_half) {
            continue;
        }

        other_layer = other->lev;
        other_x = other->col;

        /*
         * A tile on the same layer blocks the left or right side when
         * its logical x coordinate differs by exactly two half-tile
         * units.
         */
        if (other_layer == layer) {
            if (other_x == x - 2) {
                left_blocked = true;
            }

            if (other_x == x + 2) {
                right_blocked = true;
            }
        } else if (other_layer > layer) {
            /*
             * A tile on any higher layer covers this tile when its x
             * coordinate lies within one half-tile unit of this tile.
             */
            if (other_x >= x - 1 &&
                other_x <= x + 1) {
                return false;
            }
        }
    }

    if (left_blocked && right_blocked) {
        return false;
    }

    return true;
}




/* ------------------------------------------------------------------------ */
/* Grid and blockage, based on xmahjongg Game::init_grid/init_blockage        */
/* ------------------------------------------------------------------------ */

static void build_grid(void)
{
    int i;
    int rr;
    int cc;

    clear_grid();

    for (i = 0; i < game.tile_count; i++) {
        struct mj_tile *tile = &game.tiles[i];

        if (!tile->real || tile->removed) {
            continue;
        }

        /*
         * xmahjongg represents each tile as occupying a 2x2 area in the
         * logical grid. This is important for half-step layouts.
         */
        for (rr = tile->row; rr < tile->row + 2; rr++) {
            for (cc = tile->col; cc < tile->col + 2; cc++) {
                if (grid_in_bounds(rr, cc, tile->lev)) {
                    game.grid[rr][cc][tile->lev] = i;
                }
            }
        }
    }
}

static void check_level_blockage(int r, int c, int l)
{
    int idx;
    bool left_blocked;
    bool right_blocked;
    struct mj_tile *tile;

    idx = grid_tile(r, c, l);

    if (idx < 0) {
        return;
    }

    tile = &game.tiles[idx];

    if (!tile->real || tile->removed) {
        return;
    }

    /*
     * xmahjongg normalizes to the tile's origin.
     */
    r = tile->row;
    c = tile->col;

    left_blocked =
        cell_has_real_tile(r,     c - 1, l) ||
        cell_has_real_tile(r + 1, c - 1, l);

    right_blocked =
        cell_has_real_tile(r,     c + 2, l) ||
        cell_has_real_tile(r + 1, c + 2, l);

    tile->blocked = left_blocked && right_blocked;
}

static void init_blockage(void)
{
    int i;
    int r;
    int c;

    build_grid();

    for (i = 0; i < game.tile_count; i++) {
        game.tiles[i].coverage = 0;
        game.tiles[i].blocked = 0;
        game.tiles[i].mark = 0;
    }

    /*
     * Coverage from tiles above:
     * xmahjongg increments coverage on the directly lower level.
     */
    for (i = 0; i < game.tile_count; i++) {
        struct mj_tile *tile = &game.tiles[i];

        if (!tile->real || tile->removed) {
            continue;
        }

        if (tile->lev > 0) {
            for (r = tile->row; r < tile->row + 2; r++) {
                for (c = tile->col; c < tile->col + 2; c++) {
                    int below = grid_tile(r, c, tile->lev - 1);

                    if (below >= 0) {
                        game.tiles[below].coverage++;
                    }
                }
            }
        }
    }

    /*
     * Side blockage on same level.
     */
    for (i = 0; i < game.tile_count; i++) {
        struct mj_tile *tile = &game.tiles[i];

        if (!tile->real || tile->removed) {
            continue;
        }

        check_level_blockage(tile->row, tile->col, tile->lev);
    }
}

/* ------------------------------------------------------------------------ */
/* Layout                                                                    */
/* ------------------------------------------------------------------------ */

static bool place_tile(int row, int col, int lev)
{
    struct mj_tile *tile;

    if (game.tile_count >= MJ_MAX_TILES) {
        return false;
    }

    if (!(row > 1 && col > 1 &&
          row < MJ_TILE_ROWS - 3 &&
          col < MJ_TILE_COLS - 3 &&
          lev >= 0 && lev < MJ_TILE_LEVS - 1)) {
        return false;
    }

    tile = &game.tiles[game.tile_count];

    tile->real = true;
    tile->removed = false;
    tile->number = game.tile_count;
    tile->picture = 0;

    /*
     * Phase 1:
     * Keep original xmahjongg concept of 36 matching groups.
     * This is not yet the original image/picture mapping, but every group has
     * four matching tiles, which is correct for Mahjongg Solitaire.
     */
    //tile->match = 0;
    tile->match = 0;

    tile->row = row;
    tile->col = col;
    tile->lev = lev;

    tile->coverage = 0;
    tile->blocked = 0;
    tile->mark = 0;

    game.tile_count++;
    game.remaining++;

    return true;
}

int mj_game_layout_count(void)
{
    return MJ_GNOME_LAYOUT_COUNT + 1;
}

int mj_game_layout_id(void)
{
    return mj_current_layout_id;
}

const char *mj_game_layout_name(int layout_id)
{
    if (layout_id == 0) {
        return "Rockbox Compact 80";
    }

    if (layout_id < 1 ||
        layout_id > MJ_GNOME_LAYOUT_COUNT) {
        return "";
    }

    return mj_gnome_layouts[layout_id - 1].name;
}

const char *mj_game_layout_score_name(int layout_id)
{
    if (layout_id == 0) {
        return "rockbox_compact_80";
    }

    if (layout_id < 1 ||
        layout_id > MJ_GNOME_LAYOUT_COUNT) {
        return "";
    }

    return mj_gnome_layouts[layout_id - 1].score_name;
}

int mj_game_layout_tile_count(int layout_id)
{
    if (layout_id == 0) {
        return 80;
    }

    if (layout_id < 1 ||
        layout_id > MJ_GNOME_LAYOUT_COUNT) {
        return 0;
    }

    return mj_gnome_layouts[layout_id - 1].slot_count;
}

bool mj_game_set_layout(int layout_id)
{
    if (layout_id < 0 ||
        layout_id > MJ_GNOME_LAYOUT_COUNT) {
        return false;
    }

    mj_current_layout_id = layout_id;
    return true;
}

static bool layout_gnome(int layout_id)
{
    const struct mj_gnome_layout *layout;
    int i;

    if (layout_id < 0 ||
        layout_id >= MJ_GNOME_LAYOUT_COUNT) {
        layout_id = 0;
    }

    layout = &mj_gnome_layouts[layout_id];

    game.tile_count = 0;
    game.remaining = 0;

    for (i = 0; i < layout->slot_count; i++) {
        const struct mj_gnome_slot *slot;

        slot = &layout->slots[i];

        /*
         * GNOME stores x, y, layer.
         * Rockbox place_tile() expects row, column, level.
         */
        /*
         * GNOME map coordinates may begin at x=0 and y=0.
         * The Rockbox/xmahjongg grid reserves a two-unit border around
         * the board, so translate the complete GNOME map by +2/+2.
         *
         * This is one global translation and does not change the layout's
         * shape, spacing, blocking relationships or layer structure.
         */
        if (!place_tile(
                slot->y + 2,
                slot->x + 2,
                slot->layer)) {
            game.tile_count = 0;
            game.remaining = 0;
            return false;
        }
    }

    return game.tile_count == layout->slot_count;
}


static void layout_default(void)
{
    int i;
    int j;

    /*
     * This is a direct C port of xmahjongg Game::layout_default().
     */

    for (j = 2; j <= 13; j++) {
        place_tile(2,  j * 2, 0);
        place_tile(8,  j * 2, 0);
        place_tile(10, j * 2, 0);
        place_tile(16, j * 2, 0);
    }

    for (j = 3; j <= 12; j++) {
        place_tile(6,  j * 2, 0);
        place_tile(12, j * 2, 0);
    }

    for (j = 4; j <= 11; j++) {
        place_tile(4,  j * 2, 0);
        place_tile(14, j * 2, 0);
    }

    for (j = 5; j <= 10; j++) {
        for (i = 2; i <= 7; i++) {
            place_tile(i * 2, j * 2, 1);
        }
    }

    for (j = 6; j <= 9; j++) {
        for (i = 3; i <= 6; i++) {
            place_tile(i * 2, j * 2, 2);
        }
    }

    for (j = 7; j <= 8; j++) {
        for (i = 4; i <= 5; i++) {
            place_tile(i * 2, j * 2, 3);
        }
    }

    place_tile(9, 2, 0);
    place_tile(9, 28, 0);
    place_tile(9, 30, 0);
    place_tile(9, 15, 4);
}

static void layout_ipod_easy(void)
{
    int c;

    for (c = 6; c <= 18; c += 2) {
        place_tile(4, c, 0);
    }

    for (c = 4; c <= 20; c += 2) {
        place_tile(6, c, 0);
        place_tile(8, c, 0);
        place_tile(10, c, 0);
    }

    for (c = 6; c <= 18; c += 2) {
        place_tile(12, c, 0);
    }

    for (c = 8; c <= 16; c += 2) {
        place_tile(6, c, 1);
        place_tile(8, c, 1);
        place_tile(10, c, 1);
    }

    for (c = 10; c <= 14; c += 2) {
        place_tile(12, c, 1);
    }

    for (c = 10; c <= 14; c += 2) {
        place_tile(8, c, 2);
        place_tile(10, c, 2);
    }

    place_tile(6, 12, 2);

    place_tile(8, 11, 3);
    place_tile(8, 13, 3);
    place_tile(8, 15, 3);

    place_tile(10, 12, 3);
    place_tile(10, 14, 3);

    place_tile(9, 13, 4);

    /*
     * Eight additional slots for the compact 80-tile adaptation.
     * The additions remain inside the existing board footprint, so the
     * layout stays suitable for the iPod display.
     */
    place_tile(6, 10, 2);
    place_tile(6, 14, 2);

    place_tile(12, 10, 2);
    place_tile(12, 12, 2);
    place_tile(12, 14, 2);

    place_tile(10, 10, 3);

    place_tile(6, 11, 3);
    place_tile(6, 13, 3);
}

static bool layout_selected(void)
{
    if (mj_current_layout_id == 0) {
        layout_ipod_easy();
        return game.tile_count == 80;
    }

    return layout_gnome(mj_current_layout_id - 1);
}


static void assign_random_matches(unsigned int seed)
{
    int i;
    int j;
    int tmp;
    int tile_numbers[MJ_MAX_TILES];

    for (i = 0; i < MJ_MAX_TILES; i++) {
        tile_numbers[i] = i;
    }

    mj_srand(seed);

    for (i = MJ_MAX_TILES - 1; i > 0; i--) {
        j = mj_rand() % (i + 1);

        tmp = tile_numbers[i];
        tile_numbers[i] = tile_numbers[j];
        tile_numbers[j] = tmp;
    }

    for (i = 0; i < game.tile_count && i < MJ_MAX_TILES; i++) {
        game.tiles[i].number = tile_numbers[i];
        game.tiles[i].picture = mj_tile_picture_from_number(tile_numbers[i]);
        game.tiles[i].match = mj_tile_match_from_number(tile_numbers[i]);
    }
}

static int collect_open_tiles(int *open_tiles, int max_tiles)
{
    int i;
    int count = 0;

    for (i = 0; i < game.tile_count && count < max_tiles; i++) {
        if (mj_game_tile_open(i)) {
            open_tiles[count] = i;
            count++;
        }
    }

    return count;
}

static bool choose_random_open_pair(int *a, int *b)
{
    int open_tiles[MJ_MAX_TILES];
    int count;
    int first;
    int second;

    count = collect_open_tiles(open_tiles, MJ_MAX_TILES);

    if (count < 2) {
        return false;
    }

    first = mj_rand() % count;

    second = mj_rand() % (count - 1);
    if (second >= first) {
        second++;
    }

    *a = open_tiles[first];
    *b = open_tiles[second];

    return true;
}

static bool assign_solvable_matches(unsigned int seed)
{
    int i;
    int j;
    int tmp;
    int npairs;
    int a;
    int b;

    int pair_a[MJ_MAX_TILES / 2];
    int pair_b[MJ_MAX_TILES / 2];
    int pair_match[MJ_MAX_TILES / 2];

    if (game.tile_count <= 0 || game.tile_count > MJ_MAX_TILES || (game.tile_count & 1)) {
        return false;
    }

    mj_srand(seed);

    for (i = 0; i < game.tile_count; i++) {
        game.tiles[i].removed = false;
    }

    game.remaining = game.tile_count;
    init_blockage();

    npairs = game.tile_count / 2;

    for (i = 0; i < npairs; i++) {
        if (!choose_random_open_pair(&a, &b)) {
            for (j = 0; j < game.tile_count; j++) {
                game.tiles[j].removed = false;
            }

            game.remaining = game.tile_count;
            init_blockage();

            return false;
        }

        pair_a[i] = a;
        pair_b[i] = b;

        game.tiles[a].removed = true;
        game.tiles[b].removed = true;
        game.remaining -= 2;

        init_blockage();
    }

    for (i = 0; i < npairs; i++) {
        pair_match[i] = i;
    }

    for (i = npairs - 1; i > 0; i--) {
        j = mj_rand() % (i + 1);

        tmp = pair_match[i];
        pair_match[i] = pair_match[j];
        pair_match[j] = tmp;
    }

    for (i = 0; i < npairs; i++) {
        int tile_number_a;
        int tile_number_b;

        tile_number_a = pair_match[i] * 2;
        tile_number_b = tile_number_a + 1;

        game.tiles[pair_a[i]].number = tile_number_a;
        game.tiles[pair_a[i]].picture = mj_tile_picture_from_number(tile_number_a);
        game.tiles[pair_a[i]].match = mj_tile_match_from_number(tile_number_a);

        game.tiles[pair_b[i]].number = tile_number_b;
        game.tiles[pair_b[i]].picture = mj_tile_picture_from_number(tile_number_b);
        game.tiles[pair_b[i]].match = mj_tile_match_from_number(tile_number_b);
    }

    for (i = 0; i < game.tile_count; i++) {
        game.tiles[i].removed = false;
    }

    game.remaining = game.tile_count;
    game.moves = 0;
    game.undo_count = 0;
    game.cursor_tile = -1;
    game.selected_tile = -1;

    init_blockage();

    return true;
}


/* ------------------------------------------------------------------------ */
/* Cursor                                                                    */
/* ------------------------------------------------------------------------ */

static bool tile_allowed_for_cursor_filter(int index)
{
    int selected;

    if (!mj_game_tile_open(index)) {
        return false;
    }

    selected = game.selected_tile;

    if (selected >= 0 &&
        selected < game.tile_count &&
        !game.tiles[selected].removed) {
        if (index == selected) {
            return false;
        }

        if (game.tiles[index].match != game.tiles[selected].match) {
            return false;
        }
    }

    return true;
}

static int find_next_open_tile(int start, int direction)
{
    int i;
    int index;

    if (game.tile_count <= 0) {
        return -1;
    }

    if (direction < 0) {
        direction = -1;
    } else {
        direction = 1;
    }

    index = start;

    for (i = 0; i < game.tile_count; i++) {
        index += direction;

        if (index >= game.tile_count) {
            index = 0;
        } else if (index < 0) {
            index = game.tile_count - 1;
        }

        if (tile_allowed_for_cursor_filter(index)) {
            return index;
        }
    }

    return -1;
}

void mj_game_cursor_next(void)
{
    int next = find_next_open_tile(game.cursor_tile, 1);

    if (next >= 0) {
        game.cursor_tile = next;
    }
}

void mj_game_cursor_prev(void)
{
    int prev = find_next_open_tile(game.cursor_tile, -1);

    if (prev >= 0) {
        game.cursor_tile = prev;
    }
}

/* ------------------------------------------------------------------------ */
/* Game actions                                                              */
/* ------------------------------------------------------------------------ */

static bool tiles_match(int a, int b)
{
    if (a < 0 || b < 0 ||
        a >= game.tile_count || b >= game.tile_count ||
        a == b) {
        return false;
    }

    if (!mj_game_tile_open(a) || !mj_game_tile_open(b)) {
        return false;
    }

    return game.tiles[a].match == game.tiles[b].match;
}

static void deselect(void)
{
    game.selected_tile = -1;
}

static void remove_pair(int a, int b)
{
    if (game.undo_count < (MJ_MAX_TILES / 2)) {
        game.undo_stack[game.undo_count].a = a;
        game.undo_stack[game.undo_count].b = b;
        game.undo_count++;
    }

    game.tiles[a].removed = true;
    game.tiles[b].removed = true;

    game.remaining -= 2;
    game.moves++;

    deselect();

    init_blockage();

    if (!mj_game_tile_open(game.cursor_tile)) {
        game.cursor_tile = find_next_open_tile(game.cursor_tile, 1);
    }
}

enum mj_select_result mj_game_select_current(void)
{
    int cur = game.cursor_tile;

    if (cur < 0 || cur >= game.tile_count) {
        return MJ_SELECT_NONE;
    }

    if (!mj_game_tile_open(cur)) {
        return MJ_SELECT_NONE;
    }

    if (game.selected_tile < 0) {
        game.selected_tile = cur;
        return MJ_SELECT_CHANGED;
    }

    if (game.selected_tile == cur) {
        deselect();
        return MJ_SELECT_CHANGED;
    }

    if (tiles_match(game.selected_tile, cur)) {
        remove_pair(game.selected_tile, cur);
        return MJ_SELECT_REMOVED_PAIR;
    }

    game.selected_tile = cur;
    return MJ_SELECT_CHANGED;
}

bool mj_game_undo(void)
{
    struct mj_move move;

    if (game.undo_count <= 0) {
        return false;
    }

    game.undo_count--;

    move = game.undo_stack[game.undo_count];

    if (move.a < 0 || move.b < 0 ||
        move.a >= game.tile_count || move.b >= game.tile_count) {
        return false;
    }

    game.tiles[move.a].removed = false;
    game.tiles[move.b].removed = false;

    game.remaining += 2;

    if (game.moves > 0) {
        game.moves--;
    }

    deselect();

    init_blockage();

    game.cursor_tile = move.a;

    if (!mj_game_tile_open(game.cursor_tile)) {
        game.cursor_tile = find_next_open_tile(-1, 1);
    }

    return true;
}

bool mj_game_find_hint(int *a, int *b)
{
    int i;
    int j;

    for (i = 0; i < game.tile_count; i++) {
        if (!mj_game_tile_open(i)) {
            continue;
        }

        for (j = i + 1; j < game.tile_count; j++) {
            if (!mj_game_tile_open(j)) {
                continue;
            }

            if (game.tiles[i].match == game.tiles[j].match) {
                if (a != NULL) {
                    *a = i;
                }

                if (b != NULL) {
                    *b = j;
                }

                return true;
            }
        }
    }

    return false;
}

int mj_game_possible_moves(void)
{
    int count = 0;
    int i;
    int j;

    for (i = 0; i < game.tile_count; i++) {
        if (!mj_game_tile_open(i)) {
            continue;
        }

        for (j = i + 1; j < game.tile_count; j++) {
            if (!mj_game_tile_open(j)) {
                continue;
            }

            if (game.tiles[i].match == game.tiles[j].match) {
                count++;
            }
        }
    }

    return count;
}


#define MJ_SAVE_MAGIC 0x4d4a4731u
#define MJ_SAVE_VERSION 2u

struct mj_save_header {
    unsigned int magic;
    unsigned int version;
    unsigned int seed;
    int tile_count;
    int moves;
    int cursor_tile;
    int selected_tile;
    int undo_count;
};

struct mj_save_tile {
    unsigned char removed;
};

struct mj_save_move {
    short a;
    short b;
};

static bool mj_write_all(int fd, const void *buf, size_t size)
{
    return rb->write(fd, buf, size) == (ssize_t)size;
}

static bool mj_read_all(int fd, void *buf, size_t size)
{
    return rb->read(fd, buf, size) == (ssize_t)size;
}

bool mj_game_save(int fd)
{
    int i;
    struct mj_save_header header;

    header.magic = MJ_SAVE_MAGIC;
    header.version = MJ_SAVE_VERSION;
    header.seed = game.seed;
    header.tile_count = game.tile_count;
    header.moves = game.moves;
    header.cursor_tile = game.cursor_tile;
    header.selected_tile = game.selected_tile;
    header.undo_count = game.undo_count;

    if (!mj_write_all(fd, &header, sizeof(header))) {
        return false;
    }

    for (i = 0; i < game.tile_count; i++) {
        struct mj_save_tile tile;

        tile.removed = game.tiles[i].removed ? 1 : 0;

        if (!mj_write_all(fd, &tile, sizeof(tile))) {
            return false;
        }
    }

    for (i = 0; i < game.undo_count; i++) {
        struct mj_save_move move;

        move.a = game.undo_stack[i].a;
        move.b = game.undo_stack[i].b;

        if (!mj_write_all(fd, &move, sizeof(move))) {
            return false;
        }
    }

    return true;
}

bool mj_game_load(int fd)
{
    int i;
    int remaining;
    struct mj_save_header header;
    struct mj_save_tile saved_tiles[MJ_MAX_TILES];
    struct mj_save_move saved_moves[MJ_MAX_TILES / 2];

    if (!mj_read_all(fd, &header, sizeof(header))) {
        return false;
    }

    if (header.magic != MJ_SAVE_MAGIC ||
        header.version != MJ_SAVE_VERSION ||
        header.tile_count <= 0 ||
        header.tile_count > MJ_MAX_TILES ||
        header.undo_count < 0 ||
        header.undo_count > MJ_MAX_TILES / 2) {
        return false;
    }

    for (i = 0; i < header.tile_count; i++) {
        if (!mj_read_all(fd, &saved_tiles[i], sizeof(saved_tiles[i]))) {
            return false;
        }
    }

    for (i = 0; i < header.undo_count; i++) {
        if (!mj_read_all(fd, &saved_moves[i], sizeof(saved_moves[i]))) {
            return false;
        }
    }

    mj_game_init_default(header.seed);

    if (header.tile_count != game.tile_count) {
        return false;
    }

    remaining = 0;

    for (i = 0; i < game.tile_count; i++) {
        game.tiles[i].removed = saved_tiles[i].removed ? true : false;

        if (!game.tiles[i].removed) {
            remaining++;
        }
    }

    game.remaining = remaining;
    game.moves = header.moves;
    game.cursor_tile = header.cursor_tile;
    game.selected_tile = header.selected_tile;
    game.undo_count = header.undo_count;

    for (i = 0; i < game.undo_count; i++) {
        game.undo_stack[i].a = saved_moves[i].a;
        game.undo_stack[i].b = saved_moves[i].b;
    }

    if (game.cursor_tile < 0 || game.cursor_tile >= game.tile_count) {
        game.cursor_tile = -1;
    }

    if (game.selected_tile < 0 ||
        game.selected_tile >= game.tile_count ||
        game.tiles[game.selected_tile].removed) {
        game.selected_tile = -1;
    }

    init_blockage();

    if (!mj_game_tile_open(game.cursor_tile)) {
        game.cursor_tile = find_next_open_tile(-1, 1);
    }

    return true;
}


/* ------------------------------------------------------------------------ */
/* Public accessors                                                          */
/* ------------------------------------------------------------------------ */

void mj_game_init_default(unsigned int seed)
{
    game.tile_count = 0;
    game.remaining = 0;
    game.moves = 0;
    game.seed = seed;
    game.cursor_tile = -1;
    game.selected_tile = -1;
    game.undo_count = 0;

    /*
     * Use the compact 80-tile Rockbox layout by default.
     * Imported GNOME 80 layouts remain available for the future
     * layout-selection menu.
     */
    if (!layout_selected()) {
        game.tile_count = 0;
        game.remaining = 0;
        layout_ipod_easy();
        mj_current_layout_id = 0;
    }

    if (!assign_solvable_matches(seed)) {
        assign_random_matches(seed);
    }

    init_blockage();

    game.cursor_tile = find_next_open_tile(-1, 1);
}

int mj_game_tile_count(void)
{
    return game.tile_count;
}

const struct mj_tile *mj_game_tile(int index)
{
    if (index < 0 || index >= game.tile_count) {
        return NULL;
    }

    return &game.tiles[index];
}

int mj_game_remaining(void)
{
    return game.remaining;
}

int mj_game_moves(void)
{
    return game.moves;
}

unsigned int mj_game_seed(void)
{
    return game.seed;
}

int mj_game_cursor_tile(void)
{
    return game.cursor_tile;
}

int mj_game_selected_tile(void)
{
    return game.selected_tile;
}