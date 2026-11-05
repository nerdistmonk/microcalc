#include "microcalc.h"
#include <expat.h>
#include <ctype.h>
#include <stdlib.h>
#include <strings.h>
#include <zlib.h>

/* Gnumeric ValueType codes */
#define GNM_VALUE_BOOLEAN 20
#define GNM_VALUE_INTEGER 30
#define GNM_VALUE_FLOAT 40
#define GNM_VALUE_ERROR 50
#define GNM_VALUE_STRING 60

typedef struct {
    const char *p;
    char out[256];
    size_t n;
    int ok;
} Xlat;

static void xl_emit(Xlat *x, char ch) {
    if (x->n + 1 < sizeof x->out) {
        x->out[x->n++] = ch;
        x->out[x->n] = '\0';
    } else {
        x->ok = 0;
    }
}

static void xl_emit_str(Xlat *x, const char *s) {
    while (*s) xl_emit(x, *s++);
}

static void xl_skip(Xlat *x) {
    while (*x->p == ' ' || *x->p == '\t') x->p++;
}

/* Gnumeric A1 refs are 1-based rows; Microcalc refs are 0-based (A0). */
static int xl_ref(Xlat *x) {
    if (!isalpha((unsigned char)*x->p)) return 0;
    char col = (char)toupper((unsigned char)*x->p);
    if (col < 'A' || col > 'Z') return 0;
    const char *q = x->p + 1;
    if (!isdigit((unsigned char)*q)) return 0;
    long row = 0;
    while (isdigit((unsigned char)*q)) {
        row = row * 10 + (*q - '0');
        if (row > MAX_ROWS) return 0;
        q++;
    }
    if (row < 1 || isalnum((unsigned char)*q) || *q == '_') return 0;
    x->p = q;
    char buf[16];
    snprintf(buf, sizeof buf, "%c%ld", col, row - 1);
    xl_emit_str(x, buf);
    return 1;
}

static int xl_number(Xlat *x) {
    const char *q = x->p;
    int dots = 0, digits = 0;
    while (isdigit((unsigned char)*q) || *q == '.') {
        if (*q == '.') dots++;
        else digits++;
        q++;
    }
    if (dots > 1 || digits == 0 || isalpha((unsigned char)*q)) return 0;
    while (x->p < q) xl_emit(x, *x->p++);
    return 1;
}

static int xl_expr(Xlat *x);

/* Function names accepted by Microcalc, mapped to their canonical spelling. */
static const char *xl_function_name(const char *name, size_t len) {
    static const char *const names[][2] = {
        {"SUM", "SUM"}, {"AVERAGE", "AVG"}, {"AVG", "AVG"},
        {"MIN", "MIN"}, {"MAX", "MAX"}, {"COUNT", "COUNT"},
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strlen(names[i][0]) == len && strncasecmp(names[i][0], name, len) == 0) return names[i][1];
    }
    return NULL;
}

static int xl_function(Xlat *x, const char *name_start, size_t name_len) {
    const char *canonical = xl_function_name(name_start, name_len);
    if (!canonical) return 0;
    x->p += name_len + 1; /* skip name and '(' */
    xl_emit_str(x, canonical);
    xl_emit(x, '(');
    xl_skip(x);
    if (!xl_ref(x)) return 0;
    xl_skip(x);
    if (*x->p == ':') {
        xl_emit(x, ':');
        x->p++;
        xl_skip(x);
        if (!xl_ref(x)) return 0;
        xl_skip(x);
    }
    if (*x->p != ')') return 0;
    xl_emit(x, ')');
    x->p++;
    return 1;
}

static int xl_primary(Xlat *x);

static int xl_factor(Xlat *x) {
    if (!xl_primary(x)) return 0;
    for (;;) {
        xl_skip(x);
        if (*x->p != '%') break;
        xl_emit(x, '%');
        x->p++;
    }
    return 1;
}

static int xl_term(Xlat *x) {
    if (!xl_factor(x)) return 0;
    for (;;) {
        xl_skip(x);
        if (*x->p != '*' && *x->p != '/') break;
        xl_emit(x, *x->p++);
        xl_skip(x);
        if (!xl_factor(x)) return 0;
    }
    return 1;
}

