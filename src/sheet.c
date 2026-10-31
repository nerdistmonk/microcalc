#include "microcalc.h"

// Forward declarations for system clipboard functions in io.c
void write_system_clipboard(const char *text);
int read_system_clipboard(char *buf, size_t max_len);

void push_undo_state(void) {
    if (undo_top < MAX_UNDO) {
        memcpy(undo_stack[undo_top].grid_state, grid, sizeof(grid));
        undo_top++;
    } else {
        memmove(&undo_stack[0], &undo_stack[1], sizeof(UndoState) * (MAX_UNDO - 1));
        memcpy(undo_stack[MAX_UNDO - 1].grid_state, grid, sizeof(grid));
    }
    redo_top = 0;
}

void undo_action(void) {
    if (undo_top > 0) {
        if (redo_top < MAX_UNDO) {
            memcpy(redo_stack[redo_top].grid_state, grid, sizeof(grid));
            redo_top++;
        } else {
            memmove(&redo_stack[0], &redo_stack[1], sizeof(UndoState) * (MAX_UNDO - 1));
            memcpy(redo_stack[MAX_UNDO - 1].grid_state, grid, sizeof(grid));
        }

        undo_top--;
        memcpy(grid, undo_stack[undo_top].grid_state, sizeof(grid));
        recalculate_all();
        snprintf(status_msg, sizeof(status_msg), " Undo successful. ");
    } else {
        snprintf(status_msg, sizeof(status_msg), " Nothing to undo! ");
    }
}

void redo_action(void) {
    if (redo_top > 0) {
        if (undo_top < MAX_UNDO) {
            memcpy(undo_stack[undo_top].grid_state, grid, sizeof(grid));
            undo_top++;
        } else {
            memmove(&undo_stack[0], &undo_stack[1], sizeof(UndoState) * (MAX_UNDO - 1));
            memcpy(undo_stack[MAX_UNDO - 1].grid_state, grid, sizeof(grid));
        }

        redo_top--;
        memcpy(grid, redo_stack[redo_top].grid_state, sizeof(grid));
        recalculate_all();
        snprintf(status_msg, sizeof(status_msg), " Redo successful. ");
    } else {
        snprintf(status_msg, sizeof(status_msg), " Nothing to redo! ");
    }
}

void cleanup_selection_format(void) {
    push_undo_state();
    int min_r, max_r, min_c, max_c;
    if (!is_selecting && !is_all_selected) {
        min_r = 0; max_r = MAX_ROWS - 1;
        min_c = max_c = cursor_c;
    } else {
        get_selected_range(&min_r, &max_r, &min_c, &max_c);
    }

    for (int c = min_c; c <= max_c; c++) {
        int has_dollar = 0;
        int has_decimal = 0;

        for (int r = min_r; r <= max_r; r++) {
            if (grid[r][c].text[0] != '\0' && grid[r][c].text[0] != '=' && is_pure_number(grid[r][c].text)) {
                if (strchr(grid[r][c].text, '$')) has_dollar = 1;
                if (strchr(grid[r][c].text, '.')) has_decimal = 1;
            }
        }

        for (int r = min_r; r <= max_r; r++) {
            if (grid[r][c].text[0] != '\0' && grid[r][c].text[0] != '=' && is_pure_number(grid[r][c].text)) {
                double val = parse_number(grid[r][c].text);
                if (has_dollar) {
                    if (val < 0) {
                        snprintf(grid[r][c].text, sizeof(grid[r][c].text), "-$%.2f", fabs(val));
                    } else {
                        snprintf(grid[r][c].text, sizeof(grid[r][c].text), "$%.2f", val);
                    }
                } else if (has_decimal) {
                    snprintf(grid[r][c].text, sizeof(grid[r][c].text), "%.2f", val);
                }
            }
        }
    }

    recalculate_all();
    snprintf(status_msg, sizeof(status_msg), " Column formatting standardized! ");
}

