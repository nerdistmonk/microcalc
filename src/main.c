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

Cell grid[MAX_ROWS][MAX_COLS];
Clipboard clipboard = { .is_active = 0 };

UndoState undo_stack[MAX_UNDO];
int undo_top = 0;

int cursor_r = 0;
int cursor_c = 0;

int view_r = 0;
int view_c = 0;
int is_all_selected = 0;

static int cur_eval_r = -1;
static int cur_eval_c = -1;

int sel_start_r = -1;
int sel_start_c = -1;
int sel_end_r = -1;
int sel_end_c = -1;
int is_selecting = 0;

int cursor_flash_error = 0;

// Synchronized On-Screen Help Header Lines
const char *HELP_LINE_1 = " Nav: Arrows | Click: Select | RClick: Range | Ctrl+A: All | Ctrl+Z: Undo | Del: Clear ";
const char *HELP_LINE_2 = " Ctrl+C/X/V: Copy/Cut/Paste | Ctrl+F: Format | Ctrl+P: PDF | Ctrl+S/O: Save/Open | Ctrl+H: Help ";
char status_msg[128] = "";

void recalculate_all(void);

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

double parse_number(const char *str) {
    int is_negative = 0;
    int has_digits = 0;
    char clean[64];
    int idx = 0;

    for (; *str; str++) {
        if (*str == '-' || *str == '(') {
            is_negative = 1;
        } else if (isdigit((unsigned char)*str) || *str == '.') {
            if (idx < (int)sizeof(clean) - 1) {
                clean[idx++] = *str;
                has_digits = 1;
            }
        }
    }
    clean[idx] = '\0';

    if (!has_digits) return 0.0;

    double val = atof(clean);
    return is_negative ? -val : val;
}

int is_pure_number(const char *str) {
    int has_digit = 0;
    for (; *str; str++) {
        if (isdigit((unsigned char)*str)) {
            has_digit = 1;
        } else if (*str != '.' && *str != '$' && *str != '-' && *str != '+' && 
                   *str != ',' && *str != ' ' && *str != '(' && *str != ')') {
            return 0;
        }
    }
    return has_digit;
}

int get_col_width(int c) {
    int max_len = MIN_CELL_WIDTH;
    for (int r = 0; r < MAX_ROWS; r++) {
        int len = 0;
        if (grid[r][c].text[0] != '\0') {
            if (grid[r][c].is_formula) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%g", grid[r][c].value);
                len = (int)strlen(buf);
            } else {
                len = (int)strlen(grid[r][c].text);
            }
        }
        if (len > max_len) {
            max_len = len;
        }
    }
    max_len += 2;
    if (max_len > MAX_CELL_WIDTH) max_len = MAX_CELL_WIDTH;
    return max_len;
}

void get_selected_range(int *min_r, int *max_r, int *min_c, int *max_c) {
    if (is_all_selected) {
        *min_r = 0; *max_r = MAX_ROWS - 1;
        *min_c = 0; *max_c = MAX_COLS - 1;
    } else if (is_selecting) {
        *min_r = (sel_start_r < sel_end_r) ? sel_start_r : sel_end_r;
        *max_r = (sel_start_r > sel_end_r) ? sel_start_r : sel_end_r;
        *min_c = (sel_start_c < sel_end_c) ? sel_start_c : sel_end_c;
        *max_c = (sel_start_c > sel_end_c) ? sel_start_c : sel_end_c;
    } else {
        *min_r = *max_r = cursor_r;
        *min_c = *max_c = cursor_c;
    }
}

int is_cell_selected(int r, int c) {
    if (is_all_selected) return 1;
    if (!is_selecting) return (r == cursor_r && c == cursor_c);

    int min_r, max_r, min_c, max_c;
    get_selected_range(&min_r, &max_r, &min_c, &max_c);

    return (r >= min_r && r <= max_r && c >= min_c && c <= max_c);
}

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
    mvprintw(12, 4, "• Ctrl+Z             : Undo last grid action / load");
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

