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

static int eval_error = 0;

static int ref_is_error(int r, int c) {
    const Cell *cell = &grid[r][c];
    if (cell->text[0] == '\0') return 0;
    if (cell->text[0] == '=') return cell->is_invalid || cell->is_value_error;
    return !is_pure_number(cell->text);
}

static void set_formula_result(int r, int c, double v) {
    if (eval_error) {
        grid[r][c].is_value_error = 1;
        grid[r][c].value = 0.0;
    } else {
        grid[r][c].value = v;
    }
}

typedef struct {
    double sum, max_val, min_val;
    int count, first;
} RangeAcc;

static void acc_range(RangeAcc *a, int r1, int c1, int r2, int c2) {
    int min_r = (r1 < r2) ? r1 : r2;
    int max_r = (r1 > r2) ? r1 : r2;
    int min_c = (c1 < c2) ? c1 : c2;
    int max_c = (c1 > c2) ? c1 : c2;

    for (int r = min_r; r <= max_r; r++) {
        for (int c = min_c; c <= max_c; c++) {
            if (r == cur_eval_r && c == cur_eval_c) continue;

            if (r >= 0 && r < MAX_ROWS && c >= 0 && c < MAX_COLS) {
                if (grid[r][c].text[0] == '=' && (grid[r][c].is_value_error || grid[r][c].is_invalid)) {
                    eval_error = 1;
                    continue;
                }
                if (grid[r][c].text[0] == '=' ? !grid[r][c].is_invalid : is_pure_number(grid[r][c].text)) {
                    double val = grid[r][c].value;
                    a->sum += val;
                    a->count++;
                    if (a->first) {
                        a->max_val = a->min_val = val;
                        a->first = 0;
                    } else {
                        if (val > a->max_val) a->max_val = val;
                        if (val < a->min_val) a->min_val = val;
                    }
                }
            }
        }
    }
}

static double acc_result(const char *func, const RangeAcc *a) {
    if (strcasecmp(func, "SUM") == 0 || strcmp(func, "s") == 0) return a->sum;
    if (strcasecmp(func, "AVG") == 0 || strcasecmp(func, "AVERAGE") == 0 || strcmp(func, "a") == 0) return (a->count > 0) ? (a->sum / a->count) : 0.0;
    if (strcasecmp(func, "MAX") == 0 || strcmp(func, "M") == 0) return (a->count > 0) ? a->max_val : 0.0;
    if (strcasecmp(func, "MIN") == 0 || strcmp(func, "m") == 0) return (a->count > 0) ? a->min_val : 0.0;
    if (strcasecmp(func, "COUNT") == 0 || strcmp(func, "c") == 0) return (double)a->count;

    return a->sum;
}

static double eval_range(const char *func, int r1, int c1, int r2, int c2) {
    RangeAcc a = { 0.0, 0.0, 0.0, 0, 1 };
    acc_range(&a, r1, c1, r2, c2);
    return acc_result(func, &a);
}

/* Reads "ref" or "ref:ref" items separated by commas, up to the closing parenthesis. */
static int parse_range_list(Parser *p, RangeAcc *a) {
    for (;;) {
        skip_whitespace(p);
        const char *s = p->src + p->pos;
        char c1 = '\0', c2 = '\0';
        int r1 = -1, r2 = -1, n = 0;
        if (sscanf(s, " %c%d : %c%d%n", &c1, &r1, &c2, &r2, &n) != 4) {
            n = 0;
            if (sscanf(s, " %c%d%n", &c1, &r1, &n) != 2) return 0;
            c2 = c1;
            r2 = r1;
        }
        p->pos += n;
        acc_range(a, r1, toupper((unsigned char)c1) - 'A', r2, toupper((unsigned char)c2) - 'A');

        skip_whitespace(p);
        if (p->src[p->pos] == ',') {
            p->pos++;
            continue;
        }
        if (p->src[p->pos] != ')') return 0;
        p->pos++;
        return 1;
    }
}

