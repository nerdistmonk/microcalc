#include "microcalc.h"

// System clipboard helper: tries wl-copy, xclip, then xsel
void write_system_clipboard(const char *text) {
    FILE *f = popen("wl-copy", "w");
    if (!f) {
        f = popen("xclip -selection clipboard", "w");
    }
    if (!f) {
        f = popen("xsel -b", "w");
    }

    if (f) {
        fputs(text, f);
        pclose(f);
    }
}

// System clipboard helper: tries wl-paste, xclip, then xsel
int read_system_clipboard(char *buf, size_t max_len) {
    FILE *f = popen("wl-paste --no-newline", "r");
    if (!f) {
        f = popen("xclip -out -selection clipboard", "r");
    }
    if (!f) {
        f = popen("xsel -ob", "r");
    }

    if (f) {
        size_t total = 0;
        while (total < max_len - 1 && fgets(buf + total, max_len - total, f) != NULL) {
            total = strlen(buf);
        }
        pclose(f);
        return (total > 0);
    }
    return 0;
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

    char escaped_title[128];
    escape_pdf_string(title_buf, escaped_title, sizeof(escaped_title));
    fprintf(f, "BT /F1 14 Tf 1 0 0 1 40 750 Tm (%s SPREADSHEET) Tj ET\n", escaped_title);

    float start_y = 720.0f;
    float row_h = 18.0f;

    for (int r = 0; r <= max_r; r++) {
        float x_acc = 40.0f;
        float y = start_y - (r * row_h);
        for (int c = 0; c <= max_c; c++) {
            float col_w = pdf_col_widths[c];
            fprintf(f, "0.5 w %.1f %.1f %.1f %.1f re S\n", x_acc, y - row_h, col_w, row_h);
            x_acc += col_w;
        }
    }

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

    fseek(f, header_pos, SEEK_SET);
    fprintf(f, "5 0 obj\n<< /Length %08ld >>\nstream\n", stream_len);
    fseek(f, content_end, SEEK_SET);
    fprintf(f, "\nendstream\nendobj\n");

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