#include "microcalc.h"

// Define Global Variables
Cell grid[MAX_ROWS][MAX_COLS];
Clipboard clipboard = { .is_active = 0 };
UndoState undo_stack[MAX_UNDO];
int undo_top = 0;
UndoState redo_stack[MAX_UNDO];
int redo_top = 0;

int cursor_r = 0;
int cursor_c = 0;

int view_r = 0;
int view_c = 0;
int is_all_selected = 0;

int sel_start_r = -1;
int sel_start_c = -1;
int sel_end_r = -1;
int sel_end_c = -1;
int is_selecting = 0;

int cursor_flash_error = 0;

const char *HELP_LINE_1 = " Nav: Arrows | Click: Select | RClick: Range | Ctrl+A: All | Ctrl+Z: Undo | Ctrl+R: Redo | Del: Clear ";
const char *HELP_LINE_2 = " Ctrl+C/X/V: Copy/Cut/Paste | Ctrl+F: Format | Ctrl+P: PDF | Ctrl+S/O: Save/Open | Ctrl+H: Help ";
char status_msg[128] = "";

int main(int argc, char *argv[]) {
    initscr();
    raw();
    noqiflush();
    noecho();
    keypad(stdscr, TRUE);
    meta(stdscr, TRUE);
    curs_set(0);

    mousemask(BUTTON1_CLICKED | BUTTON3_CLICKED | BUTTON1_DOUBLE_CLICKED, NULL);
    mouseinterval(50);

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_WHITE, COLOR_BLUE);   
        init_pair(2, COLOR_BLACK, COLOR_CYAN);   
        init_pair(3, COLOR_GREEN, COLOR_BLACK);  
        init_pair(4, COLOR_WHITE, COLOR_BLACK);  
        init_pair(5, COLOR_GREEN, COLOR_BLACK);  
        init_pair(6, COLOR_RED, COLOR_BLACK);    
        init_pair(7, COLOR_WHITE, COLOR_RED);    
    }

    memset(grid, 0, sizeof(grid));

    if (argc > 1) {
        load_sheet(argv[1]);
    }

    int ch;
    while (1) {
        draw_grid();
        ch = getch();

        status_msg[0] = '\0';

        if (ch == 'q' || ch == 'Q') {
            break;
        }

        switch (ch) {
            case KEY_UP:
                if (cursor_r > 0) cursor_r--;
                is_selecting = 0;
                break;
            case KEY_DOWN:
                if (cursor_r < MAX_ROWS - 1) cursor_r++;
                is_selecting = 0;
                break;
            case KEY_LEFT:
                if (cursor_c > 0) cursor_c--;
                is_selecting = 0;
                break;
            case KEY_RIGHT:
                if (cursor_c < MAX_COLS - 1) cursor_c++;
                is_selecting = 0;
                break;
            case '+':
                grid[cursor_r][cursor_c].color_state = 1;
                break;
            case '-':
                grid[cursor_r][cursor_c].color_state = 2;
                break;
            case '0':
                grid[cursor_r][cursor_c].color_state = 0;
                break;
            case KEY_DC:
            case 127:
            case KEY_BACKSPACE:
                clear_selected_range();
                break;
            case CTRL('z'):
                undo_action();
                break;
            case CTRL('r'):
                redo_action();
                break;
            case CTRL('c'):
                copy_selection();
                break;
            case CTRL('x'):
                cut_selection();
                break;
            case CTRL('v'):
                paste_selection();
                break;
            case CTRL('f'):
                cleanup_selection_format();
                break;
            case CTRL('p'):
                prompt_pdf_export();
                break;
            case CTRL('s'):
                prompt_save();
                break;
            case CTRL('o'):
                prompt_load();
                break;
            case CTRL('h'):
                show_cheatsheet();
                break;
            case CTRL('a'):
                is_all_selected = !is_all_selected;
                is_selecting = 0;
                snprintf(status_msg, sizeof(status_msg), is_all_selected ? " All cells marked selected. " : " Selection cleared. ");
                break;
            case KEY_MOUSE:
                handle_mouse();
                break;
            case '\n':
            case '\r':
            case KEY_ENTER:
                edit_cell();
                break;
            default:
                break;
        }
    }

    endwin();
    return 0;
}