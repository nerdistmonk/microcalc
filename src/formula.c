#include "microcalc.h"

static int cur_eval_r = -1;
static int cur_eval_c = -1;

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
    int has_digit = 0, has_dot = 0, has_sign = 0, has_dollar = 0, has_percent = 0;
    int group = 0, commas = 0;
    for (; *str; str++) {
        char ch = *str;
        if (isdigit((unsigned char)ch)) {
            has_digit = 1;
            if (!has_dot) group++;
        } else if (ch == '.') {
            if (has_dot) return 0;
            has_dot = 1;
        } else if (ch == ',') {
            if (!has_digit || has_dot || group == 0 || group > 3) return 0;
            if (commas > 0 && group != 3) return 0;
            commas++;
            group = 0;
        } else if (ch == '-' || ch == '+' || ch == '(') {
            if (has_digit || has_sign) return 0;
            has_sign = 1;
        } else if (ch == '$' || (ch == '\xE2' && str[1] == '\x82' && str[2] == '\xAC')) {
            if (has_digit || has_dollar) return 0;
            has_dollar = 1;
            if (ch != '$') str += 2;
        } else if (ch == '%') {
            if (!has_digit || has_percent) return 0;
            has_percent = 1;
            for (const char *q = str + 1; *q; q++) {
                if (*q != ' ') return 0;
            }
            break;
        } else if (ch == ')') {
            if (str[1] != '\0') return 0;
        } else if (ch == ' ') {
            if (has_digit) {
                for (const char *q = str; *q; q++) {
                    if (*q != ' ') return 0;
                }
                break;
            }
        } else {
            return 0;
        }
    }
    if (commas > 0 && group != 3) return 0;
    return has_digit;
}

