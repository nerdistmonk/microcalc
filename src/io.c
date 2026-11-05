#include "microcalc.h"
#include <signal.h>
#include <strings.h>

// popen() succeeds even when the command is missing, so success is judged by the exit status
static int run_clipboard_writer(const char *cmd, const char *text) {
    FILE *f = popen(cmd, "w");
    if (!f) return 0;
    fputs(text, f);
    return pclose(f) == 0;
}

static int run_clipboard_reader(const char *cmd, char *buf, size_t max_len) {
    FILE *f = popen(cmd, "r");
    if (!f) return 0;
    size_t total = 0;
    while (total < max_len - 1 && fgets(buf + total, max_len - total, f) != NULL) {
        total = strlen(buf);
    }
    int status = pclose(f);
    return (status == 0 && total > 0);
}

// System clipboard helper: tries wl-copy, xclip, then xsel
void write_system_clipboard(const char *text) {
    // A writer that exits early must not kill the editor with SIGPIPE
    void (*prev_sigpipe)(int) = signal(SIGPIPE, SIG_IGN);
    if (!run_clipboard_writer("wl-copy 2>/dev/null", text) &&
        !run_clipboard_writer("xclip -selection clipboard 2>/dev/null", text)) {
        run_clipboard_writer("xsel -b 2>/dev/null", text);
    }
    signal(SIGPIPE, prev_sigpipe);
}

// System clipboard helper: tries wl-paste, xclip, then xsel
int read_system_clipboard(char *buf, size_t max_len) {
    buf[0] = '\0';
    if (run_clipboard_reader("wl-paste --no-newline 2>/dev/null", buf, max_len)) return 1;
    buf[0] = '\0';
    if (run_clipboard_reader("xclip -out -selection clipboard 2>/dev/null", buf, max_len)) return 1;
    buf[0] = '\0';
    return run_clipboard_reader("xsel -ob 2>/dev/null", buf, max_len);
}

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
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    int r = 0;

    while (r < MAX_ROWS && (line_len = getline(&line, &line_cap, f)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
            line[--line_len] = '\0';
        }

        char *field = malloc((size_t)line_len + 1);
        if (!field) break;

        char *ptr = line;
        int c = 0;
        while (c < MAX_COLS) {
            size_t idx = 0;

            if (*ptr == '"') {
                ptr++;
                while (*ptr) {
                    if (*ptr == '"' && ptr[1] == '"') {
                        field[idx++] = '"';
                        ptr += 2;
                    } else if (*ptr == '"') {
                        ptr++;
                        break;
                    } else {
                        field[idx++] = *ptr++;
                    }
                }
                while (*ptr && *ptr != ',') ptr++;
            } else {
                while (*ptr && *ptr != ',') {
                    field[idx++] = *ptr++;
                }
            }
            field[idx] = '\0';

            char *start = field;
            while (*start == ' ' || *start == '\t') start++;
            size_t n = strlen(start);
            while (n > 0 && (start[n - 1] == ' ' || start[n - 1] == '\t')) {
                start[--n] = '\0';
            }

            if (n > 0) {
                strncpy(grid[r][c].text, start, sizeof(grid[r][c].text) - 1);
            }

            c++;
            if (*ptr == ',') {
                ptr++;
            } else {
                break;
            }
        }

        free(field);
        r++;
    }

    free(line);
    fclose(f);
    recalculate_all();
    snprintf(status_msg, sizeof(status_msg), " Successfully loaded CSV from %s ", filename);
}

static void write_csv_field(FILE *f, const char *text) {
    if (!strpbrk(text, ",\"\r\n")) {
        fputs(text, f);
        return;
    }
    fputc('"', f);
    for (const char *p = text; *p; p++) {
        if (*p == '"') fputc('"', f);
        fputc(*p, f);
    }
    fputc('"', f);
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
            Cell *cell = &grid[r][c];
            if (cell->is_value_error || cell->is_invalid) {
                fprintf(f, "#VALUE!");
            } else if (cell->is_formula) {
                fprintf(f, "%.15g", cell->value);
            } else if (cell->text[0] != '\0') {
                write_csv_field(f, cell->text);
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
            if (grid[r][c].text[0] != '\0' || grid[r][c].color_state != 0) {
                fprintf(f, "%d|%d|%d|%s\n", r, c, grid[r][c].color_state, grid[r][c].text);
            }
        }
    }

    fclose(f);
    snprintf(status_msg, sizeof(status_msg), " Successfully saved to %s ", final_name);
}