/* ==================== FORMULA PARSER ENGINE ==================== */

typedef struct {
    const char *src;
    int pos;
} Parser;

static void skip_whitespace(Parser *p) {
    while (p->src[p->pos] == ' ' || p->src[p->pos] == '\t' || 
           p->src[p->pos] == '\r' || p->src[p->pos] == '\n') {
        p->pos++;
    }
}

static double eval_range(const char *func, int r1, int c1, int r2, int c2) {
    int min_r = (r1 < r2) ? r1 : r2;
    int max_r = (r1 > r2) ? r1 : r2;
    int min_c = (c1 < c2) ? c1 : c2;
    int max_c = (c1 > c2) ? c1 : c2;

    double sum = 0.0, max_val = 0.0, min_val = 0.0;
    int count = 0, first = 1;

    for (int r = min_r; r <= max_r; r++) {
        for (int c = min_c; c <= max_c; c++) {
            if (r == cur_eval_r && c == cur_eval_c) continue;

            if (r >= 0 && r < MAX_ROWS && c >= 0 && c < MAX_COLS) {
                if (grid[r][c].text[0] != '\0') {
                    double val = grid[r][c].value;
                    sum += val;
                    count++;
                    if (first) {
                        max_val = min_val = val;
                        first = 0;
                    } else {
                        if (val > max_val) max_val = val;
                        if (val < min_val) min_val = val;
                    }
                }
            }
        }
    }

    if (strcasecmp(func, "SUM") == 0 || strcasecmp(func, "S") == 0) return sum;
    if (strcasecmp(func, "AVG") == 0 || strcasecmp(func, "AVERAGE") == 0 || strcasecmp(func, "A") == 0) return (count > 0) ? (sum / count) : 0.0;
    if (strcasecmp(func, "MAX") == 0 || strcasecmp(func, "M") == 0) return (count > 0) ? max_val : 0.0;
    if (strcasecmp(func, "MIN") == 0 || strcasecmp(func, "m") == 0) return (count > 0) ? min_val : 0.0;
    if (strcasecmp(func, "COUNT") == 0 || strcasecmp(func, "C") == 0) return (double)count;

    return sum;
}

static double parse_expr(Parser *p);

static double parse_factor(Parser *p) {
    skip_whitespace(p);

    if (p->src[p->pos] == '+') {
        p->pos++;
        return parse_factor(p);
    }
    if (p->src[p->pos] == '-') {
        p->pos++;
        return -parse_factor(p);
    }
    if (p->src[p->pos] == '$') {
        p->pos++;
        return parse_factor(p);
    }

    if (p->src[p->pos] == '(') {
        p->pos++;
        double val = parse_expr(p);
        skip_whitespace(p);
        if (p->src[p->pos] == ')') p->pos++;
        return val;
    }

    if (isalpha((unsigned char)p->src[p->pos])) {
        int start = p->pos;
        while (isalpha((unsigned char)p->src[p->pos])) p->pos++;
        int len = p->pos - start;
        char token[16] = {0};
        if (len < 15) strncpy(token, p->src + start, len);

        skip_whitespace(p);

        if (p->src[p->pos] == '(') {
            p->pos++;
            char c1 = '\0', c2 = '\0';
            int r1 = -1, r2 = -1;
            if (sscanf(p->src + p->pos, " %c%d : %c%d", &c1, &r1, &c2, &r2) == 4) {
                while (p->src[p->pos] != ')' && p->src[p->pos] != '\0') p->pos++;
                if (p->src[p->pos] == ')') p->pos++;
                int col1 = toupper((unsigned char)c1) - 'A';
                int col2 = toupper((unsigned char)c2) - 'A';
                return eval_range(token, r1, col1, r2, col2);
            }
        } else {
            int col_idx = toupper((unsigned char)token[0]) - 'A';
            int row_idx = 0;
            while (isdigit((unsigned char)p->src[p->pos])) {
                row_idx = row_idx * 10 + (p->src[p->pos] - '0');
                p->pos++;
            }
            if (row_idx == cur_eval_r && col_idx == cur_eval_c) return 0.0;
            if (col_idx >= 0 && col_idx < MAX_COLS && row_idx >= 0 && row_idx < MAX_ROWS) {
                return grid[row_idx][col_idx].value;
            }
            return 0.0;
        }
    }

    if (isdigit((unsigned char)p->src[p->pos]) || p->src[p->pos] == '.') {
        char *endptr;
        double val = strtod(p->src + p->pos, &endptr);
        p->pos = (int)(endptr - p->src);
        return val;
    }

    return 0.0;
}

