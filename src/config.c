/**
 * @             Project: BUAA GPA Studio
 * @                File: config.c
 * @              Author: WangNan
 * @       Creation Date: 2026.10.14
 * @         Description: Read and write the shared, human readable INI format.
 * @ Version Information: Created by WangNan, 2026.10.14
 *                        Revised by WangNan, 2026.10.19
 *                        Revised by WangNan, 2026.11.11
 *                        Revised by WangNan, 2026.11.13
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include "gpastudio.h"

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

/* 配置路径在模块边界统一使用 UTF-8；Windows 文件 API 接收 UTF-16。 */
#ifdef _WIN32
static wchar_t *wide_path(const char *path) {
    if (!path) return NULL;
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!size) return NULL;
    wchar_t *wide = malloc((size_t)size * sizeof(wchar_t));
    if (!wide) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, size)) {
        free(wide);
        return NULL;
    }
    return wide;
}

static FILE *open_path(const char *path, const wchar_t *mode) {
    wchar_t *wide = wide_path(path);
    if (!wide) return NULL;
    FILE *fp = _wfopen(wide, mode);
    free(wide);
    return fp;
}

static void remove_path(const char *path) {
    wchar_t *wide = wide_path(path);
    if (wide) {
        _wremove(wide);
        free(wide);
    }
}

static bool replace_path(const char *temp, const char *path) {
    wchar_t *wide_temp = wide_path(temp);
    wchar_t *wide_dest = wide_path(path);
    bool ok = wide_temp && wide_dest &&
              MoveFileExW(wide_temp, wide_dest,
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(wide_temp);
    free(wide_dest);
    return ok;
}
#else
static FILE *open_path(const char *path, const char *mode) { return fopen(path, mode); }
static void remove_path(const char *path) { remove(path); }
static bool replace_path(const char *temp, const char *path) { return rename(temp, path) == 0; }
#endif

bool config_file_exists(const char *path) {
    if (!path || !*path) return false;
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    if (!wide) return false;
    DWORD attrs = GetFileAttributesW(wide);
    free(wide);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    fclose(fp);
    return true;
#endif
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) ++s;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

static bool credit_decimal(const char *s, double *out) {
    char *end;
    errno = 0;
    if (!s || !*s) return false;
    int fraction_digits = -1;
    bool has_digit = false;
    for (const char *p = s; *p; ++p) {
        if (*p == '.' && fraction_digits < 0) fraction_digits = 0;
        else if (isdigit((unsigned char)*p)) {
            has_digit = true;
            if (fraction_digits >= 0 && ++fraction_digits > 2) return false;
        } else return false;
    }
    if (!has_digit) return false;
    double v = strtod(s, &end);
    if (end == s || *end || errno == ERANGE || !isfinite(v) || v < 0) return false;
    *out = v;
    return true;
}

static bool credit_text(double value, char out[MAX_CREDIT_TEXT]) {
    if (!isfinite(value) || value < 0 || signbit(value)) return false;
    int written = snprintf(out, MAX_CREDIT_TEXT, "%.2f", value);
    if (written < 0 || written >= MAX_CREDIT_TEXT) return false;
    char *end;
    return strtod(out, &end) == value && *end == '\0';
}

static bool positive_index(const char *s, int max, int *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end || errno == ERANGE || v < 1 || v > max) return false;
    *out = (int)v;
    return true;
}

static bool grow_sets(ScoreSet **sets, int *capacity, int needed) {
    if (needed <= *capacity) return true;
    int next = *capacity ? *capacity : 4;
    while (next < needed) next = next > MAX_SETS / 2 ? MAX_SETS : next * 2;
    ScoreSet *p = realloc(*sets, (size_t)next * sizeof(*p));
    if (!p) return false;
    memset(p + *capacity, 0, (size_t)(next - *capacity) * sizeof(*p));
    *sets = p;
    *capacity = next;
    return true;
}

static bool grow_courses(ScoreSet *set, int needed) {
    if (needed <= set->capacity) return true;
    int next = set->capacity ? set->capacity : 4;
    while (next < needed) next = next > MAX_COURSES / 2 ? MAX_COURSES : next * 2;
    Course *p = realloc(set->courses, (size_t)next * sizeof(*p));
    if (!p) return false;
    memset(p + set->capacity, 0, (size_t)(next - set->capacity) * sizeof(*p));
    set->courses = p;
    set->capacity = next;
    return true;
}

