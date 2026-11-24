/**
 * @             Project: BUAA GPA Studio
 * @                File: calc.c
 * @              Author: WangNan
 * @       Creation Date: 2026.10.14
 * @         Description: Grade conversion and GPA calculation.
 * @ Version Information: Created by WangNan, 2026.10.14
 *                        Revised by WangNan, 2026.10.19
 *                        Revised by WangNan, 2026.11.04
 *                        Revised by WangNan, 2026.11.13
**/

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "gpastudio.h"

/* ─────────────────── 内部工具 ─────────────────── */

/** 去除首尾空白，返回静态缓冲区指针 */
static const char *trim(const char *s) {
    static char buf[MAX_SCORE_LEN];
    if (!s) return "";

    // 跳过前导空白
    while (*s && isspace((unsigned char)*s)) s++;
    strncpy(buf, s, MAX_SCORE_LEN - 1);
    buf[MAX_SCORE_LEN - 1] = '\0';

    // 去除尾部空白
    size_t len = strlen(buf);
    while (len > 0 && isspace((unsigned char)buf[len - 1])) {
        buf[--len] = '\0';
    }
    return buf;
}

/** 不区分大小写比较 */
static bool iequals(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* ─────────────────── 百分制 ─────────────────── */

double calc_percent_gpa(double score) {
    if (score < 60.0) return 0.0;
    if (score > 100.0) score = 100.0;
    return 4.0 - 3.0 * (100.0 - score) * (100.0 - score) / 1600.0;
}

/* ─────────────────── 五级制 ─────────────────── */

/** 五级制等级 -> 枚举 (无法识别返回 -1) */
static int parse_fivelevel(const char *str) {
    const char *s = trim(str);
    if (iequals(s, "优秀") || iequals(s, "excellent") || iequals(s, "a") || iequals(s, "a+"))
        return FIVE_EXCELLENT;
    if (iequals(s, "良好") || iequals(s, "good") || iequals(s, "b") || iequals(s, "b+"))
        return FIVE_GOOD;
    if (iequals(s, "中等") || iequals(s, "medium") || iequals(s, "c") || iequals(s, "c+"))
        return FIVE_MEDIUM;
    if (iequals(s, "及格") || iequals(s, "pass") || iequals(s, "d") || iequals(s, "passing"))
        return FIVE_PASS;
    if (iequals(s, "不及格") || iequals(s, "fail") || iequals(s, "f") || iequals(s, "failing") || iequals(s, "不合格"))
        return FIVE_FAIL;
    return -1;
}

bool is_valid_fivelevel(const char *str) {
    return parse_fivelevel(str) >= 0;
}

double calc_fivelevel_gpa(const char *level_str) {
    static const double gpa_map[] = {4.0, 3.5, 2.8, 1.7, 0.0};
    int lv = parse_fivelevel(level_str);
    if (lv < 0) return 0.0;
    return gpa_map[lv];
}

/* ─────────────────── 两级制 ─────────────────── */

bool is_pass(const char *str) {
    const char *s = trim(str);
    return iequals(s, "通过") || iequals(s, "pass") || iequals(s, "p") || iequals(s, "合格");
}

bool is_valid_passfail(const char *str) {
    if (is_pass(str)) return true;
    const char *s = trim(str);
    return iequals(s, "不通过") || iequals(s, "fail") || iequals(s, "f") || iequals(s, "不合格");
}

/* ─────────────────── 统一转换 ─────────────────── */

static double parse_percent_score(const char *score) {
    char *end = NULL;
    double value = strtod(score, &end);
    if (end == score) return -1.0;
    while (isspace((unsigned char)*end)) ++end;
    if (*end || !isfinite(value) || value < 0.0 || value > 100.0) return -1.0;
    return value;
}

double score_to_number(const Course *c) {
    if (!c) return -1.0;

    switch (c->type) {
        case SCORE_PERCENT:
            return parse_percent_score(c->score_str);
        case SCORE_FIVELEVEL:
        case SCORE_PASSFAIL:
            return -1.0;
    }
    return -1.0;
}

double score_to_gpa(const Course *c) {
    if (!c) return -1.0;

    switch (c->type) {
        case SCORE_PERCENT: {
            double score = parse_percent_score(c->score_str);
            return score < 0.0 ? -1.0 : calc_percent_gpa(score);
        }
        case SCORE_FIVELEVEL:
            if (!is_valid_fivelevel(c->score_str)) return -1.0;
            return calc_fivelevel_gpa(c->score_str);
        case SCORE_PASSFAIL:
            return -1.0;
    }
    return -1.0;
}

/* ─────────────────── 整组计算 ─────────────────── */

CalcResult calc_set(const ScoreSet *set) {
    CalcResult r = {0};
    if (!set || set->count <= 0) {
        r.has_data = false;
        return r;
    }

    double sum_score = 0.0;
    double sum_weight = 0.0;
    double percent_credit = 0.0;
    double sum_gpa = 0.0;
    double gpa_credit = 0.0;

    for (int i = 0; i < set->count; i++) {
        const Course *c = &set->courses[i];
        if (!isfinite(c->credit) || c->credit <= 0.0) continue;

        switch (c->type) {
            case SCORE_PERCENT: {
                double score = score_to_number(c);
                if (score < 0.0) continue;
                sum_score += score;
                sum_weight += score * c->credit;
                percent_credit += c->credit;
                r.percent_count++;
                sum_gpa += calc_percent_gpa(score) * c->credit;
                gpa_credit += c->credit;
                r.gpa_count++;
                break;
            }
            case SCORE_FIVELEVEL: {
                double gpa = score_to_gpa(c);
                if (gpa < 0.0) continue;
                sum_gpa += gpa * c->credit;
                gpa_credit += c->credit;
                r.gpa_count++;
                break;
            }
            case SCORE_PASSFAIL:
                if (!is_valid_passfail(c->score_str)) continue;
                break;
            default:
                continue;
        }
        r.total_credit += c->credit;
        r.valid_count++;
    }

    r.has_data = r.valid_count > 0;
    if (r.percent_count > 0)
        r.avg_grade = sum_score / (double)r.percent_count;
    if (percent_credit > 0.0)
        r.weighted_avg = sum_weight / percent_credit;
    if (gpa_credit > 0.0)
        r.gpa = sum_gpa / gpa_credit;
    return r;
}