static double parse_term(Parser *p) {
    double left = parse_factor(p);
    while (1) {
        skip_whitespace(p);
        char op = p->src[p->pos];
        if (op == '*' || op == '/') {
            p->pos++;
            double right = parse_factor(p);
            if (op == '*') left *= right;
            else if (op == '/') left = (right != 0.0) ? (left / right) : 0.0;
        } else {
            break;
        }
    }
    return left;
}

static double parse_expr(Parser *p) {
    double left = parse_term(p);
    while (1) {
        skip_whitespace(p);
        char op = p->src[p->pos];
        if (op == '+' || op == '-') {
            p->pos++;
            double right = parse_term(p);
            if (op == '+') left += right;
            else if (op == '-') left -= right;
        } else {
            break;
        }
    }
    return left;
}

void eval_cell(int r, int c) {
    cur_eval_r = r;
    cur_eval_c = c;

    if (grid[r][c].text[0] == '=') {
        grid[r][c].is_formula = 1;
        const char *expr = grid[r][c].text + 1;

        char c1, c2, temp_op;
        int r1, r2;

        if (sscanf(expr, " %c%d : %c%d", &c1, &r1, &c2, &r2) == 4) {
            int col1 = toupper((unsigned char)c1) - 'A';
            int col2 = toupper((unsigned char)c2) - 'A';
            grid[r][c].value = eval_range("SUM", r1, col1, r2, col2);
            return;
        }
        if (sscanf(expr, " %c%d%c%c%d", &c1, &r1, &temp_op, &c2, &r2) == 5 && strchr("asMmCc", temp_op)) {
            int col1 = toupper((unsigned char)c1) - 'A';
            int col2 = toupper((unsigned char)c2) - 'A';
            char fn_str[2] = {temp_op, '\0'};
            grid[r][c].value = eval_range(fn_str, r1, col1, r2, col2);
            return;
        }

        Parser p = { .src = expr, .pos = 0 };
        grid[r][c].value = parse_expr(&p);
    } else {
        grid[r][c].is_formula = 0;
        grid[r][c].value = parse_number(grid[r][c].text);
    }
}

void recalculate_all(void) {
    for (int pass = 0; pass < 50; pass++) {
        int changed = 0;
        for (int r = 0; r < MAX_ROWS; r++) {
            for (int c = 0; c < MAX_COLS; c++) {
                if (grid[r][c].text[0] == '=') {
                    double prev = grid[r][c].value;
                    eval_cell(r, c);
                    if (fabs(grid[r][c].value - prev) > 1e-9) {
                        changed = 1;
                    }
                } else {
                    eval_cell(r, c);
                }
            }
        }
        if (!changed) break;
    }
}

/* ==================== FORMAT CLEANUP & PDF EXPORT ==================== */

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

static void escape_pdf_string(const char *src, char *dest, size_t dest_size) {
    size_t d = 0;
    for (size_t i = 0; src[i] != '\0' && d < dest_size - 2; i++) {
        if (src[i] == '(' || src[i] == ')' || src[i] == '\\') {
            dest[d++] = '\\';
        }
        dest[d++] = src[i];
    }
    dest[d] = '\0';
}

void get_cell_display_text(int r, int c, char *out, size_t max_len) {
    if (grid[r][c].text[0] == '\0') {
        out[0] = '\0';
    } else if (grid[r][c].is_formula) {
        snprintf(out, max_len, "%g", grid[r][c].value);
    } else {
        strncpy(out, grid[r][c].text, max_len - 1);
        out[max_len - 1] = '\0';
    }
}

