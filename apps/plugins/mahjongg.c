/***************************************************************************
 * Rockbox Mahjongg Plugin - early skeleton
 *
 * Based structurally on Rockbox plugins such as Sokoban and Solitaire.
 * Game logic will later be filled from xmahjongg:
 *   - tile.c / tile.h
 *   - game.c / game.h
 *   - solvable.c / solvable.h
 *
 ****************************************************************************/

#include "plugin.h"
#include "mahjongg_game.h"
#include "mahjongg_game.c"
#include "pluginbitmaps/mahjongg_tiles.h"
/* #include "lib/playback_control.h" */

/* ------------------------------------------------------------------------ */
/* Basic metadata                                                            */
/* ------------------------------------------------------------------------ */

#define MAHJONGG_TITLE "Mahjongg"

#define MAHJONGG_SAVE_FILE PLUGIN_GAMES_DATA_DIR "/mahjongg.save"
#define MAHJONGG_SCORE_FILE PLUGIN_GAMES_DATA_DIR "/mahjongg.score"

/* ------------------------------------------------------------------------ */
/* Display constants                                                         */
/* ------------------------------------------------------------------------ */

/*
 * First test sizes.
 * Later these should be derived from actual bitmap dimensions.
 */
#define MJ_TILE_GAP_PX 1
#define MJ_TILE_W 20
#define MJ_TILE_H 27
#define MJ_PATTERN_W (MJ_TILE_W + MJ_LEVEL_DX)
#define MJ_PATTERN_H (MJ_TILE_H - MJ_LEVEL_DY)
#define MJ_TILE_XSTEP (MJ_TILE_W / 2)
#define MJ_TILE_YSTEP (MJ_TILE_H / 2)
#define MJ_LEVEL_DX (MJ_TILE_W / 7)
#define MJ_LEVEL_DY -(MJ_TILE_H / 10)

#define MJ_MAX_TILES   144
#define MJ_ROWS         24
#define MJ_COLS         38
#define MJ_LEVS         12

#define MJ_FONT FONT_SYSFIXED

#if LCD_DEPTH > 1
#define MJ_TABLE_BG      LCD_RGBPACK(18, 78, 28)
#define MJ_TABLE_DARK    LCD_RGBPACK(12, 55, 22)
#define MJ_TABLE_LIGHT   LCD_RGBPACK(24, 92, 36)
#endif

/* ------------------------------------------------------------------------ */
/* iPod / target keymap                                                      */
/* ------------------------------------------------------------------------ */

#if (CONFIG_KEYPAD == IPOD_4G_PAD) || \
    (CONFIG_KEYPAD == IPOD_3G_PAD) || \
    (CONFIG_KEYPAD == IPOD_1G2G_PAD)

/*
 * iPod-style control proposal:
 *
 * Wheel back/fwd  : previous/next selectable tile
 * Select          : select tile / remove pair
 * Select hold     : menu
 * Menu short      : hint
 * Play            : undo
 * Left/Right      : optional alternate navigation
 */
#define MJ_PREV             BUTTON_SCROLL_BACK
#define MJ_NEXT             BUTTON_SCROLL_FWD

#define MJ_SELECT_PRE       BUTTON_SELECT
#define MJ_SELECT           (BUTTON_SELECT | BUTTON_REL)

#define MJ_MENU             (BUTTON_MENU | BUTTON_REL)

#define MJ_HINT_PRE         BUTTON_LEFT
#define MJ_HINT             (BUTTON_LEFT | BUTTON_REL)

#define MJ_UNDO             BUTTON_PLAY

/*#define MJ_LEFT_PRE         BUTTON_LEFT
#define MJ_LEFT             (BUTTON_LEFT | BUTTON_REL)

#define MJ_RIGHT_PRE        BUTTON_RIGHT
#define MJ_RIGHT            (BUTTON_RIGHT | BUTTON_REL)
*/
#define MJ_KEYS_PREV_NEXT   "SCROLL"
#define MJ_KEYS_SELECT      "SELECT"
#define MJ_KEYS_MENU        "MENU"
#define MJ_KEYS_HINT        "LEFT"
#define MJ_KEYS_UNDO        "PLAY"

#else

/*
 * Generic fallback for non-iPod targets.
 * This is intentionally simple and may need adjustment per target.
 */