static int xl_expr(Xlat *x) {
    if (!xl_term(x)) return 0;
    for (;;) {
        xl_skip(x);
        if (*x->p != '+' && *x->p != '-') break;
        xl_emit(x, *x->p++);
        xl_skip(x);
        if (!xl_term(x)) return 0;
    }
    return 1;
}

static int xl_primary(Xlat *x) {
    xl_skip(x);
    char c = *x->p;
    if (c == '+' || c == '-') {
        xl_emit(x, c);
        x->p++;
        return xl_factor(x);
    }
    if (c == '(') {
        xl_emit(x, '(');
        x->p++;
        xl_skip(x);
        if (!xl_expr(x)) return 0;
        xl_skip(x);
        if (*x->p != ')') return 0;
        xl_emit(x, ')');
        x->p++;
        return 1;
    }
    if (isdigit((unsigned char)c) || c == '.') return xl_number(x);
    if (isalpha((unsigned char)c)) {
        const char *q = x->p;
        while (isalpha((unsigned char)*q)) q++;
        if (*q == '(') return xl_function(x, x->p, (size_t)(q - x->p));
        return xl_ref(x);
    }
    return 0;
}

/* Translates a Gnumeric formula ("=...") into Microcalc syntax. Returns 0 if any part is unsupported. */
static int translate_formula(const char *src, char *out, size_t out_size) {
    if (src[0] != '=') return 0;
    Xlat x;
    memset(&x, 0, sizeof x);
    x.p = src + 1;
    x.ok = 1;
    xl_emit(&x, '=');
    if (!xl_expr(&x)) return 0;
    xl_skip(&x);
    if (*x.p != '\0' || !x.ok || x.n >= out_size) return 0;
    memcpy(out, x.out, x.n + 1);
    return 1;
}

typedef struct {
    Cell (*target)[MAX_COLS];
    int sheet_count;
    int root_seen, root_ok;
    int target_sheet;
    int in_target_sheet;
    int in_cells;
    int in_cell;
    int has_row, has_col, has_type;
    int row, col, value_type;
    char content[1024];
    size_t content_len;
    int content_overflow;
    int loaded;
    int skipped;
} GnmState;

static const char *local_name(const char *name) {
    const char *sp = strrchr(name, ' ');
    return sp ? sp + 1 : name;
}

/* Gnumeric text often has newlines and padding; a grid cell must be one trimmed line. */
static void collapse_whitespace(char *s) {
    char *out = s;
    int pending_space = 0;
    for (const char *p = s; *p; p++) {
        if ((unsigned char)*p <= ' ') {
            pending_space = out != s;
        } else {
            if (pending_space) *out++ = ' ';
            pending_space = 0;
            *out++ = *p;
        }
    }
    *out = '\0';
}

/* Microcalc's number check rejects exponent notation, so expand the 15-digit mantissa to plain decimal. */
static void format_plain_number(double v, char *out, size_t n) {
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%.14e", v);
    if (!strchr(tmp, 'e')) {
        snprintf(out, n, "%s", tmp);
        return;
    }
    char digits[16];
    int nd = 0;
    const char *p = tmp;
    int neg = (*p == '-');
    if (neg) p++;
    for (; *p && *p != 'e'; p++) {
        if (isdigit((unsigned char)*p)) digits[nd++] = *p;
    }
    digits[nd] = '\0';
    int exp10 = atoi(p + 1);
    char body[512];
    size_t b = 0;
    if (exp10 >= 0) {
        for (int i = 0; i <= exp10; i++) body[b++] = i < nd ? digits[i] : '0';
        if (nd > exp10 + 1) {
            body[b++] = '.';
            for (int i = exp10 + 1; i < nd; i++) body[b++] = digits[i];
        }
    } else {
        body[b++] = '0';
        body[b++] = '.';
        for (int i = 0; i < -exp10 - 1; i++) body[b++] = '0';
        for (int i = 0; i < nd; i++) body[b++] = digits[i];
    }
    body[b] = '\0';
    if (strchr(body, '.')) {
        while (b > 0 && body[b - 1] == '0') body[--b] = '\0';
        if (b > 0 && body[b - 1] == '.') body[--b] = '\0';
    }
    snprintf(out, n, "%s%s", neg ? "-" : "", body);
}