void adjust_formula_references(const char *src_formula, char *dest, size_t dest_size, int delta_r, int delta_c) {
    size_t src_len = strlen(src_formula);
    size_t d = 0;
    for (size_t i = 0; i < src_len && d < dest_size - 1; ) {
        if (isalpha((unsigned char)src_formula[i]) && isdigit((unsigned char)src_formula[i+1])) {
            char col_ch = src_formula[i];
            int row_num = 0;
            size_t j = i + 1;
            while (j < src_len && isdigit((unsigned char)src_formula[j])) {
                row_num = row_num * 10 + (src_formula[j] - '0');
                j++;
            }
            int new_col = (toupper((unsigned char)col_ch) - 'A') + delta_c;
            int new_row = row_num + delta_r;
            if (new_col >= 0 && new_col < MAX_COLS && new_row >= 0 && new_row < MAX_ROWS) {
                d += snprintf(dest + d, dest_size - d, "%c%d", 'A' + new_col, new_row);
            } else {
                for (size_t k = i; k < j && d < dest_size - 1; k++) {
                    dest[d++] = src_formula[k];
                }
            }
            i = j;
        } else {
            dest[d++] = src_formula[i++];
        }
    }
    dest[d] = '\0';
}

void copy_selection(void) {
    int min_r, max_r, min_c, max_c;
    get_selected_range(&min_r, &max_r, &min_c, &max_c);

    clipboard.start_r = min_r;
    clipboard.start_c = min_c;
    clipboard.rows = max_r - min_r + 1;
    clipboard.cols = max_c - min_c + 1;

    for (int r = 0; r < clipboard.rows; r++) {
        for (int c = 0; c < clipboard.cols; c++) {
            clipboard.cells[r][c] = grid[min_r + r][min_c + c];
        }
    }
    clipboard.is_active = 1;

    // Serialize to TSV and push to system clipboard (Wayland / X11)
    char tsv_buf[8192] = {0};
    size_t offset = 0;
    for (int r = 0; r < clipboard.rows && offset < sizeof(tsv_buf) - 1; r++) {
        for (int c = 0; c < clipboard.cols && offset < sizeof(tsv_buf) - 1; c++) {
            char disp[64];
            Cell *cell = &clipboard.cells[r][c];
            if (cell->text[0] != '\0') {
                if (cell->is_formula) {
                    snprintf(disp, sizeof(disp), "%g", cell->value);
                } else {
                    strncpy(disp, cell->text, sizeof(disp) - 1);
                    disp[sizeof(disp) - 1] = '\0';
                }
            } else {
                disp[0] = '\0';
            }
            int written = snprintf(tsv_buf + offset, sizeof(tsv_buf) - offset, "%s", disp);
            if (written > 0) offset += written;

            if (c < clipboard.cols - 1 && offset < sizeof(tsv_buf) - 1) {
                tsv_buf[offset++] = '\t';
                tsv_buf[offset] = '\0';
            }
        }
        if (r < clipboard.rows - 1 && offset < sizeof(tsv_buf) - 1) {
            tsv_buf[offset++] = '\n';
            tsv_buf[offset] = '\0';
        }
    }
    write_system_clipboard(tsv_buf);

    snprintf(status_msg, sizeof(status_msg), " Copied %dx%d cells (internal & system). ", clipboard.rows, clipboard.cols);
}

void cut_selection(void) {
    push_undo_state();
    int min_r, max_r, min_c, max_c;
    get_selected_range(&min_r, &max_r, &min_c, &max_c);

    copy_selection();

    for (int r = min_r; r <= max_r; r++) {
        for (int c = min_c; c <= max_c; c++) {
            memset(&grid[r][c], 0, sizeof(Cell));
        }
    }
    is_selecting = 0;
    is_all_selected = 0;
    recalculate_all();
    snprintf(status_msg, sizeof(status_msg), " Cut %dx%d cells. ", clipboard.rows, clipboard.cols);
}

