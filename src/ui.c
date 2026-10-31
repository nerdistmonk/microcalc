#include "microcalc.h"

void show_cheatsheet(void) {
    clear();
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(1, 2, " ==================== microcalc Cheat Sheet ==================== ");
    attroff(COLOR_PAIR(1) | A_BOLD);

    mvprintw(3, 2, " [ Navigation & Mouse Controls ]");
    mvprintw(4, 4, "• Arrow Keys         : Navigate active cell cursor");
    mvprintw(5, 4, "• Left Click         : Select cell (Click active cell to edit)");
    mvprintw(6, 4, "• Right Click        : Drag or set multi-cell selection range");
    mvprintw(7, 4, "• Double Left Click  : Quick-sum range into empty target cell");

    mvprintw(9, 2, " [ Editing, Formatting & Clipboard ]");
    mvprintw(10, 4, "• Enter / Return     : Edit contents of selected cell");
    mvprintw(11, 4, "• Del / Backspace    : Clear active cell or current selection");
    mvprintw(12, 4, "• Ctrl+Z / Ctrl+R    : Undo / Redo last grid action");
    mvprintw(13, 4, "• Ctrl+A             : Toggle select all cells");
    mvprintw(14, 4, "• Ctrl+C / X / V     : Copy, Cut, or Paste selection range");
    mvprintw(15, 4, "• Ctrl+F             : Clean up / standardize formatting ($20.00 / 20.00)");
    mvprintw(16, 4, "• +, -, 0 Keys       : Tag cell color (Green profit, Red loss, Normal)");

    mvprintw(18, 2, " [ File I/O & Formulas ]");
    mvprintw(19, 4, "• Ctrl+S / Ctrl+O    : Save / Open (.mc native or .csv flat spreadsheet)");
    mvprintw(20, 4, "• Ctrl+P             : Export printable sheet as PDF document");
    mvprintw(21, 4, "• Formula Examples   : =A0+A1*(B2-5) | =SUM(A0:A10) | =AVG(C0:C5)");

    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(23, 2, " Press any key to return to your spreadsheet... ");
    attroff(COLOR_PAIR(3) | A_BOLD);
    refresh();
    getch();
}