void export_pdf(const char *filename) {
    char pdf_name[128];
    if (strchr(filename, '.') == NULL) {
        snprintf(pdf_name, sizeof(pdf_name), "%s.pdf", filename);
    } else {
        snprintf(pdf_name, sizeof(pdf_name), "%s", filename);
    }

    FILE *f = fopen(pdf_name, "wb");
    if (!f) {
        snprintf(status_msg, sizeof(status_msg), " Error: Could not generate PDF. ");
        return;
    }

    recalculate_all();

    // Extract title from filename
    char title_buf[64] = {0};
    const char *base_name = strrchr(pdf_name, '/');
    base_name = base_name ? base_name + 1 : pdf_name;
    strncpy(title_buf, base_name, sizeof(title_buf) - 1);
    char *dot = strrchr(title_buf, '.');
    if (dot) *dot = '\0';
    for (int i = 0; title_buf[i]; i++) title_buf[i] = toupper((unsigned char)title_buf[i]);

    // Find grid bounding box for populated cells
    int max_r = 0, max_c = 0;
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            if (grid[r][c].text[0] != '\0') {
                if (r > max_r) max_r = r;
                if (c > max_c) max_c = c;
            }
        }
    }

    // Dynamic column width calculations
    float pdf_col_widths[MAX_COLS];
    for (int c = 0; c <= max_c; c++) {
        int max_len = 0;
        for (int r = 0; r <= max_r; r++) {
            char disp[64];
            get_cell_display_text(r, c, disp, sizeof(disp));
            int len = (int)strlen(disp);
            if (len > max_len) max_len = len;
        }
        pdf_col_widths[c] = (max_len > 0) ? (max_len * 7.0f + 14.0f) : 40.0f;
    }

    long obj_offsets[6];
    fprintf(f, "%%PDF-1.4\n");

    obj_offsets[1] = ftell(f);
    fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    obj_offsets[2] = ftell(f);
    fprintf(f, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");

    obj_offsets[3] = ftell(f);
    fprintf(f, "3 0 obj\n<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 4 0 R >> >> /MediaBox [0 0 612 792] /Contents 5 0 R >>\nendobj\n");

    obj_offsets[4] = ftell(f);
    fprintf(f, "4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n");

    obj_offsets[5] = ftell(f);
    long header_pos = ftell(f);
    fprintf(f, "5 0 obj\n<< /Length 00000000 >>\nstream\n");
    long content_start = ftell(f);

    // Title Header
    char escaped_title[128];
    escape_pdf_string(title_buf, escaped_title, sizeof(escaped_title));
    fprintf(f, "BT /F1 14 Tf 1 0 0 1 40 750 Tm (%s SPREADSHEET) Tj ET\n", escaped_title);

    float start_y = 720.0f;
    float row_h = 18.0f;

    // Draw Grid Boxes
    for (int r = 0; r <= max_r; r++) {
        float x_acc = 40.0f;
        float y = start_y - (r * row_h);
        for (int c = 0; c <= max_c; c++) {
            float col_w = pdf_col_widths[c];
            fprintf(f, "0.5 w %.1f %.1f %.1f %.1f re S\n", x_acc, y - row_h, col_w, row_h);
            x_acc += col_w;
        }
    }

    // Render Text Contents using absolute matrix positioning (Tm)
    fprintf(f, "BT /F1 9 Tf\n");
    for (int r = 0; r <= max_r; r++) {
        float x_acc = 40.0f;
        float y = start_y - (r * row_h);
        for (int c = 0; c <= max_c; c++) {
            float col_w = pdf_col_widths[c];
            char disp[64];
            get_cell_display_text(r, c, disp, sizeof(disp));
            if (disp[0] != '\0') {
                char escaped_val[128] = {0};
                escape_pdf_string(disp, escaped_val, sizeof(escaped_val));
                fprintf(f, "1 0 0 1 %.1f %.1f Tm (%s) Tj\n", x_acc + 4.0f, y - 13.0f, escaped_val);
            }
            x_acc += col_w;
        }
    }
    fprintf(f, "ET\n");

    long content_end = ftell(f);
    long stream_len = content_end - content_start;

    // Fixed 8-digit padding offset write
    fseek(f, header_pos, SEEK_SET);
    fprintf(f, "5 0 obj\n<< /Length %08ld >>\nstream\n", stream_len);
    fseek(f, content_end, SEEK_SET);
    fprintf(f, "\nendstream\nendobj\n");

    // XRef Table
    long xref_pos = ftell(f);
    fprintf(f, "xref\n0 6\n");
    fprintf(f, "0000000000 65535 f \n");
    for (int i = 1; i <= 5; i++) {
        fprintf(f, "%010ld 00000 n \n", obj_offsets[i]);
    }

    fprintf(f, "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", xref_pos);

    fclose(f);
    snprintf(status_msg, sizeof(status_msg), " Successfully exported PDF to %s ", pdf_name);
}