static bool parse_course(char *value, Course *course) {
    char *fields[4];
    fields[0] = value;
    for (int i = 1; i < 4; ++i) {
        char *sep = strchr(fields[i - 1], '|');
        if (!sep) return false;
        *sep = '\0';
        fields[i] = sep + 1;
    }
    if (strchr(fields[3], '|')) return false;
    for (int i = 0; i < 4; ++i) fields[i] = trim(fields[i]);
    if (strlen(fields[0]) >= MAX_COURSE_NAME || strlen(fields[2]) >= MAX_SCORE_LEN) return false;
    memset(course, 0, sizeof(*course));
    strcpy(course->name, fields[0]);
    strcpy(course->score_str, fields[2]);
    if (!credit_decimal(fields[1], &course->credit)) return false;

    if (!strcmp(fields[3], "percent")) course->type = SCORE_PERCENT;
    else if (!strcmp(fields[3], "fivelevel")) course->type = SCORE_FIVELEVEL;
    else if (!strcmp(fields[3], "passfail")) course->type = SCORE_PASSFAIL;
    else return false;

    return true;
}

bool config_load(const char *path) {
    if (!path || !g_app) return false;
    FILE *fp = open_path(path,
#ifdef _WIN32
                         L"rb"
#else
                         "rb"
#endif
    );
    if (!fp) return false;

    ScoreSet *sets = NULL;
    int count = 0, capacity = 0, current = -1;
    int precision = 5, declared_sets = -1, declared_courses = -1;
    bool autosave = true, general_seen = false, ok = true;
    char line[1024];

    while (fgets(line, sizeof(line), fp)) {
        size_t n = strlen(line);
        if (n && line[n - 1] != '\n' && !feof(fp)) {
            ok = false;
            break;
        }
        if (n && line[n - 1] == '\n') line[--n] = '\0';
        if (n && line[n - 1] == '\r') line[--n] = '\0';
        char *p = trim(line);
        if ((unsigned char)p[0] == 0xEF &&
            (unsigned char)p[1] == 0xBB &&
            (unsigned char)p[2] == 0xBF) {
            p += 3;
        }
        if (!*p || *p == ';' || *p == '#') continue;
        if (*p == '[') {
            if (current >= 0 && declared_courses >= 0 && sets[current].count != declared_courses) {
                ok = false;
                break;
            }
            size_t len = strlen(p);
            if (len < 3 || p[len - 1] != ']') {
                ok = false;
                break;
            }
            p[len - 1] = '\0';
            if (!strcmp(p + 1, "general")) {
                general_seen = true;
                current = -1;
            } else if (!strncmp(p + 1, "set", 3)) {
                int number;
                if (!positive_index(p + 4, MAX_SETS, &number) || number != count + 1 || !grow_sets(&sets, &capacity, number)) {
                    ok = false;
                    break;
                }
                current = count++;
                declared_courses = -1;
            } else {
                ok = false;
                break;
            }
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) {
            ok = false;
            break;
        }
        *eq++ = '\0';
        char *key = trim(p), *value = trim(eq);
        if (current < 0) {
            if (!general_seen) {
                ok = false;
                break;
            }
            if (!strcmp(key, "precision")) {
                if (!strcmp(value, "0")) precision = 0;
                else if (!positive_index(value, 15, &precision)) {
                    ok = false;
                    break;
                }
            } else if (!strcmp(key, "sets")) {
                if (!positive_index(value, MAX_SETS, &declared_sets)) {
                    ok = false;
                    break;
                }
            } else if (!strcmp(key, "autosave")) {
                if (!strcmp(value, "true")) autosave = true;
                else if (!strcmp(value, "false")) autosave = false;
                else {
                    ok = false;
                    break;
                }
            }
        } else {
            ScoreSet *set = &sets[current];
            if (!strcmp(key, "name")) {
                if (strlen(value) >= MAX_NAME_LEN) {
                    ok = false;
                    break;
                }
                strcpy(set->name, value);
            } else if (!strcmp(key, "courses")) {
                if (!strcmp(value, "0")) declared_courses = 0;
                else if (!positive_index(value, MAX_COURSES, &declared_courses)) {
                    ok = false;
                    break;
                }
            } else if (key[0] == 'c') {
                int number;
                if (!positive_index(key + 1, MAX_COURSES, &number) ||
                    number != set->count + 1 ||
                    !grow_courses(set, number) ||
                    !parse_course(value, &set->courses[set->count])) {
                    ok = false;
                    break;
                }
                ++set->count;
            }
        }
    }
    if (ferror(fp)) ok = false;
    fclose(fp);

    if (current >= 0 && declared_courses >= 0 && sets[current].count != declared_courses) ok = false;
    if (!general_seen || count < 1 || (declared_sets >= 0 && count != declared_sets)) ok = false;
    for (int i = 0; i < count; ++i)
        if (!sets[i].name[0]) strcpy(sets[i].name, "未命名");
    if (!ok) {
        state_free_sets(sets, count);
        return false;
    }

    state_free_sets(g_app->sets, g_app->set_count);
    g_app->sets         = sets;
    g_app->set_count    = count;
    g_app->set_capacity = capacity;
    g_app->current_set  = 0;
    g_app->precision    = precision;
    g_app->autosave     = autosave;
    g_app->dirty        = false;
    for (int i = 0; i < g_app->undo_count; ++i)
        state_free_sets(g_app->undo_stack[i].sets, g_app->undo_stack[i].count);
    for (int i = 0; i < g_app->redo_count; ++i)
        state_free_sets(g_app->redo_stack[i].sets, g_app->redo_stack[i].count);
    g_app->undo_count = g_app->redo_count = 0;
    return true;
}

