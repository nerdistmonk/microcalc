#ifndef MICROCALC_H
#define MICROCALC_H

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_ROWS 100
#define MAX_COLS 26
#define MIN_CELL_WIDTH 10
#define MAX_CELL_WIDTH 30
#define MAX_UNDO 20

#define CTRL(c) ((c) & 0x1f)

typedef struct {
    char text[64];
    double value;
    int is_formula;
    int color_state; // 0: Normal, 1: Green (+), 2: Red (-)
} Cell;

typedef struct {
    Cell cells[MAX_ROWS][MAX_COLS];
    int start_r;
    int start_c;
    int rows;
    int cols;
    int is_active;
} Clipboard;

typedef struct {
    Cell grid_state[MAX_ROWS][MAX_COLS];
} UndoState;

// Global Variables (extern definitions)
extern Cell grid[MAX_ROWS][MAX_COLS];
extern Clipboard clipboard;
extern UndoState undo_stack[MAX_UNDO];
extern int undo_top;
extern int cursor_r;
extern int cursor_c;
extern int view_r;
extern int view_c;
extern int is_all_selected;
extern int sel_start_r;
extern int sel_start_c;
extern int sel_end_r;
extern int sel_end_c;
extern int is_selecting;
extern int cursor_flash_error;
extern char status_msg[128];

extern const char *HELP_LINE_1;
extern const char *HELP_LINE_2;

// Function Prototypes
double parse_number(const char *str);
int is_pure_number(const char *str);
int get_col_width(int c);
void get_selected_range(int *min_r, int *max_r, int *min_c, int *max_c);
int is_cell_selected(int r, int c);

void push_undo_state(void);
void undo_action(void);
void recalculate_all(void);
void eval_cell(int r, int c);

void cleanup_selection_format(void);
void copy_selection(void);
void cut_selection(void);
void paste_selection(void);
void clear_selected_range(void);
void adjust_formula_references(const char *src_formula, char *dest, size_t dest_size, int delta_r, int delta_c);

void load_csv(const char *filename);
void save_csv(const char *filename);
void save_sheet(const char *filename);
void load_sheet(const char *filename);
void export_pdf(const char *filename);

void show_cheatsheet(void);
void prompt_input(const char *prompt_str, char *output, size_t max_len);
void prompt_save(void);
void prompt_load(void);
void prompt_pdf_export(void);
void edit_cell(void);
void draw_grid(void);
void handle_mouse(void);

#endif