/* ==================== CLIPBOARD & RANGE OPERATIONS ==================== */

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

/* ==================== DIALOGS, AUTOCOMPLETE & FILE I/O ==================== */

static void autocomplete_path(char *buffer, size_t *len, size_t max_len) {
    char dir_part[256] = ".";
    char file_prefix[128] = "";

    char *last_slash = strrchr(buffer, '/');
    if (last_slash) {
        size_t dir_len = last_slash - buffer;
        if (dir_len == 0) {
            strcpy(dir_part, "/");
        } else {
            strncpy(dir_part, buffer, dir_len);
            dir_part[dir_len] = '\0';
        }
        strcpy(file_prefix, last_slash + 1);
    } else {
        strcpy(file_prefix, buffer);
    }

    DIR *d = opendir(dir_part);
    if (!d) return;

    struct dirent *entry;
    char matches[64][128];
    int is_dir[64];
    int match_count = 0;
    size_t prefix_len = strlen(file_prefix);

    while ((entry = readdir(d)) != NULL && match_count < 64) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        if (strncmp(entry->d_name, file_prefix, prefix_len) == 0) {
            strncpy(matches[match_count], entry->d_name, 127);
            
            char full_path[512];
            if (strcmp(dir_part, ".") == 0) {
                snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
            } else if (strcmp(dir_part, "/") == 0) {
                snprintf(full_path, sizeof(full_path), "/%s", entry->d_name);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", dir_part, entry->d_name);
            }

            struct stat st;
            if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                is_dir[match_count] = 1;
            } else {
                is_dir[match_count] = 0;
            }

            match_count++;
        }
    }
    closedir(d);

    if (match_count == 1) {
        char completed[512];
        if (last_slash) {
            if (strcmp(dir_part, "/") == 0) {
                snprintf(completed, sizeof(completed), "/%s%s", matches[0], is_dir[0] ? "/" : "");
            } else {
                snprintf(completed, sizeof(completed), "%s/%s%s", dir_part, matches[0], is_dir[0] ? "/" : "");
            }
        } else {
            snprintf(completed, sizeof(completed), "%s%s", matches[0], is_dir[0] ? "/" : "");
        }
        strncpy(buffer, completed, max_len - 1);
        *len = strlen(buffer);
    } else if (match_count > 1) {
        char common[128];
        strncpy(common, matches[0], sizeof(common));
        for (int i = 1; i < match_count; i++) {
            int j = 0;
            while (common[j] && matches[i][j] && common[j] == matches[i][j]) {
                j++;
            }
            common[j] = '\0';
        }

        if (strlen(common) > prefix_len) {
            char completed[512];
            if (last_slash) {
                if (strcmp(dir_part, "/") == 0) {
                    snprintf(completed, sizeof(completed), "/%s", common);
                } else {
                    snprintf(completed, sizeof(completed), "%s/%s", dir_part, common);
                }
            } else {
                snprintf(completed, sizeof(completed), "%s", common);
            }
            strncpy(buffer, completed, max_len - 1);
            *len = strlen(buffer);
        }
    }
}