static const char *type_name(ScoreType type) {
    switch (type) {
        case SCORE_PERCENT: return "percent";
        case SCORE_FIVELEVEL: return "fivelevel";
        case SCORE_PASSFAIL: return "passfail";
    }
    return "percent";
}

bool config_save(const char *path) {
    if (!path || !g_app || strlen(path) >= MAX_PATH_LEN - 4) return false;
    char temp[MAX_PATH_LEN];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return false;
    FILE *fp = open_path(temp,
#ifdef _WIN32
                         L"wb"
#else
                         "wb"
#endif
    );
    if (!fp) return false;
    bool ok = true;
    if (fwrite("\xEF\xBB\xBF", 1, 3, fp) != 3) ok = false;
    if (fprintf(fp,
        "; BUAA GPA Studio 配置文件\n"
        "; 课程格式：课程名|学分|成绩|类型；学分为 0 的课程不计入统计\n\n"
        "[general]\nversion=%s\nsets=%d\nprecision=%d\nautosave=%s\n\n",
        APP_VERSION, g_app->set_count, g_app->precision,
        g_app->autosave ? "true" : "false") < 0) ok = false;
    for (int i = 0; i < g_app->set_count; ++i) {
        const ScoreSet *set = &g_app->sets[i];
        if (strchr(set->name, '\n') || strchr(set->name, '\r') || strchr(set->name, '=')) ok = false;
        if (fprintf(fp, "[set%d]\nname=%s\ncourses=%d\n", i + 1, set->name, set->count) < 0) ok = false;
        for (int j = 0; j < set->count; ++j) {
            const Course *c = &set->courses[j];
            char credit[MAX_CREDIT_TEXT];
            if (strchr(c->name, '|') || strchr(c->name, '\n') || strchr(c->name, '\r') ||
                strchr(c->score_str, '|') || strchr(c->score_str, '\n') || strchr(c->score_str, '\r') ||
                !credit_text(c->credit, credit)) ok = false;
            else if (fprintf(fp, "c%d=%s|%s|%s|%s\n", j + 1, c->name, credit, c->score_str, type_name(c->type)) < 0) ok = false;
        }
        if (fputc('\n', fp) == EOF) ok = false;
    }
    if (fclose(fp) != 0) ok = false;
    if (!ok) {
        remove_path(temp);
        return false;
    }

    if (!replace_path(temp, path)) {
        remove_path(temp);
        return false;
    }

    g_app->dirty = false;
    g_app->config_loaded = true;
    return true;
}

bool config_default_path(char *out, size_t size) {
    if (!out || !size) return false;
    out[0] = '\0';

#ifdef _WIN32
    wchar_t path[MAX_PATH_LEN];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH_LEN);
    if (!n || n >= MAX_PATH_LEN) return false;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash) slash = wcsrchr(path, L'/');
    if (!slash) return false;
    size_t prefix = (size_t)(slash - path) + 1;
    int name_chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         CONFIG_FILENAME, -1, NULL, 0);
    if (!name_chars || prefix + (size_t)name_chars > MAX_PATH_LEN || size > INT_MAX)
        return false;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                             CONFIG_FILENAME, -1, path + prefix, name_chars)) return false;
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
                                    NULL, 0, NULL, NULL);
    if (!bytes || (size_t)bytes > size) return false;
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
                               out, (int)size, NULL, NULL) == bytes;
#else
    int bytes = snprintf(out, size, "%s", CONFIG_FILENAME);
    if (bytes < 0 || (size_t)bytes >= size) {
        out[0] = '\0';
        return false;
    }
    return true;
#endif
}
