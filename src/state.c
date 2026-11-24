/**
 * @             Project: BUAA GPA Studio
 * @                File: state.c
 * @              Author: WangNan
 * @       Creation Date: 2026.10.14
 * @         Description: Dynamic score sets, courses, undo and redo history.
 * @ Version Information: Created by WangNan, 2026.10.14
 *                        Revised by WangNan, 2026.10.19
 *                        Revised by WangNan, 2026.11.13
 *                        Revised by WangNan, 2026.11.21
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpastudio.h"

/* 全局应用状态实例 */
AppState *g_app = NULL;

/* ─────────────────── 深拷贝工具 ─────────────────── */

ScoreSet *state_clone_sets(const ScoreSet *src, int count, int *out_count) {
    if (!src || count <= 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    ScoreSet *dst = calloc((size_t)count, sizeof(ScoreSet));
    if (!dst) return NULL;

    for (int i = 0; i < count; i++) {
        strncpy(dst[i].name, src[i].name, MAX_NAME_LEN - 1);
        dst[i].count = src[i].count;
        /* 空组的数组和容量必须同时为空，下一次添加才会触发扩容。 */
        dst[i].capacity = src[i].count;
        if (src[i].count > 0) {
            dst[i].courses = malloc((size_t)dst[i].capacity * sizeof(Course));
            if (dst[i].courses) {
                memcpy(dst[i].courses, src[i].courses, (size_t)src[i].count * sizeof(Course));
            } else {
                state_free_sets(dst, count);
                return NULL;
            }
        } else {
            dst[i].courses = NULL;
        }
    }
    if (out_count) *out_count = count;
    return dst;
}

void state_free_sets(ScoreSet *sets, int count) {
    if (!sets) return;
    for (int i = 0; i < count; i++) {
        free(sets[i].courses);
        sets[i].courses = NULL;
    }
    free(sets);
}

/* ─────────────────── 初始化 / 释放 ─────────────────── */

void state_init(void) {
    if (g_app) state_free();

    g_app = calloc(1, sizeof(AppState));
    if (!g_app) {
        fprintf(stderr, "FATAL: 无法分配应用状态内存\n");
        exit(1);
    }

    g_app->precision = 4;
    g_app->autosave = true;
    g_app->undo_depth = DEFAULT_UNDO;
    g_app->current_set = 0;
    g_app->dirty = false;

    /* 默认创建一组空成绩 */
    g_app->set_count = 1;
    g_app->set_capacity = 4;
    g_app->sets = calloc((size_t)g_app->set_capacity, sizeof(ScoreSet));
    if (!g_app->sets) exit(1);
    strncpy(g_app->sets[0].name, "全部课程", MAX_NAME_LEN - 1);
    g_app->sets[0].count = 0;
    g_app->sets[0].capacity = 4;
    g_app->sets[0].courses = calloc(4, sizeof(Course));
    if (!g_app->sets[0].courses) exit(1);

    /* 撤销栈 */
    g_app->undo_stack = calloc((size_t)(g_app->undo_depth + 1), sizeof(HistorySnapshot));
    g_app->redo_stack = calloc((size_t)(g_app->undo_depth + 1), sizeof(HistorySnapshot));
    if (!g_app->undo_stack || !g_app->redo_stack) exit(1);
    g_app->undo_count = 0;
    g_app->redo_count = 0;
}

void state_free(void) {
    if (!g_app) return;

    state_free_sets(g_app->sets, g_app->set_count);

    for (int i = 0; i < g_app->undo_count; i++)
        state_free_sets(g_app->undo_stack[i].sets, g_app->undo_stack[i].count);
    free(g_app->undo_stack);

    for (int i = 0; i < g_app->redo_count; i++)
        state_free_sets(g_app->redo_stack[i].sets, g_app->redo_stack[i].count);
    free(g_app->redo_stack);

    free(g_app);
    g_app = NULL;
}

/* ─────────────────── 撤销 / 重做 ─────────────────── */

bool state_push_undo(void) {
    if (!g_app) return false;
    if (g_app->undo_depth <= 0) return true;

    /* 深拷贝当前状态 */
    int cnt = 0;
    ScoreSet *clone = state_clone_sets(g_app->sets, g_app->set_count, &cnt);
    if (!clone) return false;

    /* 栈满则丢弃最旧的 */
    if (g_app->undo_count >= g_app->undo_depth) {
        state_free_sets(g_app->undo_stack[0].sets, g_app->undo_stack[0].count);
        memmove(&g_app->undo_stack[0], &g_app->undo_stack[1],
                (size_t)(g_app->undo_depth - 1) * sizeof(HistorySnapshot));
        g_app->undo_count = g_app->undo_depth - 1;
    }

    g_app->undo_stack[g_app->undo_count].sets = clone;
    g_app->undo_stack[g_app->undo_count].count = cnt;
    g_app->undo_count++;

    /* 任何新修改都清空重做栈 */
    for (int i = 0; i < g_app->redo_count; i++)
        state_free_sets(g_app->redo_stack[i].sets, g_app->redo_stack[i].count);
    g_app->redo_count = 0;
    return true;
}

static bool history_step(HistorySnapshot *from, int *from_count,
                         HistorySnapshot *to, int *to_count) {
    if (!g_app || g_app->undo_depth <= 0 || *from_count <= 0) return false;

    /* 转移当前数组的所有权，不再为撤销/重做额外分配内存。 */
    if (*to_count >= g_app->undo_depth) {
        state_free_sets(to[0].sets, to[0].count);
        memmove(to, to + 1, (size_t)(*to_count - 1) * sizeof(*to));
        --*to_count;
    }
    to[(*to_count)++] = (HistorySnapshot){g_app->sets, g_app->set_count};
    HistorySnapshot previous = from[--*from_count];
    from[*from_count] = (HistorySnapshot){0};
    g_app->sets = previous.sets;
    g_app->set_count = previous.count;
    g_app->set_capacity = previous.count;

    if (g_app->current_set >= g_app->set_count)
        g_app->current_set = g_app->set_count - 1;
    if (g_app->current_set < 0) g_app->current_set = 0;
    g_app->dirty = true;
    return true;
}

bool state_undo(void) {
    if (!g_app) return false;
    return history_step(g_app->undo_stack, &g_app->undo_count,
                        g_app->redo_stack, &g_app->redo_count);
}

bool state_redo(void) {
    if (!g_app) return false;
    return history_step(g_app->redo_stack, &g_app->redo_count,
                        g_app->undo_stack, &g_app->undo_count);
}

bool state_can_undo(void) { return g_app && g_app->undo_count > 0; }
bool state_can_redo(void) { return g_app && g_app->redo_count > 0; }

/* ─────────────────── 成绩组操作 ─────────────────── */

int state_add_set(const char *name) {
    if (!g_app || g_app->set_count >= MAX_SETS) return -1;

    Course *courses = calloc(4, sizeof(Course));
    if (!courses) return -1;

    /* 扩容 */
    if (g_app->set_count >= g_app->set_capacity) {
        int next = g_app->set_capacity + 4;
        ScoreSet *resized = realloc(g_app->sets, (size_t)next * sizeof(ScoreSet));
        if (!resized) {
            free(courses);
            return -1;
        }
        g_app->sets = resized;
        g_app->set_capacity = next;
        for (int i = g_app->set_count; i < g_app->set_capacity; i++) memset(&g_app->sets[i], 0, sizeof(ScoreSet));
    }

    if (!state_push_undo()) {
        free(courses);
        return -1;
    }

    ScoreSet *s = &g_app->sets[g_app->set_count];
    memset(s, 0, sizeof(ScoreSet));
    if (name && name[0]) {
        strncpy(s->name, name, MAX_NAME_LEN - 1);
    } else {
        snprintf(s->name, MAX_NAME_LEN, "成绩组 %d", g_app->set_count + 1);
    }
    s->capacity = 4;
    s->courses = courses;
    s->count = 0;

    int idx = g_app->set_count;
    g_app->set_count++;
    g_app->current_set = idx;
    g_app->dirty = true;
    return idx;
}

bool state_remove_set(int index) {
    if (!g_app || index < 0 || index >= g_app->set_count) return false;
    if (g_app->set_count <= 1) return false;  /* 至少保留一组 */

    if (!state_push_undo()) return false;

    free(g_app->sets[index].courses);
    memmove(&g_app->sets[index], &g_app->sets[index + 1], (size_t)(g_app->set_count - index - 1) * sizeof(ScoreSet));
    g_app->set_count--;

    if (g_app->current_set > index)
        g_app->current_set--;
    else if (g_app->current_set >= g_app->set_count)
        g_app->current_set = g_app->set_count - 1;
    g_app->dirty = true;
    return true;
}

bool state_rename_set(int index, const char *new_name) {
    if (!g_app || index < 0 || index >= g_app->set_count) return false;
    if (!new_name) return false;

    if (!state_push_undo()) return false;
    strncpy(g_app->sets[index].name, new_name, MAX_NAME_LEN - 1);
    g_app->sets[index].name[MAX_NAME_LEN - 1] = '\0';
    g_app->dirty = true;
    return true;
}

/* ─────────────────── 课程操作 ─────────────────── */

int state_add_course(int set_idx) {
    if (!g_app || set_idx < 0 || set_idx >= g_app->set_count || g_app->sets[set_idx].count >= MAX_COURSES) return -1;

    ScoreSet *s = &g_app->sets[set_idx];
    if (s->count >= s->capacity) {
        int next = s->capacity ? s->capacity * 2 : 4;
        if (next > MAX_COURSES) next = MAX_COURSES;
        Course *resized = realloc(s->courses, (size_t)next * sizeof(Course));
        if (!resized) return -1;
        s->courses = resized;
        s->capacity = next;
        for (int i = s->count; i < s->capacity; i++) memset(&s->courses[i], 0, sizeof(Course));
    }

    if (!state_push_undo()) return -1;

    Course *c = &s->courses[s->count];
    memset(c, 0, sizeof(Course));
    c->credit = 1.0;
    c->type = SCORE_PERCENT;
    c->score_str[0] = '\0';

    int idx = s->count;
    s->count++;
    g_app->dirty = true;
    return idx;
}

bool state_remove_course(int set_idx, int course_idx) {
    if (!g_app || set_idx < 0 || set_idx >= g_app->set_count) return false;
    ScoreSet *s = &g_app->sets[set_idx];
    if (course_idx < 0 || course_idx >= s->count) return false;

    if (!state_push_undo()) return false;
    memmove(&s->courses[course_idx], &s->courses[course_idx + 1], (size_t)(s->count - course_idx - 1) * sizeof(Course));
    s->count--;
    g_app->dirty = true;
    return true;
}