void prompt_input(const char *prompt_str, char *output, size_t max_len) {
    noecho();
    curs_set(1);
    mvprintw(3, 0, "%s", prompt_str);
    clrtoeol();

    size_t i = 0;
    output[0] = '\0';
    int ch_in;

    while (1) {
        ch_in = getch();
        if (ch_in == '\n' || ch_in == '\r' || ch_in == KEY_ENTER) {
            break;
        }
        if (ch_in == '\t') {
            autocomplete_path(output, &i, max_len);
            mvprintw(3, 0, "%s%s", prompt_str, output);
            clrtoeol();
            move(3, (int)strlen(prompt_str) + (int)i);
        } else if (ch_in == KEY_BACKSPACE || ch_in == 127 || ch_in == 8) {
            if (i > 0) {
                i--;
                output[i] = '\0';
                mvprintw(3, 0, "%s%s", prompt_str, output);
                clrtoeol();
                move(3, (int)strlen(prompt_str) + (int)i);
            }
        } else if (i < max_len - 1 && ch_in >= 32 && ch_in < 127) {
            output[i++] = (char)ch_in;
            output[i] = '\0';
            mvprintw(3, (int)strlen(prompt_str) + (int)(i - 1), "%c", ch_in);
        }
        refresh();
    }

    noecho();
    curs_set(0);
}

void load_csv(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        snprintf(status_msg, sizeof(status_msg), " Error: Could not open CSV: %s ", filename);
        return;
    }

    push_undo_state();
    memset(grid, 0, sizeof(grid));
    char line[512];
    int r = 0;

    while (fgets(line, sizeof(line), f) && r < MAX_ROWS) {
        int c = 0;
        char *ptr = line;

        while (*ptr && *ptr != '\r' && *ptr != '\n' && c < MAX_COLS) {
            char cell_buf[64] = {0};
            int idx = 0;
            int in_quotes = 0;

            while (*ptr && idx < (int)sizeof(cell_buf) - 1) {
                if (*ptr == '"') {
                    in_quotes = !in_quotes;
                } else if (*ptr == ',' && !in_quotes) {
                    ptr++;
                    break;
                } else if ((*ptr == '\r' || *ptr == '\n') && !in_quotes) {
                    break;
                } else {
                    cell_buf[idx++] = *ptr;
                }
                ptr++;
            }
            cell_buf[idx] = '\0';

            char *start = cell_buf;
            while (*start == ' ' || *start == '\t') start++;
            char *end = start + strlen(start) - 1;
            while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
                *end = '\0';
                end--;
            }

            if (strlen(start) > 0) {
                strncpy(grid[r][c].text, start, sizeof(grid[r][c].text) - 1);
            }

            c++;
        }
        r++;
    }

    fclose(f);
    recalculate_all();
    snprintf(status_msg, sizeof(status_msg), " Successfully loaded CSV from %s ", filename);
}

void save_csv(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        snprintf(status_msg, sizeof(status_msg), " Error: Could not save CSV file. ");
        return;
    }

    int max_r = 0, max_c = 0;
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            if (grid[r][c].text[0] != '\0') {
                if (r > max_r) max_r = r;
                if (c > max_c) max_c = c;
            }
        }
    }

    for (int r = 0; r <= max_r; r++) {
        for (int c = 0; c <= max_c; c++) {
            if (grid[r][c].text[0] != '\0') {
                if (strchr(grid[r][c].text, ',')) {
                    fprintf(f, "\"%s\"", grid[r][c].text);
                } else {
                    fprintf(f, "%s", grid[r][c].text);
                }
            }
            if (c < max_c) fprintf(f, ",");
        }
        fprintf(f, "\n");
    }

    fclose(f);
    snprintf(status_msg, sizeof(status_msg), " Successfully saved CSV to %s ", filename);
}

