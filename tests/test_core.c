/**
 * @             Project: BUAA GPA Studio
 * @                File: test_core.c
 * @              Author: WangNan
 * @       Creation Date: 2026.10.19
 * @         Description: Regression checks for calculation, history and INI.
 * @ Version Information: Created by WangNan, 2026.10.19
 *                        Revised by WangNan, 2026.11.04
 *                        Revised by WangNan, 2026.11.13
**/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "../src/gpastudio.h"

static int near(double x, double y) { return fabs(x - y) < 0.00001; }

int main(void)
{
    state_init();
    assert(state_add_course(0) == 0);
    Course c = g_app->sets[0].courses[0];
    c.credit = 4;
    strcpy(c.name, "高等数学");
    strcpy(c.score_str, "95");
    g_app->sets[0].courses[0] = c;
    assert(state_add_course(0) == 1);
    c = g_app->sets[0].courses[1];
    c.credit = 3;
    c.type = SCORE_FIVELEVEL;
    strcpy(c.score_str, "优秀");
    g_app->sets[0].courses[1] = c;
    assert(state_add_course(0) == 2);
    c = g_app->sets[0].courses[2];
    c.credit = 2;
    c.type = SCORE_PASSFAIL;
    strcpy(c.score_str, "通过");
    assert(state_push_undo());
    g_app->sets[0].courses[2] = c;
    CalcResult r = calc_set(&g_app->sets[0]);
    assert(r.has_data && r.valid_count == 3 && near(r.total_credit, 9));
    assert(r.percent_count == 1 && r.gpa_count == 2);
    assert(near(r.avg_grade, 95));
    assert(near(r.weighted_avg, 95));
    assert(near(r.gpa, (calc_percent_gpa(95) * 4 + 4 * 3) / 7));

    Course cases[] = {
        {.credit = 2, .type = SCORE_FIVELEVEL, .score_str = "不及格"},
        {.credit = 3, .type = SCORE_PASSFAIL, .score_str = "不通过"},
        {.credit = 4, .type = SCORE_PASSFAIL, .score_str = ""},
        {.credit = 5, .type = SCORE_PERCENT, .score_str = "95junk"},
        {.credit = 0, .type = SCORE_PERCENT, .score_str = "100"}
    };
    ScoreSet separate = {.courses = cases, .count = 5};
    CalcResult limited = calc_set(&separate);
    assert(limited.has_data && limited.valid_count == 2);
    assert(limited.percent_count == 0 && limited.gpa_count == 1);
    assert(near(limited.total_credit, 5) && near(limited.gpa, 0));
    assert(near(limited.avg_grade, 0) && near(limited.weighted_avg, 0));
    separate.courses = &cases[1];
    separate.count = 1;
    limited = calc_set(&separate);
    assert(limited.has_data && limited.valid_count == 1);
    assert(limited.percent_count == 0 && limited.gpa_count == 0);
    assert(near(limited.total_credit, 3));
    assert(state_undo());
    assert(g_app->sets[0].courses[2].type == SCORE_PERCENT);
    assert(state_redo());
    assert(g_app->sets[0].courses[2].type == SCORE_PASSFAIL);
    assert(config_save("work/test.ini"));
    assert(config_load("work/test.ini"));
    assert(g_app->set_count == 1 && g_app->sets[0].count == 3);
    assert(near(calc_set(&g_app->sets[0]).gpa, r.gpa));
    assert(config_load("tests/fixtures/sample.ini"));
    assert(g_app->set_count == 2 && g_app->sets[0].count == 4);
    r = calc_set(&g_app->sets[0]);
    assert(r.valid_count == 4 && near(r.total_credit, 10));
    assert(r.percent_count == 2 && r.gpa_count == 3);
    int before = g_app->set_count;
    FILE *fp = fopen("work/bad.ini", "wb");
    assert(fp);
    fputs("[general]\nsets=1\n[set1]\nname=Bad\ncourses=1\nc999999999999=bad|1|95|percent\n", fp);
    fclose(fp);
    assert(!config_load("work/bad.ini"));
    assert(g_app->set_count == before);
    c = g_app->sets[0].courses[0];
    strcpy(c.score_str, "95junk");
    assert(score_to_number(&c) < 0);
    fp = fopen("work/web-roundtrip.ini", "rb");
    if (fp) {
        fclose(fp);
        assert(config_load("work/web-roundtrip.ini"));
        assert(g_app->precision == 0);
        assert(g_app->sets[0].courses[4].credit == 0);
        assert(g_app->sets[0].courses[4].score_str[0] == 0);
    }

    g_app->sets[0].courses[0].credit = 1.25;
    assert(config_save("work/中文路径.ini"));
    assert(config_file_exists("work/中文路径.ini"));
    assert(config_load("work/中文路径.ini"));
    assert(near(g_app->sets[0].courses[0].credit, 1.25));
    g_app->sets[0].courses[0].credit = 1.234;
    assert(!config_save("work/中文路径.ini"));
    assert(config_load("work/中文路径.ini"));
    assert(near(g_app->sets[0].courses[0].credit, 1.25));

    char tiny[2];
    assert(!config_default_path(tiny, sizeof(tiny)) && tiny[0] == '\0');
    fp = fopen("work/bad-credit.ini", "wb");
    assert(fp);
    fputs("[general]\nsets=1\n[set1]\nname=Bad\ncourses=1\nc1=Test|1.234|95|percent\n", fp);
    fclose(fp);
    assert(!config_load("work/bad-credit.ini"));
    assert(near(g_app->sets[0].courses[0].credit, 1.25));

    state_init();
    assert(state_add_course(0) == 0);
    assert(state_undo());
    assert(g_app->sets[0].count == 0 && g_app->sets[0].capacity == 0);
    assert(g_app->sets[0].courses == NULL);
    assert(state_add_course(0) == 0);
    assert(g_app->sets[0].courses != NULL && !state_can_redo());

    state_init();
    assert(state_add_set("中间组") == 1);
    assert(state_add_set("最后组") == 2);
    assert(state_remove_set(1));
    assert(g_app->current_set == 1);
    assert(strcmp(g_app->sets[1].name, "最后组") == 0);
    state_free();
    puts("core checks passed");
    return 0;
}