static void store_text(GnmState *st, Cell *cell, const char *text) {
    if (strlen(text) > sizeof cell->text - 1) st->skipped++;
    strncpy(cell->text, text, sizeof cell->text - 1);
    cell->text[sizeof cell->text - 1] = '\0';
    st->loaded++;
}

/* Keeps an untranslated formula as-is so Microcalc flags it as invalid (red).
   Refused if Microcalc would accept it, since its meaning would differ. */
static int keep_untranslated_formula(const char *src, Cell *cell) {
    size_t len = strlen(src);
    if (len > sizeof cell->text - 1) return 0;
    char expr[sizeof cell->text];
    int k = 0;
    for (const char *s = src + 1; *s; s++) {
        if (*s != '$') expr[k++] = *s;
    }
    expr[k] = '\0';
    if (formula_is_valid(expr)) return 0;
    memcpy(cell->text, src, len + 1);
    return 1;
}

static void finish_cell(GnmState *st) {
    if (!st->has_row || !st->has_col) return;
    if (st->row < 0 || st->row >= MAX_ROWS || st->col < 0 || st->col >= MAX_COLS || st->content_overflow) {
        st->skipped++;
        return;
    }
    collapse_whitespace(st->content);
    if (st->content[0] == '\0') return;
    Cell *cell = &st->target[st->row][st->col];

    if (!st->has_type) {
        if (st->content[0] == '=') {
            char translated[256];
            if (translate_formula(st->content, translated, sizeof translated)) {
                store_text(st, cell, translated);
            } else if (keep_untranslated_formula(st->content, cell)) {
                st->loaded++;
            } else {
                st->skipped++;
            }
        } else {
            store_text(st, cell, st->content);
        }
        return;
    }

    switch (st->value_type) {
        case GNM_VALUE_BOOLEAN:
        case GNM_VALUE_ERROR:
            store_text(st, cell, st->content);
            break;
        case GNM_VALUE_INTEGER:
        case GNM_VALUE_FLOAT: {
            char *end = NULL;
            double v = strtod(st->content, &end);
            if (end != st->content && *end == '\0') {
                char num[512];
                format_plain_number(v, num, sizeof num);
                store_text(st, cell, num);
            } else {
                store_text(st, cell, st->content);
            }
            break;
        }
        case GNM_VALUE_STRING:
            /* Text beginning with '=' would be read back as a formula, so it can't be kept. */
            if (st->content[0] == '=') st->skipped++;
            else store_text(st, cell, st->content);
            break;
        default:
            break;
    }
}

static void XMLCALL on_start(void *ud, const XML_Char *name, const XML_Char **atts) {
    GnmState *st = ud;
    const char *n = local_name(name);

    if (!st->root_seen) {
        st->root_seen = 1;
        st->root_ok = strcmp(n, "Workbook") == 0 && strstr(name, "http://www.gnumeric.org/") != NULL;
    }

    if (strcmp(n, "Sheet") == 0) {
        st->sheet_count++;
        if (st->sheet_count == st->target_sheet) st->in_target_sheet = 1;
    } else if (strcmp(n, "Cells") == 0 && st->in_target_sheet) {
        st->in_cells = 1;
    } else if (strcmp(n, "Cell") == 0 && st->in_cells) {
        st->in_cell = 1;
        st->has_row = st->has_col = st->has_type = 0;
        st->content_len = 0;
        st->content_overflow = 0;
        st->content[0] = '\0';
        for (int i = 0; atts[i]; i += 2) {
            const char *an = local_name(atts[i]);
            if (strcmp(an, "Row") == 0) {
                st->row = atoi(atts[i + 1]);
                st->has_row = 1;
            } else if (strcmp(an, "Col") == 0) {
                st->col = atoi(atts[i + 1]);
                st->has_col = 1;
            } else if (strcmp(an, "ValueType") == 0) {
                st->value_type = atoi(atts[i + 1]);
                st->has_type = 1;
            }
        }
    }
}