int is_gnumeric_name(const char *filename) {
    size_t len = strlen(filename);
    return (len >= 9 && strcasecmp(filename + len - 9, ".gnumeric") == 0) ||
           (len >= 4 && strcasecmp(filename + len - 4, ".gnm") == 0) ||
           (len >= 4 && strcasecmp(filename + len - 4, ".xml") == 0);
}

/* page is the 1-based Gnumeric sheet to load; 0 means not given (asks if the file has several sheets). */
void load_sheet(const char *filename, int page) {
    if (strstr(filename, ".csv") || strstr(filename, ".CSV")) {
        if (page > 0) {
            snprintf(status_msg, sizeof(status_msg), " Error: Page selection only applies to Gnumeric files ");
            return;
        }
        load_csv(filename);
        return;
    }
    if (is_gnumeric_name(filename)) {
        if (page > 0) {
            load_gnumeric(filename, page);
            return;
        }
        int count = gnumeric_sheet_count(filename);
        if (count <= 1) {
            load_gnumeric(filename, 1);
            return;
        }
        char question[96];
        char answer[16] = {0};
        snprintf(question, sizeof(question), "Sheet number to open [1-%d]: ", count);
        prompt_input(question, answer, sizeof(answer));
        if (strlen(answer) == 0) {
            snprintf(status_msg, sizeof(status_msg), " Open cancelled. ");
            return;
        }
        char *end;
        long chosen = strtol(answer, &end, 10);
        if (*end != '\0' || chosen < 1 || chosen > count) {
            snprintf(status_msg, sizeof(status_msg), " Error: Sheet must be a number from 1 to %d ", count);
            return;
        }
        load_gnumeric(filename, (int)chosen);
        return;
    }
    if (page > 0) {
        snprintf(status_msg, sizeof(status_msg), " Error: Page selection only applies to Gnumeric files ");
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
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        int n = 0;
        if (sscanf(line, "%d|%d|%d|%n", &r, &c, &col_st, &n) != 3 || n == 0) break;
        char *text = line + n;
        text[strcspn(text, "\r\n")] = '\0';
        if (r >= 0 && r < MAX_ROWS && c >= 0 && c < MAX_COLS) {
            strncpy(grid[r][c].text, text, sizeof(grid[r][c].text) - 1);
            grid[r][c].color_state = col_st;
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
        load_sheet(filename, 0);
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

static void get_cell_display_text(int r, int c, char *out, size_t max_len) {
    if (grid[r][c].text[0] == '\0') {
        out[0] = '\0';
    } else if (grid[r][c].is_value_error || grid[r][c].is_invalid) {
        strcpy(out, "#VALUE!");
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

    char title_buf[64] = {0};
    const char *base_name = strrchr(pdf_name, '/');
    base_name = base_name ? base_name + 1 : pdf_name;
    strncpy(title_buf, base_name, sizeof(title_buf) - 1);
    char *dot = strrchr(title_buf, '.');
    if (dot) *dot = '\0';
    for (int i = 0; title_buf[i]; i++) title_buf[i] = toupper((unsigned char)title_buf[i]);

    int max_r = 0, max_c = 0;
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            if (grid[r][c].text[0] != '\0') {
                if (r > max_r) max_r = r;
                if (c > max_c) max_c = c;
            }
        }
    }

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

    const float x_start = 40.0f;
    const float x_limit = 612.0f - 40.0f;
    const float start_y = 720.0f;
    const float row_h = 18.0f;
    const float bottom_margin = 36.0f;
    int rows_per_page = (int)((start_y - bottom_margin) / row_h);
    int total_rows = max_r + 1;

    int col_group_first[MAX_COLS], col_group_last[MAX_COLS];
    int n_col_groups = 0;
    int c = 0;
    while (c <= max_c) {
        int first = c;
        float acc = 0.0f;
        while (c <= max_c && (c == first || acc + pdf_col_widths[c] <= x_limit - x_start)) {
            acc += pdf_col_widths[c];
            c++;
        }
        col_group_first[n_col_groups] = first;
        col_group_last[n_col_groups] = c - 1;
        n_col_groups++;
    }

    int n_row_groups = (total_rows + rows_per_page - 1) / rows_per_page;
    int n_pages = n_row_groups * n_col_groups;
    int n_objs = 3 + 2 * n_pages;

    long *obj_offsets = malloc(sizeof(long) * (size_t)(n_objs + 1));
    if (!obj_offsets) {
        fclose(f);
        snprintf(status_msg, sizeof(status_msg), " Error: Could not generate PDF. ");
        return;
    }

    fprintf(f, "%%PDF-1.4\n");

    obj_offsets[1] = ftell(f);
    fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    obj_offsets[2] = ftell(f);
    fprintf(f, "2 0 obj\n<< /Type /Pages /Kids [");
    for (int p = 0; p < n_pages; p++) fprintf(f, " %d 0 R", 4 + 2 * p);
    fprintf(f, " ] /Count %d >>\nendobj\n", n_pages);

    obj_offsets[3] = ftell(f);
    fprintf(f, "3 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n");

    for (int p = 0; p < n_pages; p++) {
        int page_obj = 4 + 2 * p;
        int content_obj = page_obj + 1;
        int row0 = (p / n_col_groups) * rows_per_page;
        int row1 = row0 + rows_per_page;
        if (row1 > total_rows) row1 = total_rows;
        int col0 = col_group_first[p % n_col_groups];
        int col1 = col_group_last[p % n_col_groups];

        obj_offsets[page_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 3 0 R >> >> /MediaBox [0 0 612 792] /Contents %d 0 R >>\nendobj\n", page_obj, content_obj);

        obj_offsets[content_obj] = ftell(f);
        long header_pos = obj_offsets[content_obj];
        fprintf(f, "%d 0 obj\n<< /Length 00000000 >>\nstream\n", content_obj);
        long content_start = ftell(f);

        char page_title[128];
        if (n_pages > 1) {
            snprintf(page_title, sizeof(page_title), "%s SPREADSHEET (PAGE %d OF %d)", title_buf, p + 1, n_pages);
        } else {
            snprintf(page_title, sizeof(page_title), "%s SPREADSHEET", title_buf);
        }
        char escaped_title[192];
        escape_pdf_string(page_title, escaped_title, sizeof(escaped_title));
        fprintf(f, "BT /F1 14 Tf 1 0 0 1 40 750 Tm (%s) Tj ET\n", escaped_title);

        for (int r = row0; r < row1; r++) {
            float x_acc = x_start;
            float y = start_y - ((r - row0) * row_h);
            for (int cc = col0; cc <= col1; cc++) {
                float col_w = pdf_col_widths[cc];
                fprintf(f, "0.5 w %.1f %.1f %.1f %.1f re S\n", x_acc, y - row_h, col_w, row_h);
                x_acc += col_w;
            }
        }

        fprintf(f, "BT /F1 9 Tf\n");
        for (int r = row0; r < row1; r++) {
            float x_acc = x_start;
            float y = start_y - ((r - row0) * row_h);
            for (int cc = col0; cc <= col1; cc++) {
                float col_w = pdf_col_widths[cc];
                char disp[64];
                get_cell_display_text(r, cc, disp, sizeof(disp));
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

        fseek(f, header_pos, SEEK_SET);
        fprintf(f, "%d 0 obj\n<< /Length %08ld >>\nstream\n", content_obj, stream_len);
        fseek(f, content_end, SEEK_SET);
        fprintf(f, "\nendstream\nendobj\n");
    }

    long xref_pos = ftell(f);
    fprintf(f, "xref\n0 %d\n", n_objs + 1);
    fprintf(f, "0000000000 65535 f \n");
    for (int i = 1; i <= n_objs; i++) {
        fprintf(f, "%010ld 00000 n \n", obj_offsets[i]);
    }

    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", n_objs + 1, xref_pos);

    free(obj_offsets);
    fclose(f);
    snprintf(status_msg, sizeof(status_msg), " Successfully exported PDF to %s ", pdf_name);
}