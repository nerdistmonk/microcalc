#include "microcalc.h"

void push_undo_state(void) {
    if (undo_top < MAX_UNDO) {
        memcpy(undo_stack[undo_top].grid_state, grid, sizeof(grid));
        undo_top++;
    } else {
        memmove(&undo_stack[0], &undo_stack[1], sizeof(UndoState) * (MAX_UNDO - 1));
        memcpy(undo_stack[MAX_UNDO - 1].grid_state, grid, sizeof(grid));
    }
}

void undo_action(void) {
    if (undo_top > 0) {
        undo_top--;
        memcpy(grid, undo_stack[undo_top].grid_state, sizeof(grid));
        recalculate_all();
        snprintf(status_msg, sizeof(status_msg), " Undo successful. ");
    } else {
        snprintf(status_msg, sizeof(status_msg), " Nothing to undo! ");
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
    snprintf(status_msg, sizeof(status_msg), " Copied %dx%d cells to clipboard. ", clipboard.rows, clipboard.cols);
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
    snprintf(status_msg, sizeof(status_msg), " Cut %dx%d cells to clipboard. ", clipboard.rows, clipboard.cols);
}

void paste_selection(void) {
    if (!clipboard.is_active) {
        snprintf(status_msg, sizeof(status_msg), " Error: Clipboard is empty! ");
        return;
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