int is_plain_number(const char *str) {
    return is_pure_number(str) && !strchr(str, '%') && !strstr(str, "\xE2\x82\xAC");
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

static int eval_error = 0;
static int eval_tilt = 0;

static int ref_is_error(int r, int c) {
    const Cell *cell = &grid[r][c];
    if (cell->text[0] == '\0') return 0;
    if (cell->text[0] == '=') return cell->is_invalid || cell->is_value_error;
    return !is_pure_number(cell->text);
}

static void set_formula_result(int r, int c, double v) {
    if (eval_error) {
        grid[r][c].is_value_error = 1;
        grid[r][c].is_tilt = eval_tilt;
        grid[r][c].value = 0.0;
    } else {
        grid[r][c].value = v;
    }
}

typedef struct {
    double sum, max_val, min_val, prod, seq;
    int count;
    char seq_op;
} RangeAcc;

/* seq_op selects the ordered fold kept in seq: 'S' subtracts each later value,
   'd' divides by each later value, anything else leaves seq unused. */
static void acc_init(RangeAcc *a, char seq_op) {
    a->sum = a->max_val = a->min_val = a->seq = 0.0;
    a->prod = 1.0;
    a->count = 0;
    a->seq_op = seq_op;
}

static void acc_cell(RangeAcc *a, int r, int c) {
    if (r == cur_eval_r && c == cur_eval_c) return;
    if (r < 0 || r >= MAX_ROWS || c < 0 || c >= MAX_COLS) return;

    if (grid[r][c].text[0] == '=' && (grid[r][c].is_value_error || grid[r][c].is_invalid)) {
        eval_error = 1;
        return;
    }
    if (grid[r][c].text[0] != '\0' && grid[r][c].text[0] != '=' && !is_pure_number(grid[r][c].text)) {
        eval_error = 1;
        return;
    }
    if (grid[r][c].text[0] == '=' ? !grid[r][c].is_invalid : is_pure_number(grid[r][c].text)) {
        double val = grid[r][c].value;
        a->sum += val;
        a->prod *= val;
        if (a->count == 0) {
            a->max_val = a->min_val = a->seq = val;
        } else {
            if (val > a->max_val) a->max_val = val;
            if (val < a->min_val) a->min_val = val;
            if (a->seq_op == 'S') a->seq -= val;
            else if (a->seq_op == 'd') {
                if (val == 0.0) { eval_tilt = 1; eval_error = 1; }
                a->seq = (val != 0.0) ? (a->seq / val) : 0.0;
            }
        }
        a->count++;
    }
}

static void acc_range(RangeAcc *a, int r1, int c1, int r2, int c2) {
    int min_r = (r1 < r2) ? r1 : r2;
    int max_r = (r1 > r2) ? r1 : r2;
    int min_c = (c1 < c2) ? c1 : c2;
    int max_c = (c1 > c2) ? c1 : c2;

    for (int r = min_r; r <= max_r; r++) {
        for (int c = min_c; c <= max_c; c++) {
            acc_cell(a, r, c);
        }
    }
}

static double acc_result(const char *func, const RangeAcc *a) {
    if (strcmp(func, "a") == 0) return (a->count > 0) ? (a->sum / a->count) : 0.0;
    if (strcmp(func, "M") == 0) return (a->count > 0) ? a->max_val : 0.0;
    if (strcmp(func, "m") == 0) return (a->count > 0) ? a->min_val : 0.0;
    if (strcmp(func, "c") == 0) return (double)a->count;
    return a->sum;
}

static int ref_in_bounds(char col_letter, int row) {
    int col = toupper((unsigned char)col_letter) - 'A';
    return row >= 0 && row < MAX_ROWS && col >= 0 && col < MAX_COLS;
}

/* "A0 a A2": one of a m M c applied to exactly two cells. */
static double pair_op(char op, int r1, int c1, int r2, int c2) {
    RangeAcc a;
    acc_init(&a, 0);
    acc_cell(&a, r1, c1);
    acc_cell(&a, r2, c2);
    char fn[2] = {op, '\0'};
    return acc_result(fn, &a);
}

/* Applies op over the rectangle between two cells. Ordered folds (S, d) start at the
   top-left cell and step through the rest in row-major order. */
static double range_op(char op, int r1, int c1, int r2, int c2) {
    RangeAcc a;
    acc_init(&a, op);
    acc_range(&a, r1, c1, r2, c2);
    if (op == 'x') return (a.count > 0) ? a.prod : 0.0;
    if (op == 'S' || op == 'd') return (a.count > 0) ? a.seq : 0.0;
    char fn[2] = {op, '\0'};
    return acc_result(fn, &a);
}

/* One left-to-right evaluator serves both shorthand systems. There is no operator
   precedence: operators apply in the order written, and parentheses group.
   Operators: s + (add), S - (subtract), x * (multiply), d / (divide), and the math
   forms + - * /. Operands: a number, a cell, a parenthesized group, cell r<op> cell,
   or cell <a|m|M|c> cell. Any operand may take a leading + or - and a trailing %.
   With eval = 0 the same walk only checks syntax. */
typedef struct {
    const char *s;
    int p;
    int eval;
} Ev;

static int ev_expr(Ev *e, double *out);

static void ev_skip(Ev *e) {
    while (e->s[e->p] == ' ' || e->s[e->p] == '\t') e->p++;
}

static int ev_cell(Ev *e, int *row, int *col) {
    ev_skip(e);
    char letter = e->s[e->p];
    if (!isalpha((unsigned char)letter) || !isdigit((unsigned char)e->s[e->p + 1])) return 0;
    int p = e->p + 1, r = 0;
    while (isdigit((unsigned char)e->s[p])) {
        if (r < MAX_ROWS) r = r * 10 + (e->s[p] - '0');
        p++;
    }
    if (!ref_in_bounds(letter, r)) return 0;
    e->p = p;
    *row = r;
    *col = toupper((unsigned char)letter) - 'A';
    return 1;
}

static int ev_reference(Ev *e, double *out) {
    int r1, c1, r2, c2;
    if (!ev_cell(e, &r1, &c1)) return 0;
    ev_skip(e);
    char op = e->s[e->p];

    if (op == 'r') {
        char rop = e->s[e->p + 1];
        if (rop == '*') rop = 'x';
        if (rop == '/') rop = 'd';
        if (rop == '\0' || strchr("sSxdaMmc", rop) == NULL) return 0;
        e->p += 2;
        if (!ev_cell(e, &r2, &c2)) return 0;
        *out = e->eval ? range_op(rop, r1, c1, r2, c2) : 0.0;
        return 1;
    }
    if (op != '\0' && strchr("aMmc", op) != NULL) {
        e->p++;
        if (!ev_cell(e, &r2, &c2)) return 0;
        *out = e->eval ? pair_op(op, r1, c1, r2, c2) : 0.0;
        return 1;
    }

    *out = 0.0;
    if (!e->eval) return 1;
    if (r1 == cur_eval_r && c1 == cur_eval_c) return 1;
    if (ref_is_error(r1, c1)) eval_error = 1;
    *out = grid[r1][c1].value;
    return 1;
}

static int ev_operand(Ev *e, double *out) {
    ev_skip(e);
    char ch = e->s[e->p];

    if (ch == '+' || ch == '-') {
        e->p++;
        if (!ev_operand(e, out)) return 0;
        if (ch == '-') *out = -*out;
        return 1;
    }
    if (ch == '(') {
        e->p++;
        if (!ev_expr(e, out)) return 0;
        ev_skip(e);
        if (e->s[e->p] != ')') return 0;
        e->p++;
    } else if (ch == 'p' && !isdigit((unsigned char)e->s[e->p + 1])) {
        /* p<cell> opens a group that runs to the end of the formula or enclosing parenthesis */
        e->p++;
        if (!ev_expr(e, out)) return 0;
    } else if (isdigit((unsigned char)ch) || ch == '.') {
        const char *begin = e->s + e->p;
        char *end;
        double v = strtod(begin, &end);
        if (end == begin) return 0;
        e->p = (int)(end - e->s);
        *out = e->eval ? v : 0.0;
    } else if (isalpha((unsigned char)ch)) {
        if (!ev_reference(e, out)) return 0;
    } else {
        return 0;
    }

    for (;;) {
        ev_skip(e);
        if (e->s[e->p] != '%') break;
        e->p++;
        *out /= 100.0;
    }
    return 1;
}

static int ev_expr(Ev *e, double *out) {
    double acc;
    if (!ev_operand(e, &acc)) return 0;
    for (;;) {
        ev_skip(e);
        char op = e->s[e->p];
        if (op == '\0' || strchr("sSxd+-*/", op) == NULL) break;
        e->p++;
        double right;
        if (!ev_operand(e, &right)) return 0;
        if (op == 's' || op == '+') acc += right;
        else if (op == 'S' || op == '-') acc -= right;
        else if (op == 'x' || op == '*') acc *= right;
        else if (right != 0.0) acc = acc / right;
        else {
            if (e->eval) { eval_tilt = 1; eval_error = 1; }
            acc = 0.0;
        }
    }
    *out = acc;
    return 1;
}

int formula_is_valid(const char *expr) {
    Ev e = { expr, 0, 0 };
    double unused;
    if (!ev_expr(&e, &unused)) return 0;
    ev_skip(&e);
    return expr[e.p] == '\0';
}

void eval_cell(int r, int c) {
    cur_eval_r = r;
    cur_eval_c = c;
    grid[r][c].is_invalid = 0;
    grid[r][c].is_value_error = 0;
    grid[r][c].is_tilt = 0;
    eval_error = 0;
    eval_tilt = 0;

    if (grid[r][c].text[0] == '=') {
        grid[r][c].is_formula = 1;

        const char *expr = grid[r][c].text + 1;

        if (!formula_is_valid(expr)) {
            grid[r][c].is_invalid = 1;
            grid[r][c].value = 0.0;
            return;
        }

        Ev e = { expr, 0, 1 };
        double v = 0.0;
        ev_expr(&e, &v);
        set_formula_result(r, c, v);
    } else {
        grid[r][c].is_formula = 0;
        if (is_pure_number(grid[r][c].text)) {
            double v = parse_number(grid[r][c].text);
            grid[r][c].value = strchr(grid[r][c].text, '%') ? v / 100.0 : v;
        } else {
            grid[r][c].value = 0.0;
        }
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