static double parse_expr(Parser *p);
static double parse_factor(Parser *p);

static double parse_primary(Parser *p) {
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
            RangeAcc acc = { 0.0, 0.0, 0.0, 0, 1 };
            if (parse_range_list(p, &acc)) return acc_result(token, &acc);
        } else {
            int col_idx = toupper((unsigned char)token[0]) - 'A';
            int row_idx = 0;
            while (isdigit((unsigned char)p->src[p->pos])) {
                row_idx = row_idx * 10 + (p->src[p->pos] - '0');
                p->pos++;
            }
            if (row_idx == cur_eval_r && col_idx == cur_eval_c) return 0.0;
            if (col_idx >= 0 && col_idx < MAX_COLS && row_idx >= 0 && row_idx < MAX_ROWS) {
                if (ref_is_error(row_idx, col_idx)) eval_error = 1;
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

static double parse_factor(Parser *p) {
    double val = parse_primary(p);
    for (;;) {
        skip_whitespace(p);
        if (p->src[p->pos] != '%') break;
        p->pos++;
        val /= 100.0;
    }
    return val;
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

static int known_function(const char *name) {
    if (strcasecmp(name, "SUM") == 0 || strcasecmp(name, "AVG") == 0 ||
        strcasecmp(name, "AVERAGE") == 0 || strcasecmp(name, "MAX") == 0 ||
        strcasecmp(name, "MIN") == 0 || strcasecmp(name, "COUNT") == 0) return 1;
    return strcmp(name, "s") == 0 || strcmp(name, "a") == 0 || strcmp(name, "M") == 0 ||
           strcmp(name, "m") == 0 || strcmp(name, "c") == 0;
}

static int ref_in_bounds(char col_letter, int row) {
    int col = toupper((unsigned char)col_letter) - 'A';
    return row >= 0 && row < MAX_ROWS && col >= 0 && col < MAX_COLS;
}

static void v_skip(const char *s, int *p) {
    while (s[*p] == ' ' || s[*p] == '\t' || s[*p] == '\r' || s[*p] == '\n') (*p)++;
}

static int v_expr(const char *s, int *p);

static int v_primary(const char *s, int *p);

static int v_factor(const char *s, int *p) {
    if (!v_primary(s, p)) return 0;
    for (;;) {
        v_skip(s, p);
        if (s[*p] != '%') break;
        (*p)++;
    }
    return 1;
}

static int v_primary(const char *s, int *p) {
    v_skip(s, p);
    char ch = s[*p];

    if (ch == '+' || ch == '-') {
        (*p)++;
        return v_factor(s, p);
    }
    if (ch == '(') {
        (*p)++;
        if (!v_expr(s, p)) return 0;
        v_skip(s, p);
        if (s[*p] != ')') return 0;
        (*p)++;
        return 1;
    }
    if (isalpha((unsigned char)ch)) {
        int start = *p;
        while (isalpha((unsigned char)s[*p])) (*p)++;
        int len = *p - start;
        v_skip(s, p);

        if (s[*p] == '(') {
            char name[16];
            if (len >= 15) return 0;
            memcpy(name, s + start, len);
            name[len] = '\0';
            if (!known_function(name)) return 0;
            (*p)++;

            for (;;) {
                char c1, c2;
                int r1, r2, n = 0;
                if (sscanf(s + *p, " %c%d : %c%d%n", &c1, &r1, &c2, &r2, &n) != 4) {
                    n = 0;
                    if (sscanf(s + *p, " %c%d%n", &c1, &r1, &n) != 2) return 0;
                    c2 = c1;
                    r2 = r1;
                }
                if (!isalpha((unsigned char)c1) || !isalpha((unsigned char)c2)) return 0;
                if (!ref_in_bounds(c1, r1) || !ref_in_bounds(c2, r2)) return 0;
                *p += n;
                v_skip(s, p);
                if (s[*p] == ',') {
                    (*p)++;
                    continue;
                }
                if (s[*p] != ')') return 0;
                (*p)++;
                return 1;
            }
        }

        if (len != 1) return 0;
        if (!isdigit((unsigned char)s[*p])) return 0;
        int row = 0;
        while (isdigit((unsigned char)s[*p])) {
            if (row < MAX_ROWS) row = row * 10 + (s[*p] - '0');
            (*p)++;
        }
        return ref_in_bounds(s[start], row);
    }
    if (isdigit((unsigned char)ch) || ch == '.') {
        char *end;
        const char *begin = s + *p;
        strtod(begin, &end);
        if (end == begin) return 0;
        *p = (int)(end - s);
        return 1;
    }
    return 0;
}

static int v_term(const char *s, int *p) {
    if (!v_factor(s, p)) return 0;
    for (;;) {
        v_skip(s, p);
        char op = s[*p];
        if (op != '*' && op != '/') break;
        (*p)++;
        if (!v_factor(s, p)) return 0;
    }
    return 1;
}

static int v_expr(const char *s, int *p) {
    if (!v_term(s, p)) return 0;
    for (;;) {
        v_skip(s, p);
        char op = s[*p];
        if (op != '+' && op != '-') break;
        (*p)++;
        if (!v_term(s, p)) return 0;
    }
    return 1;
}

int formula_is_valid(const char *expr) {
    char c1, c2, op;
    int r1, r2, n = 0;

    if (sscanf(expr, " %c%d %c %c%d%n", &c1, &r1, &op, &c2, &r2, &n) == 5 &&
        isalpha((unsigned char)c1) && isalpha((unsigned char)c2) && strchr("asMmc", op) &&
        ref_in_bounds(c1, r1) && ref_in_bounds(c2, r2)) {
        int p = n;
        v_skip(expr, &p);
        if (expr[p] == '\0') return 1;
    }

    if (sscanf(expr, " %c%d : %c%d%n", &c1, &r1, &c2, &r2, &n) == 4 &&
        isalpha((unsigned char)c1) && isalpha((unsigned char)c2) &&
        ref_in_bounds(c1, r1) && ref_in_bounds(c2, r2)) {
        int p = n;
        v_skip(expr, &p);
        if (expr[p] == '\0') return 1;
    }

    int p = 0;
    if (!v_expr(expr, &p)) return 0;
    v_skip(expr, &p);
    return expr[p] == '\0';
}

void eval_cell(int r, int c) {
    cur_eval_r = r;
    cur_eval_c = c;
    grid[r][c].is_invalid = 0;
    grid[r][c].is_value_error = 0;
    eval_error = 0;

    if (grid[r][c].text[0] == '=') {
        grid[r][c].is_formula = 1;

        char expr[sizeof grid[r][c].text];
        int k = 0;
        for (const char *s = grid[r][c].text + 1; *s != '\0'; s++) {
            if (*s != '$') expr[k++] = *s;
        }
        expr[k] = '\0';

        if (!formula_is_valid(expr)) {
            grid[r][c].is_invalid = 1;
            grid[r][c].value = 0.0;
            return;
        }

        char c1, c2, temp_op;
        int r1, r2;

        if (sscanf(expr, " %c%d : %c%d", &c1, &r1, &c2, &r2) == 4) {
            int col1 = toupper((unsigned char)c1) - 'A';
            int col2 = toupper((unsigned char)c2) - 'A';
            set_formula_result(r, c, eval_range("SUM", r1, col1, r2, col2));
            return;
        }
        if (sscanf(expr, " %c%d %c %c%d", &c1, &r1, &temp_op, &c2, &r2) == 5 && strchr("asMmc", temp_op)) {
            int col1 = toupper((unsigned char)c1) - 'A';
            int col2 = toupper((unsigned char)c2) - 'A';
            char fn_str[2] = {temp_op, '\0'};
            set_formula_result(r, c, eval_range(fn_str, r1, col1, r2, col2));
            return;
        }

        Parser p = { .src = expr, .pos = 0 };
        set_formula_result(r, c, parse_expr(&p));
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