void save_sheet(const char *filename) {
    if (strstr(filename, ".csv") || strstr(filename, ".CSV")) {
        save_csv(filename);
        return;
    }

    char final_name[128];
    if (strchr(filename, '.') == NULL) {
        snprintf(final_name, sizeof(final_name), "%s.mc", filename);
    } else {
        snprintf(final_name, sizeof(final_name), "%s", filename);
    }

    FILE *f = fopen(final_name, "w");
    if (!f) {
        snprintf(status_msg, sizeof(status_msg), " Error: Could not open file for writing. ");
        return;
    }

    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            if (grid[r][c].text[0] != '\0') {
                fprintf(f, "%d|%d|%d|%s\n", r, c, grid[r][c].color_state, grid[r][c].text);
            }
        }
    }

    fclose(f);
    snprintf(status_msg, sizeof(status_msg), " Successfully saved to %s ", final_name);
}

void load_sheet(const char *filename) {
    if (strstr(filename, ".csv") || strstr(filename, ".CSV")) {
        load_csv(filename);
        return;
    }

    char final_name[128];
    if (strchr(filename, '.') == NULL) {
        snprintf(final_name, sizeof(final_name), "%s.mc", filename);
    } else {
        snprintf(final_name, sizeof(final_name), "%s", filename);
    }

    FILE *f = fopen(final_name, "r");
    if (!f) {
        snprintf(status_msg, sizeof(status_msg), " Error: Could not open file for reading: %s ", final_name);
        return;
    }

    push_undo_state();
    memset(grid, 0, sizeof(grid));

    int r, c, col_st;
    char buffer[64];
    while (1) {
        int scanned = fscanf(f, "%d|%d|%d|%63[^\n]", &r, &c, &col_st, buffer);
        if (scanned == 4) {
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\r') buffer[len - 1] = '\0';
            if (r >= 0 && r < MAX_ROWS && c >= 0 && c < MAX_COLS) {
                strncpy(grid[r][c].text, buffer, sizeof(grid[r][c].text) - 1);
                grid[r][c].color_state = col_st;
            }
            fgetc(f);
        } else {
            break;
        }
    }

    fclose(f);
    recalculate_all();
    snprintf(status_msg, sizeof(status_msg), " Successfully loaded from %s ", final_name);
}

void prompt_save(void) {
    char filename[128] = {0};
    prompt_input("Save filename [Tab to autocomplete]: ", filename, sizeof(filename));
    if (strlen(filename) > 0) {
        save_sheet(filename);
    } else {
        snprintf(status_msg, sizeof(status_msg), " Save cancelled. ");
    }
}

void prompt_load(void) {
    char filename[128] = {0};
    prompt_input("Open filename [Tab to autocomplete]: ", filename, sizeof(filename));
    if (strlen(filename) > 0) {
        load_sheet(filename);
    } else {
        snprintf(status_msg, sizeof(status_msg), " Open cancelled. ");
    }
}

void prompt_pdf_export(void) {
    char filename[128] = {0};
    prompt_input("Export PDF filename [Tab to autocomplete]: ", filename, sizeof(filename));
    if (strlen(filename) > 0) {
        export_pdf(filename);
    } else {
        snprintf(status_msg, sizeof(status_msg), " Export cancelled. ");
    }
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

/* ==================== RENDERING & INTERACTION ==================== */

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

    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, 0, " microcalc: [%c%d] Val/Formula: %-40s ", 'A' + cursor_c, cursor_r, grid[cursor_r][cursor_c].text);
    mvprintw(1, 0, "%-80s", (status_msg[0] != '\0') ? status_msg : HELP_LINE_1);
    mvprintw(2, 0, "%-80s", HELP_LINE_2);
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

int main(int argc, char *argv[]) {
    initscr();
    raw();
    noqiflush();
    noecho();
    keypad(stdscr, TRUE);
    meta(stdscr, TRUE);
    curs_set(0);

    mousemask(BUTTON1_CLICKED | BUTTON3_CLICKED | BUTTON1_DOUBLE_CLICKED, NULL);
    mouseinterval(250);

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