#define MJ_PREV             BUTTON_LEFT
#define MJ_NEXT             BUTTON_RIGHT
#define MJ_SELECT           BUTTON_SELECT
#define MJ_MENU             BUTTON_POWER
#define MJ_HINT             BUTTON_UP
#define MJ_UNDO             BUTTON_DOWN

#define MJ_KEYS_PREV_NEXT   "LEFT/RIGHT"
#define MJ_KEYS_SELECT      "SELECT"
#define MJ_KEYS_MENU        "POWER"
#define MJ_KEYS_HINT        "UP"
#define MJ_KEYS_UNDO        "DOWN"

#endif

#if defined(MJ_SELECT_PRE) || defined(MJ_HINT_PRE) || \
    defined(MJ_LEFT_PRE) || defined(MJ_RIGHT_PRE)
#define MJ_NEED_LASTBUTTON
#endif

static int hint_tile_a = -1;
static int hint_tile_b = -1;

static void clear_hint(void)
{
    hint_tile_a = -1;
    hint_tile_b = -1;
}

#define MJ_STOPWATCH_SAVE_MAGIC 0x4d4a5731u
#define MJ_SCORE_MAGIC 0x4d4a5331u

static bool mj_stopwatch_enabled = true;
static bool mj_random_maps_enabled = false;
static bool mj_dead_end_prompted = false;
static long mj_stopwatch_start_tick = 0;
static long mj_best_elapsed_ticks = -1;

struct mj_stopwatch_save_state {
    unsigned int magic;
    unsigned int stopwatch_enabled;
    long elapsed_ticks;
};

struct mj_score_record {
    unsigned int magic;
    int layout_id;
    long best_ticks;
};

static void mj_stopwatch_reset(void)
{
    mj_stopwatch_start_tick = *rb->current_tick;
}

static long mj_stopwatch_elapsed_ticks(void)
{
    if (!mj_stopwatch_enabled) {
        return -1;
    }

    return *rb->current_tick - mj_stopwatch_start_tick;
}

static void mj_format_time(long ticks, char *buffer, int buffer_size)
{
    int total_seconds;
    int minutes;
    int seconds;

    if (ticks < 0) {
        rb->snprintf(buffer, buffer_size, "--:--");
        return;
    }

    total_seconds = ticks / HZ;
    minutes = total_seconds / 60;
    seconds = total_seconds % 60;

    rb->snprintf(buffer, buffer_size, "%d:%02d", minutes, seconds);
}

static bool mj_stopwatch_save(int fd)
{
    struct mj_stopwatch_save_state state;

    state.magic = MJ_STOPWATCH_SAVE_MAGIC;
    state.stopwatch_enabled = mj_stopwatch_enabled ? 1u : 0u;
    state.elapsed_ticks = mj_stopwatch_elapsed_ticks();

    if (state.elapsed_ticks < 0) {
        state.elapsed_ticks = 0;
    }

    return rb->write(fd, &state, sizeof(state)) == (ssize_t)sizeof(state);
}

static void mj_stopwatch_load(int fd)
{
    struct mj_stopwatch_save_state state;
    long elapsed;

    if (rb->read(fd, &state, sizeof(state)) != (ssize_t)sizeof(state)) {
        mj_stopwatch_enabled = true;
        mj_stopwatch_reset();
        return;
    }

    if (state.magic != MJ_STOPWATCH_SAVE_MAGIC) {
        mj_stopwatch_enabled = true;
        mj_stopwatch_reset();
        return;
    }

    mj_stopwatch_enabled = state.stopwatch_enabled != 0;
    elapsed = state.elapsed_ticks;

    if (elapsed < 0) {
        elapsed = 0;
    }

    mj_stopwatch_start_tick = *rb->current_tick - elapsed;
}

#define MJ_SCORE_VERSION 1u
#define MJ_MAX_LAYOUT_RECORDS 72
#define MJ_SCORE_EMPTY 0xffffffffu

struct mj_score_file {
    unsigned int magic;
    unsigned int version;
    unsigned int count;
    unsigned int best_seconds[MJ_MAX_LAYOUT_RECORDS];
};

static void mj_score_file_init(struct mj_score_file *scores)
{
    int i;

    scores->magic = MJ_SCORE_MAGIC;
    scores->version = MJ_SCORE_VERSION;
    scores->count = MJ_MAX_LAYOUT_RECORDS;

    for (i = 0; i < MJ_MAX_LAYOUT_RECORDS; i++) {
        scores->best_seconds[i] = MJ_SCORE_EMPTY;
    }
}