static void XMLCALL on_end(void *ud, const XML_Char *name) {
    GnmState *st = ud;
    const char *n = local_name(name);

    if (st->in_cell && strcmp(n, "Cell") == 0) {
        finish_cell(st);
        st->in_cell = 0;
    } else if (st->in_cells && strcmp(n, "Cells") == 0) {
        st->in_cells = 0;
    } else if (st->in_target_sheet && strcmp(n, "Sheet") == 0) {
        st->in_target_sheet = 0;
    }
}

static void XMLCALL on_text(void *ud, const XML_Char *s, int len) {
    GnmState *st = ud;
    if (!st->in_cell) return;
    if (st->content_len + (size_t)len < sizeof st->content) {
        memcpy(st->content + st->content_len, s, (size_t)len);
        st->content_len += (size_t)len;
        st->content[st->content_len] = '\0';
    } else {
        st->content_overflow = 1;
    }
}

/* Parses the file, storing cells of target_sheet (1-based; 0 stores none). Returns -1 if it can't be opened, -2 if unreadable. */
static int gnm_parse(const char *filename, int target_sheet, Cell (*fresh)[MAX_COLS], GnmState *out) {
    gzFile gz = gzopen(filename, "rb");
    if (!gz) return -1;

    XML_Parser parser = XML_ParserCreateNS(NULL, ' ');
    if (!parser) {
        gzclose(gz);
        return -2;
    }

    GnmState st;
    memset(&st, 0, sizeof st);
    st.target = fresh;
    st.target_sheet = target_sheet;
    XML_SetUserData(parser, &st);
    XML_SetElementHandler(parser, on_start, on_end);
    XML_SetCharacterDataHandler(parser, on_text);

    int failed = 0;
    char buf[65536];
    for (;;) {
        int len = gzread(gz, buf, sizeof buf);
        if (len < 0) {
            failed = 1;
            break;
        }
        int last = gzeof(gz);
        if (XML_Parse(parser, buf, len, last) == XML_STATUS_ERROR) {
            failed = 1;
            break;
        }
        if (last) break;
    }
    XML_ParserFree(parser);
    gzclose(gz);

    if (failed || !st.root_ok || st.sheet_count == 0) return -2;
    *out = st;
    return 0;
}

int gnumeric_sheet_count(const char *filename) {
    Cell (*scratch)[MAX_COLS] = calloc(MAX_ROWS, sizeof *scratch);
    GnmState st;
    int rc = scratch ? gnm_parse(filename, 0, scratch, &st) : -2;
    free(scratch);
    return rc == 0 ? st.sheet_count : -1;
}

/* Imports one sheet (1-based) of a .gnumeric file (gzip-compressed or plain XML). */
void load_gnumeric(const char *filename, int sheet) {
    if (sheet < 1) {
        snprintf(status_msg, sizeof(status_msg), " Error: Sheet number must be 1 or more ");
        return;
    }

    Cell (*fresh)[MAX_COLS] = calloc(MAX_ROWS, sizeof *fresh);
    if (!fresh) {
        snprintf(status_msg, sizeof(status_msg), " Error: Out of memory while loading Gnumeric file ");
        return;
    }

    GnmState st;
    int rc = gnm_parse(filename, sheet, fresh, &st);
    if (rc == -1) {
        free(fresh);
        snprintf(status_msg, sizeof(status_msg), " Error: Could not open Gnumeric file: %s ", filename);
        return;
    }
    if (rc != 0) {
        free(fresh);
        snprintf(status_msg, sizeof(status_msg), " Error: Could not read Gnumeric file: %s ", filename);
        return;
    }
    if (sheet > st.sheet_count) {
        free(fresh);
        snprintf(status_msg, sizeof(status_msg), " Error: Sheet %d not found, file has %d sheet(s) ", sheet, st.sheet_count);
        return;
    }

    push_undo_state();
    memcpy(grid, fresh, sizeof grid);
    free(fresh);
    recalculate_all();
    snprintf(status_msg, sizeof(status_msg), " Imported sheet %d of %d: %d cells, %d skipped ", sheet, st.sheet_count, st.loaded, st.skipped);
}