void paste_selection(void) {
    // If internal clipboard is inactive, try loading from system clipboard as a TSV/CSV block
    if (!clipboard.is_active) {
        char sys_buf[8192] = {0};
        if (read_system_clipboard(sys_buf, sizeof(sys_buf))) {
            push_undo_state();
            char *line = strtok(sys_buf, "\n");
            int r = cursor_r;
            int max_pasted_cols = 0;
            int rows_pasted = 0;

            while (line && r < MAX_ROWS) {
                char *token = strtok(line, "\t");
                if (!token) token = strtok(line, ","); // Fallback to comma if not TSV
                int c = cursor_c;
                int cols_in_row = 0;

                while (token && c < MAX_COLS) {
                    // Trim whitespaces
                    while (*token == ' ' || *token == '\t') token++;
                    char *end = token + strlen(token) - 1;
                    while (end > token && (*end == ' ' || *end == '\t' || *end == '\r')) {
                        *end = '\0';
                        end--;
                    }

                    memset(&grid[r][c], 0, sizeof(Cell));
                    strncpy(grid[r][c].text, token, sizeof(grid[r][c].text) - 1);

                    token = strtok(NULL, "\t,");
                    c++;
                    cols_in_row++;
                }
                if (cols_in_row > max_pasted_cols) max_pasted_cols = cols_in_row;
                line = strtok(NULL, "\n");
                r++;
                rows_pasted++;
            }

            is_selecting = 0;
            is_all_selected = 0;
            recalculate_all();
            snprintf(status_msg, sizeof(status_msg), " Pasted %dx%d cells from system clipboard. ", rows_pasted, max_pasted_cols);
            return;
        } else {
            snprintf(status_msg, sizeof(status_msg), " Error: Clipboard is empty! ");
            return;
        }
    }

    push_undo_state();
    int delta_r = cursor_r - clipboard.start_r;
    int delta_c = cursor_c - clipboard.start_c;

    for (int r = 0; r < clipboard.rows; r++) {
        for (int c = 0; c < clipboard.cols; c++) {
            int tr = cursor_r + r;
            int tc = cursor_c + c;

            if (tr >= 0 && tr < MAX_ROWS && tc >= 0 && tc < MAX_COLS) {
                Cell src = clipboard.cells[r][c];
                grid[tr][tc].color_state = src.color_state;

                if (src.text[0] == '=' && (delta_r != 0 || delta_c != 0)) {
                    adjust_formula_references(src.text, grid[tr][tc].text, sizeof(grid[tr][tc].text), delta_r, delta_c);
                } else {
                    strncpy(grid[tr][tc].text, src.text, sizeof(grid[tr][tc].text) - 1);
                    grid[tr][tc].text[sizeof(grid[tr][tc].text) - 1] = '\0';
                }
            }
        }
    }

    is_selecting = 0;
    is_all_selected = 0;
    recalculate_all();
    snprintf(status_msg, sizeof(status_msg), " Pasted %dx%d cells. ", clipboard.rows, clipboard.cols);
}

void clear_selected_range(void) {
    push_undo_state();
    if (is_selecting || is_all_selected) {
        int min_r, max_r, min_c, max_c;
        get_selected_range(&min_r, &max_r, &min_c, &max_c);

        for (int r = min_r; r <= max_r; r++) {
            for (int c = min_c; c <= max_c; c++) {
                memset(&grid[r][c], 0, sizeof(Cell));
            }
        }
        is_selecting = 0;
        is_all_selected = 0;
        recalculate_all();
        snprintf(status_msg, sizeof(status_msg), " Cleared selected range. ");
    } else {
        memset(&grid[cursor_r][cursor_c], 0, sizeof(Cell));
        recalculate_all();
        snprintf(status_msg, sizeof(status_msg), " Cleared cell %c%d ", 'A' + cursor_c, cursor_r);
    }
}