static bool mj_score_file_read(struct mj_score_file *scores)
{
    int fd;
    bool ok;

    mj_score_file_init(scores);

    fd = rb->open(MAHJONGG_SCORE_FILE, O_RDONLY);

    if (fd < 0) {
        return false;
    }

    ok = rb->read(fd, scores, sizeof(*scores)) == (ssize_t)sizeof(*scores);
    rb->close(fd);

    if (!ok ||
        scores->magic != MJ_SCORE_MAGIC ||
        scores->version != MJ_SCORE_VERSION ||
        scores->count != MJ_MAX_LAYOUT_RECORDS) {
        mj_score_file_init(scores);
        return false;
    }

    return true;
}

static bool mj_score_file_write(const struct mj_score_file *scores)
{
    int fd;
    bool ok;

    rb->mkdir(PLUGIN_GAMES_DATA_DIR);

    fd = rb->open(MAHJONGG_SCORE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    if (fd < 0) {
        return false;
    }

    ok = rb->write(fd, scores, sizeof(*scores)) == (ssize_t)sizeof(*scores);
    rb->close(fd);

    return ok;
}

static void mj_score_load_best(void)
{

    /*
     * Clear the previously loaded layout record first. If the selected
     * layout has no record yet, Best must remain --:-- instead of showing
     * the previous layout's time.
     */
    mj_best_elapsed_ticks = -1;

    struct mj_score_file scores;
    unsigned int best_seconds;

    mj_best_elapsed_ticks = -1;

    mj_score_file_read(&scores);

    if (mj_game_layout_id() < 0 ||
        mj_game_layout_id() >= MJ_MAX_LAYOUT_RECORDS) {
        return;
    }

    best_seconds = scores.best_seconds[mj_game_layout_id()];

    if (best_seconds == MJ_SCORE_EMPTY) {
        return;
    }

    mj_best_elapsed_ticks = (long)best_seconds * HZ;
}

static void mj_score_save_best(long elapsed_ticks)
{
    struct mj_score_file scores;
    unsigned int elapsed_seconds;
    unsigned int current_best;

    if (elapsed_ticks < 0) {
        return;
    }

    if (mj_game_layout_id() < 0 ||
        mj_game_layout_id() >= MJ_MAX_LAYOUT_RECORDS) {
        return;
    }

    elapsed_seconds = (unsigned int)((elapsed_ticks + HZ - 1) / HZ);

    mj_score_file_read(&scores);

    current_best = scores.best_seconds[mj_game_layout_id()];

    if (current_best != MJ_SCORE_EMPTY &&
        elapsed_seconds >= current_best) {
        mj_best_elapsed_ticks = (long)current_best * HZ;
        return;
    }

    scores.best_seconds[mj_game_layout_id()] = elapsed_seconds;

    if (mj_score_file_write(&scores)) {
        mj_best_elapsed_ticks = (long)elapsed_seconds * HZ;
    }
}

static void draw_table_background(void)
{
#if LCD_DEPTH > 1
    int x;
    int y;

    /*
     * Subtle felt texture.
     * Keep the status area readable and avoid strong horizontal lines.
     */
    rb->lcd_set_foreground(LCD_RGBPACK(13, 65, 24));

    for (y = 38; y < LCD_HEIGHT; y += 18) {
        for (x = ((y / 18) & 1) ? 9 : 0; x < LCD_WIDTH; x += 18) {
            rb->lcd_fillrect(x, y, 1, 1);
        }
    }

    rb->lcd_set_foreground(LCD_RGBPACK(22, 90, 34));

    for (y = 46; y < LCD_HEIGHT; y += 28) {
        for (x = ((y / 28) & 1) ? 12 : 4; x < LCD_WIDTH; x += 30) {
            rb->lcd_fillrect(x, y, 1, 1);
        }
    }
#endif
}

static void start_new_game(void)
{
    clear_hint();
    mj_dead_end_prompted = false;
    mj_game_init_default((unsigned int)*rb->current_tick);
    mj_score_load_best();
    mj_stopwatch_reset();
}

static void undo_move(void)
{
    clear_hint();

    if (!mj_game_undo()) {
        rb->splash(HZ / 2, "Nothing to undo");
    }
}

static void show_hint(void)
{
    int a;
    int b;

    if (mj_game_find_hint(&a, &b)) {
        hint_tile_a = a;
        hint_tile_b = b;
    } else {
        clear_hint();
        rb->splash(HZ, "No moves");
    }
}

/* ------------------------------------------------------------------------ */
/* Positioning and rendering                                                 */
/* ------------------------------------------------------------------------ */

static void tile_position(const struct mj_tile *tile, int *x, int *y)
{
    const int layout_x = 16;
    const int layout_y = 4;

    /*
     * Exact GNOME Mahjongg projection:
     *
     * x = x_offset + slot.x * tile_width / 2
     *              + layer * tile_width / 7
     *
     * y = y_offset + slot.y * tile_height / 2
     *              - layer * tile_height / 10
     *
     * No additional row, column, availability or screen-zone offsets
     * are applied after this calculation.
     */
    *x = layout_x
       + tile->col * MJ_TILE_XSTEP
       + tile->lev * MJ_LEVEL_DX;

    *y = layout_y
       + tile->row * MJ_TILE_YSTEP
       + tile->lev * MJ_LEVEL_DY;
}

static void draw_tile_box(int x, int y, int picture, int match, int level,
                          bool open, bool selected, bool cursor, bool hint)
{
#if LCD_DEPTH > 1
    int oldfg = rb->lcd_get_foreground();
#endif
    int picture_count;
    int normal_picture_count;
    int sprite_picture;

    (void)match;
    (void)level;

    picture_count = BMPWIDTH_mahjongg_tiles / MJ_PATTERN_W;

    if (picture_count <= 0) {
        picture_count = 1;
    }

    normal_picture_count = picture_count;

    /*
     * The sheet contains three banks:
     * normal, dimmed and GNOME-highlighted.
     */
    if (picture_count >= 126) {
        normal_picture_count = picture_count / 3;
    } else if (picture_count >= 84) {
        normal_picture_count = picture_count / 2;
    }

    if (picture < 0) {
        picture = 0;
    }

    picture %= normal_picture_count;
    sprite_picture = picture;

    if ((cursor || selected || hint) &&
        picture_count >= normal_picture_count * 3) {
        sprite_picture += normal_picture_count * 2;
    } else if (!open &&
               picture_count >= normal_picture_count * 2) {
        sprite_picture += normal_picture_count;
    }

    /*
     * Draw the complete GNOME theme pattern. It contains the tile face
     * and the isometric right/bottom extension. Magenta pixels are
     * transparent, so higher tiles do not erase tiles below.
     */
    rb->lcd_bitmap_transparent_part(
        mahjongg_tiles,
        sprite_picture * MJ_PATTERN_W,
        0,
        STRIDE(
            SCREEN_MAIN,
            BMPWIDTH_mahjongg_tiles,
            BMPHEIGHT_mahjongg_tiles
        ),
        x,
        y,
        MJ_PATTERN_W,
        MJ_PATTERN_H
    );





#if LCD_DEPTH > 1
    rb->lcd_set_foreground(oldfg);
#endif
}

static void draw_status_bar(void)
{
#if LCD_DEPTH > 1
    int oldfg = rb->lcd_get_foreground();
#endif
    char time_text[8];
    char best_text[8];
    int w;
    int h;

#if LCD_DEPTH > 1
    rb->lcd_set_foreground(LCD_WHITE);
#endif

    rb->lcd_putsxyf(2, 2, "Left:%d Moves:%d",
                    mj_game_remaining(),
                    mj_game_moves());

    mj_format_time(mj_best_elapsed_ticks, best_text, sizeof(best_text));

    rb->lcd_putsxyf(2, 12, "Open:%d Best:%s",
                    mj_game_possible_moves(),
                    best_text);

    if (mj_stopwatch_enabled) {
        mj_format_time(mj_stopwatch_elapsed_ticks(), time_text, sizeof(time_text));

        rb->lcd_setfont(FONT_UI);
        rb->lcd_getstringsize(time_text, &w, &h);
        rb->lcd_putsxy(LCD_WIDTH - w - 4, 2, time_text);
        rb->lcd_setfont(MJ_FONT);
    }

    if (mj_game_remaining() > 0 &&
        mj_game_possible_moves() == 0) {
        rb->lcd_putsxy(2, 22, "No moves left");
    } else if (mj_game_selected_tile() >= 0) {
        rb->lcd_putsxy(2, 22, "Selected");
    } else {
        const char *layout_name;

        layout_name = mj_game_layout_name(
            mj_game_layout_id()
        );

        rb->lcd_putsxy(2, 22, layout_name);
    }

#if LCD_DEPTH > 1
    rb->lcd_set_foreground(oldfg);
#endif
}

static bool handle_dead_end(void)
{
    if (mj_game_remaining() <= 0 ||
        mj_game_possible_moves() > 0) {
        mj_dead_end_prompted = false;
        return false;
    }

    if (mj_dead_end_prompted) {
        return false;
    }

    mj_dead_end_prompted = true;

    if (rb->yesno_pop("No moves left. New Game?") == YESNO_YES) {
        rb->remove(MAHJONGG_SAVE_FILE);
        start_new_game();
        return true;
    }

    return false;
}

static int compare_tile_slots(const struct mj_tile *a,
                              const struct mj_tile *b)
{
    int layer_difference;
    int x_difference;
    int y_difference;

    /*
     * Exact comparator used by GNOME Mahjongg:
     * lowest layer first.
     */
    layer_difference = a->lev - b->lev;

    if (layer_difference != 0) {
        return layer_difference;
    }

    /*
     * Within a layer, sort diagonally from top-left to bottom-right.
     */
    x_difference = a->col - b->col;
    y_difference = a->row - b->row;

    if (x_difference > y_difference) {
        return -1;
    }

    if (x_difference < y_difference) {
        return 1;
    }

    return x_difference;
}

static void update_screen(void)
{
    int i;
    int n = 0;
    int pass;
    int idx[MJ_MAX_TILES];
    int sx[MJ_MAX_TILES];
    int sy[MJ_MAX_TILES];
    int board_min_x = 32767;
    int board_min_y = 32767;
    int board_max_x = -32768;
    int board_max_y = -32768;
    int board_offset_x = 0;
    int board_offset_y = 0;
    const int board_area_top = 32;

#if LCD_DEPTH > 1
    rb->lcd_set_background(MJ_TABLE_BG);
    rb->lcd_set_foreground(LCD_BLACK);
#endif

    rb->lcd_clear_display();

    draw_table_background();
    draw_status_bar();

    /*
     * Determine the complete board bounds from all real layout slots,
     * including removed tiles. This keeps the board stationary while
     * pairs are removed.
     */
    for (i = 0; i < mj_game_tile_count(); i++) {
        const struct mj_tile *tile;
        int x;
        int y;

        tile = mj_game_tile(i);

        if (tile == NULL || !tile->real) {
            continue;
        }

        tile_position(tile, &x, &y);

        if (x < board_min_x) {
            board_min_x = x;
        }

        if (y < board_min_y) {
            board_min_y = y;
        }

        if (x + MJ_PATTERN_W > board_max_x) {
            board_max_x = x + MJ_PATTERN_W;
        }

        if (y + MJ_PATTERN_H > board_max_y) {
            board_max_y = y + MJ_PATTERN_H;
        }
    }

    if (board_max_x > board_min_x &&
        board_max_y > board_min_y) {
        int board_width;
        int board_height;
        int available_height;

        board_width = board_max_x - board_min_x;
        board_height = board_max_y - board_min_y;
        available_height = LCD_HEIGHT - board_area_top;

        board_offset_x =
            (LCD_WIDTH - board_width) / 2 - board_min_x;

        board_offset_y =
            board_area_top
            + (available_height - board_height) / 2
            - board_min_y;
    }

    /*
     * Position every tile exactly once. Availability must never alter
     * a tile's visible position.
     */
    for (i = 0; i < mj_game_tile_count(); i++) {
        const struct mj_tile *tile;
        int x;
        int y;

        tile = mj_game_tile(i);

        if (tile == NULL) {
            continue;
        }

        if (!tile->real || tile->removed) {
            continue;
        }

        tile_position(tile, &x, &y);

        /*
         * Apply one global board translation. Relative tile positions,
         * layer projection, blocking and draw order remain unchanged.
         */
        x += board_offset_x;
        y += board_offset_y;

        if (x + MJ_PATTERN_W < 0 || x >= LCD_WIDTH ||
            y + MJ_PATTERN_H < 0 || y >= LCD_HEIGHT) {
            continue;
        }

        idx[n] = i;
        sx[n] = x;
        sy[n] = y;
        n++;
    }

    /*
     * Stable insertion sort using the exact GNOME Mahjongg slot
     * comparator. GNOME stores its map slots in this order and draws
     * them sequentially.
     */
    for (pass = 1; pass < n; pass++) {
        int position = pass;
        int saved_index = idx[pass];
        int saved_x = sx[pass];
        int saved_y = sy[pass];

        while (position > 0) {
            const struct mj_tile *previous;
            const struct mj_tile *current;

            previous = mj_game_tile(idx[position - 1]);
            current = mj_game_tile(saved_index);

            if (compare_tile_slots(previous, current) <= 0) {
                break;
            }

            idx[position] = idx[position - 1];
            sx[position] = sx[position - 1];
            sy[position] = sy[position - 1];
            position--;
        }

        idx[position] = saved_index;
        sx[position] = saved_x;
        sy[position] = saved_y;
    }

    for (i = 0; i < n; i++) {
        const struct mj_tile *tile;

        tile = mj_game_tile(idx[i]);

        draw_tile_box(sx[i],
                      sy[i],
                      tile->picture,
                      tile->match,
                      tile->lev,
                      mj_game_tile_open(idx[i]),
                      idx[i] == mj_game_selected_tile(),
                      idx[i] == mj_game_cursor_tile(),
                      idx[i] == hint_tile_a ||
                      idx[i] == hint_tile_b);
    }

    rb->lcd_update();
}

/* ------------------------------------------------------------------------ */
/* Save/load placeholders                                                    */
/* ------------------------------------------------------------------------ */

static int save_game(void)
{
    int fd;
    bool ok;

    rb->mkdir(PLUGIN_GAMES_DATA_DIR);

    fd = rb->open(MAHJONGG_SAVE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    if (fd < 0) {
        rb->splash(HZ, "Save failed");
        return -1;
    }

    ok = mj_game_save(fd);

    if (ok) {
        ok = mj_stopwatch_save(fd);
    }

    rb->close(fd);

    if (!ok) {
        rb->splash(HZ, "Save failed");
        return -1;
    }

    rb->splash(HZ / 2, "Game saved");
    return 0;
}


static int load_game(void)
{
    int fd;
    bool ok;

    fd = rb->open(MAHJONGG_SAVE_FILE, O_RDONLY);

    if (fd < 0) {
        return -1;
    }

    ok = mj_game_load(fd);

    if (ok) {
        mj_stopwatch_load(fd);
    }

    rb->close(fd);

    if (!ok) {
        return -1;
    }

    /*
     * Savegames contain the active board and elapsed stopwatch time.
     * Best times are stored separately in the single score file.
     */
    mj_score_load_best();

    rb->splash(HZ / 2, "Game loaded");
    return 0;
}


/* ------------------------------------------------------------------------ */
/* Menu                                                                      */
/* ------------------------------------------------------------------------ */

enum {
    MJ_MENU_RESUME = 0,
    MJ_MENU_NEW_GAME,
    MJ_MENU_TOGGLE_TIMER,
    MJ_MENU_SELECT_LAYOUT,
    MJ_MENU_RANDOM_MAPS,
    MJ_MENU_HELP,
    MJ_MENU_SAVE_QUIT,
    MJ_MENU_QUIT
};

static void show_help(void)
{
    rb->lcd_clear_display();
    rb->lcd_putsxy(2, 2, MAHJONGG_TITLE);
    rb->lcd_putsxy(2, 16, MJ_KEYS_PREV_NEXT ": Prev/Next tile");
    rb->lcd_putsxy(2, 26, MJ_KEYS_SELECT ": Select");
    rb->lcd_putsxy(2, 36, MJ_KEYS_HINT ": Hint");
    rb->lcd_putsxy(2, 46, MJ_KEYS_UNDO ": Undo");
    rb->lcd_putsxy(2, 56, MJ_KEYS_MENU ": Menu");
    rb->lcd_update();

    rb->button_clear_queue();
    rb->button_get(true);
}

static void choose_random_layout(void)
{
    int layout_count;
    int current_layout;
    int next_layout;
    unsigned long random_value;

    layout_count = mj_game_layout_count();
    current_layout = mj_game_layout_id();

    if (layout_count <= 1) {
        return;
    }

    random_value = (unsigned long)*rb->current_tick;
    random_value ^= (unsigned long)mj_stopwatch_elapsed_ticks();
    random_value ^= (unsigned long)(current_layout * 1103515245u);

    next_layout = (int)(random_value % layout_count);

    if (next_layout == current_layout) {
        next_layout = (next_layout + 1) % layout_count;
    }

    mj_game_set_layout(next_layout);
}

static void start_new_game_with_layout_policy(void)
{
    if (mj_random_maps_enabled) {
        choose_random_layout();
    }

    mj_best_elapsed_ticks = -1;
    rb->remove(MAHJONGG_SAVE_FILE);

    start_new_game();
    mj_score_load_best();

    rb->splashf(HZ,
                "%s",
                mj_game_layout_name(mj_game_layout_id()));
}

static bool select_layout_menu(void)
{
    int selection;
    int previous_layout;

    MENUITEM_STRINGLIST(layout_menu, "Select Layout", NULL,
                        "Rockbox Compact 80",
                        "Turtle 80",
                        "The Ziggurat 80",
                        "Four Bridges 80",
                        "Cloud 80",
                        "Tic-Tac-Toe 80",
                        "Red Dragon 80",
                        "Overpass 80",
                        "Pyramid's Walls 80",
                        "Confounding Cross 80",
                        "Taipei 80");

    previous_layout = mj_game_layout_id();
    selection = previous_layout;

    selection = rb->do_menu(&layout_menu,
                            &selection,
                            NULL,
                            false);

    if (selection < 0 ||
        selection >= mj_game_layout_count()) {
        return false;
    }

    if (selection == previous_layout) {
        return false;
    }

    if (!mj_game_set_layout(selection)) {
        rb->splash(HZ, "Layout error");
        return false;
    }

    /*
     * The saved game belongs to the previous board geometry.
     * Records remain in the shared mahjongg.score file.
     */
    rb->remove(MAHJONGG_SAVE_FILE);

    start_new_game();
    mj_score_load_best();

    rb->splashf(HZ,
                "%s",
                mj_game_layout_name(selection));

    return true;
}

static int mahjongg_menu(void)
{
    int selected = 0;

    MENUITEM_STRINGLIST(menu_random_off, "Mahjongg", NULL,
                        "Resume",
                        "New Game",
                        "Toggle Stopwatch",
                        "Select Layout",
                        "Random Maps: Off",
                        "Help",
                        /*"Audio Playback",*/
                        "Save and Quit",
                        "Quit without Saving");

    MENUITEM_STRINGLIST(menu_random_on, "Mahjongg", NULL,
                        "Resume",
                        "New Game",
                        "Toggle Stopwatch",
                        "Select Layout",
                        "Random Maps: On",
                        "Help",
                        /*"Audio Playback",*/
                        "Save and Quit",
                        "Quit without Saving");

    while (true) {
        switch (rb->do_menu(
                    mj_random_maps_enabled
                        ? &menu_random_on
                        : &menu_random_off,
                    &selected,
                    NULL,
                    false)) {
            case MJ_MENU_RESUME:
                return MJ_MENU_RESUME;

            case MJ_MENU_NEW_GAME:
                start_new_game_with_layout_policy();
                return MJ_MENU_RESUME;

            case MJ_MENU_TOGGLE_TIMER:
                mj_stopwatch_enabled = !mj_stopwatch_enabled;
                mj_stopwatch_reset();

                if (mj_stopwatch_enabled) {
                    rb->splash(HZ, "Stopwatch on");
                } else {
                    rb->splash(HZ, "Stopwatch off");
                }

                return MJ_MENU_RESUME;

            case MJ_MENU_SELECT_LAYOUT:
                if (select_layout_menu()) {
                    return MJ_MENU_RESUME;
                }
                break;

            case MJ_MENU_RANDOM_MAPS:
                mj_random_maps_enabled =
                    !mj_random_maps_enabled;

                if (mj_random_maps_enabled) {
                    rb->splash(HZ, "Random Maps on");
                } else {
                    rb->splash(HZ, "Random Maps off");
                }

                break;

            case MJ_MENU_HELP:
                show_help();
                break;

            /*case MJ_MENU_PLAYBACK:
                playback_control(NULL);
                break;
*/
            case MJ_MENU_SAVE_QUIT:
                save_game();
                return MJ_MENU_SAVE_QUIT;

            case MJ_MENU_QUIT:
            default:
                return MJ_MENU_QUIT;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Main loop                                                                 */
/* ------------------------------------------------------------------------ */

static enum plugin_status mahjongg_loop(void)
{
    int button;

#ifdef MJ_NEED_LASTBUTTON
    int lastbutton = BUTTON_NONE;
#endif

    while (true) {
        button = rb->button_get_w_tmo(mj_stopwatch_enabled ? HZ / 4 : HZ);

        if (button == BUTTON_NONE) {
            update_screen();
            rb->yield();
            continue;
        }


        if (handle_dead_end()) {
            update_screen();
            continue;
        }

        switch (button) {
            case MJ_PREV:
            case MJ_PREV | BUTTON_REPEAT:
                clear_hint();
                mj_game_cursor_prev();
                update_screen();
                break;

            case MJ_NEXT:
            case MJ_NEXT | BUTTON_REPEAT:
                clear_hint();
                mj_game_cursor_next();
                update_screen();
                break;

#ifdef MJ_LEFT
            case MJ_LEFT:
#ifdef MJ_LEFT_PRE
                if (lastbutton != MJ_LEFT_PRE)
                    break;
#endif
                /*
                 * Optional navigation.
                 * For now, same as previous tile.
                 */
                mj_game_cursor_prev();
                update_screen();
                break;
#endif

#ifdef MJ_RIGHT
            case MJ_RIGHT:
#ifdef MJ_RIGHT_PRE
                if (lastbutton != MJ_RIGHT_PRE)
                    break;
#endif
                /*
                 * Optional navigation.
                 * For now, same as next tile.
                 */
                mj_game_cursor_next();
                update_screen();
                break;
#endif

            case MJ_SELECT:
#ifdef MJ_SELECT_PRE
                if (lastbutton != MJ_SELECT_PRE)
                    break;
#endif
                {
                    enum mj_select_result result;

                    clear_hint();
                    result = mj_game_select_current();

                    if (result == MJ_SELECT_REMOVED_PAIR &&
                        mj_game_remaining() == 0) {
                        long elapsed;
                        long previous_best;
                        bool new_best;

                        elapsed = mj_stopwatch_elapsed_ticks();
                        previous_best = mj_best_elapsed_ticks;

                        new_best = previous_best < 0 ||
                                   elapsed < previous_best;

                        mj_score_save_best(elapsed);

                        /*
                         * A completed board must never be restored as an
                         * unfinished saved game on the next plugin start.
                         */
                        rb->remove(MAHJONGG_SAVE_FILE);

                        /*
                         * Reload the record from the single score file so the
                         * status display immediately uses the persisted value.
                         */
                        mj_score_load_best();

                        update_screen();

                        if (new_best) {
                            rb->splash(HZ * 2, "New best time!");
                        } else {
                            rb->splash(HZ * 2, "Solved!");
                        }

                        /*
                         * Stay inside the plugin. Start a fresh board while
                         * retaining the newly stored best time.
                         */
                        start_new_game();
                        update_screen();
                        break;
                    }

                    update_screen();
                }
                break;

            case MJ_HINT:
#ifdef MJ_HINT_PRE
                if (lastbutton != MJ_HINT_PRE)
                    break;
#endif
                show_hint();
                update_screen();
                break;

            case MJ_UNDO:
            case MJ_UNDO | BUTTON_REPEAT:
                undo_move();
                update_screen();
                break;

            case MJ_MENU:
                switch (mahjongg_menu()) {
                    case MJ_MENU_SAVE_QUIT:
                    case MJ_MENU_QUIT:
                        return PLUGIN_OK;

                    case MJ_MENU_RESUME:
                    default:
                        update_screen();
                        break;
                }
                break;

            case SYS_POWEROFF:
            case SYS_REBOOT:
                save_game();
                return PLUGIN_OK;

            default:
                if (rb->default_event_handler(button) == SYS_USB_CONNECTED)
                    return PLUGIN_USB_CONNECTED;
                break;
        }

#ifdef MJ_NEED_LASTBUTTON
        if (button != BUTTON_NONE)
            lastbutton = button;
#endif

        rb->yield();
    }

    return PLUGIN_OK;
}

/* ------------------------------------------------------------------------ */
/* Plugin entry point                                                        */
/* ------------------------------------------------------------------------ */

enum plugin_status plugin_start(const void *parameter)
{
    int w, h;
    int loaded;

    (void)parameter;

    rb->lcd_setfont(MJ_FONT);

    rb->lcd_clear_display();
    rb->lcd_getstringsize(MAHJONGG_TITLE, &w, &h);
    rb->lcd_putsxy(LCD_WIDTH / 2 - w / 2,
                   LCD_HEIGHT / 2 - h / 2,
                   MAHJONGG_TITLE);
    rb->lcd_update();
    rb->sleep(HZ);

    loaded = load_game();

    if (loaded != 0) {
        start_new_game();
    }

    update_screen();

    return mahjongg_loop();
}