void edit_cell(void) {
    echo();
    curs_set(1);

    mvprintw(3, 0, "Edit cell %c%d: ", 'A' + cursor_c, cursor_r);
    clrtoeol();

    char buffer[64];
    getnstr(buffer, 63);

    push_undo_state();

    if (strcasecmp(buffer, "=D") == 0) {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    } else if (strcasecmp(buffer, "=T") == 0) {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else if (strcasecmp(buffer, "=DT") == 0) {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    }

    strncpy(grid[cursor_r][cursor_c].text, buffer, sizeof(grid[cursor_r][cursor_c].text) - 1);

    noecho();
    curs_set(0);

    recalculate_all();
    status_msg[0] = '\0';
}

void draw_grid(void) {
    clear();

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int visible_rows = max_y - 5;
    if (visible_rows > MAX_ROWS) visible_rows = MAX_ROWS;

    if (cursor_r < view_r) view_r = cursor_r;
    if (cursor_r >= view_r + visible_rows) view_r = cursor_r - visible_rows + 1;
    if (cursor_c < view_c) view_c = cursor_c;

    int col_widths[MAX_COLS];
    int visible_cols = 0;
    int x_acc = 4;

    for (int c = view_c; c < MAX_COLS; c++) {
        col_widths[c] = get_col_width(c);
        if (x_acc + col_widths[c] + 1 <= max_x) {
            visible_cols++;
            x_acc += col_widths[c] + 1;
        } else {
            break;
        }
    }
    if (visible_cols == 0) visible_cols = 1;

    if (cursor_c >= view_c + visible_cols) {
        view_c = cursor_c - visible_cols + 1;
        visible_cols = 0;
        x_acc = 4;
        for (int c = view_c; c < MAX_COLS; c++) {
            col_widths[c] = get_col_width(c);
            if (x_acc + col_widths[c] + 1 <= max_x) {
                visible_cols++;
                x_acc += col_widths[c] + 1;
            } else {
                break;
            }
        }
        if (visible_cols == 0) visible_cols = 1;
    }

    // Draw top information panels spanning the full width (max_x)
    attron(COLOR_PAIR(1) | A_BOLD);
    {
        char buf[512];
        int len;

        // Row 0: Active cell info & formula/value
        snprintf(buf, sizeof(buf), " microcalc: [%c%d] Val/Formula: %s ", 'A' + cursor_c, cursor_r, grid[cursor_r][cursor_c].text);
        len = (int)strlen(buf);
        mvprintw(0, 0, "%s", buf);
        for (int x = len; x < max_x; x++) printw(" ");

        // Row 1: Status message or Help Line 1
        const char *line1_text = (status_msg[0] != '\0') ? status_msg : HELP_LINE_1;
        snprintf(buf, sizeof(buf), "%s", line1_text);
        len = (int)strlen(buf);
        mvprintw(1, 0, "%s", buf);
        for (int x = len; x < max_x; x++) printw(" ");

        // Row 2: Help Line 2
        snprintf(buf, sizeof(buf), "%s", HELP_LINE_2);
        len = (int)strlen(buf);
        mvprintw(2, 0, "%s", buf);
        for (int x = len; x < max_x; x++) printw(" ");
    }
    attroff(COLOR_PAIR(1) | A_BOLD);

    attron(COLOR_PAIR(3));
    mvprintw(4, 0, "    ");
    for (int c = view_c; c < view_c + visible_cols && c < MAX_COLS; c++) {
        int w = col_widths[c];
        printw(" %*c%*s", (w / 2) + 1, 'A' + c, w - (w / 2) - 1, "");
    }
    attroff(COLOR_PAIR(3));

    for (int r = 0; r < visible_rows && (view_r + r) < MAX_ROWS; r++) {
        int actual_r = view_r + r;

        attron(COLOR_PAIR(3));
        mvprintw(5 + r, 0, "%2d |", actual_r);
        attroff(COLOR_PAIR(3));

        for (int c = view_c; c < view_c + visible_cols && c < MAX_COLS; c++) {
            int w = col_widths[c];
            int is_cursor = (actual_r == cursor_r && c == cursor_c);
            int selected = is_cell_selected(actual_r, c);

            if (is_cursor) {
                if (cursor_flash_error) {
                    attron(COLOR_PAIR(7) | A_BOLD);
                } else {
                    attron(COLOR_PAIR(2) | A_BOLD);
                }
            } else if (selected || is_all_selected) {
                attron(COLOR_PAIR(4) | A_REVERSE);
            } else {
                if (grid[actual_r][c].color_state == 1) {
                    attron(COLOR_PAIR(5));
                } else if (grid[actual_r][c].color_state == 2) {
                    attron(COLOR_PAIR(6));
                } else {
                    attron(COLOR_PAIR(4));
                }
            }

            if (grid[actual_r][c].text[0] == '\0') {
                printw("%*s", w, "");
            } else if (grid[actual_r][c].is_formula) {
                printw("%*g", w, grid[actual_r][c].value);
            } else {
                printw("%*s", w, grid[actual_r][c].text);
            }

            if (is_cursor) {
                attroff(cursor_flash_error ? (COLOR_PAIR(7) | A_BOLD) : (COLOR_PAIR(2) | A_BOLD));
            } else if (selected || is_all_selected) {
                attroff(COLOR_PAIR(4) | A_REVERSE);
            } else {
                if (grid[actual_r][c].color_state == 1) attroff(COLOR_PAIR(5));
                else if (grid[actual_r][c].color_state == 2) attroff(COLOR_PAIR(6));
                else attroff(COLOR_PAIR(4));
            }

            attron(COLOR_PAIR(3));
            printw("|");
            attroff(COLOR_PAIR(3));
        }
    }

    refresh();
    cursor_flash_error = 0;
}

void handle_mouse(void) {
    MEVENT event;
    if (getmouse(&event) == OK) {
        int clicked_r = event.y - 5 + view_r;
        
        int clicked_c = -1;
        int x_acc = 4;
        for (int c = view_c; c < MAX_COLS; c++) {
            int w = get_col_width(c) + 1;
            if (event.x >= x_acc && event.x < x_acc + w) {
                clicked_c = c;
                break;
            }
            x_acc += w;
        }

        if (clicked_r >= 0 && clicked_r < MAX_ROWS && clicked_c >= 0 && clicked_c < MAX_COLS) {
            if (event.bstate & BUTTON1_CLICKED) {
                if (cursor_r == clicked_r && cursor_c == clicked_c) {
                    edit_cell();
                } else {
                    cursor_r = clicked_r;
                    cursor_c = clicked_c;
                    is_selecting = 0;
                }
            } else if (event.bstate & BUTTON3_CLICKED) {
                if (!is_selecting) {
                    sel_start_r = cursor_r;
                    sel_start_c = cursor_c;
                    is_selecting = 1;
                }
                sel_end_r = clicked_r;
                sel_end_c = clicked_c;
                snprintf(status_msg, sizeof(status_msg), " Range selected. ");
            } else if (event.bstate & BUTTON1_DOUBLE_CLICKED) {
                if (grid[clicked_r][clicked_c].text[0] == '\0') {
                    if (is_selecting) {
                        push_undo_state();
                        int min_r, max_r, min_c, max_c;
                        get_selected_range(&min_r, &max_r, &min_c, &max_c);

                        if (clicked_r == max_r && max_r > min_r) max_r--;
                        else if (clicked_r == min_r && min_r < max_r) min_r++;

                        if (clicked_c == max_c && max_c > min_c) max_c--;
                        else if (clicked_c == min_c && min_c < max_c) min_c++;

                        snprintf(grid[clicked_r][clicked_c].text, sizeof(grid[clicked_r][clicked_c].text), 
                                 "=SUM(%c%d:%c%d)", 'A' + min_c, min_r, 'A' + max_c, max_r);
                        cursor_r = clicked_r;
                        cursor_c = clicked_c;
                        is_selecting = 0;
                        recalculate_all();
                        snprintf(status_msg, sizeof(status_msg), " Quick sum calculated! ");
                    } else {
                        snprintf(status_msg, sizeof(status_msg), " Right-click select a range first! ");
                    }
                } else {
                    cursor_r = clicked_r;
                    cursor_c = clicked_c;
                    cursor_flash_error = 1;
                    snprintf(status_msg, sizeof(status_msg), " Error: Target cell is occupied! ");
                }
            }
        }
    }
}