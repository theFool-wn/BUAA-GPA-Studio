/**
 * @             Project: BUAA GPA Studio
 * @                File: gpastudio.h
 * @              Author: WangNan
 * @       Creation Date: 2026.10.14
 * @         Description: Shared data structures, constants and module interfaces.
 * @ Version Information: Created by WangNan, 2026.10.14
 *                        Revised by WangNan, 2026.10.19
 *                        Revised by WangNan, 2026.11.04
 *                        Revised by WangNan, 2026.11.13
 *                        Revised by WangNan, 2026.11.14
**/

#ifndef GPASTUDIO_H
#define GPASTUDIO_H

#include <stdbool.h>
#include <stddef.h>

/* ─────────────────────────── 常量 ─────────────────────────── */

#define APP_NAME        "BUAA GPA Studio"
#define APP_VERSION     "1.0.0"
#define APP_URL         "https://github.com/theFool-wn/BUAA-GPA-Studio"
#define APP_EMAIL       "me@wangnan.net"
#define APP_COPYRIGHT   "Copyright (C) 2026 WangNan"
#define WEB_URL         "https://gpa.wangnan.net"
#define HOMEPAGE_URL    "https://wangnan.net"
#define CONFIG_FILENAME "config.ini"
#define MAX_PATH_LEN    1024
#define MAX_NAME_LEN    256
#define MAX_SCORE_LEN   64
#define MAX_COURSE_NAME 128
#define MAX_CREDIT_TEXT 384      // 足够显示 double 最大有限值的两位小数形式
#define MAX_SETS        1000
#define MAX_COURSES     10000
#define DEFAULT_TAB_MAX 5        // Tab 栏直接显示的最大数量，超出则汉堡菜单
#define DEFAULT_UNDO    50       // 默认撤销深度
#define AUTOSAVE_DELAY  500      // 自动保存防抖时间 (毫秒)

/* ───────────────────────── 评分制 ─────────────────────────── */

typedef enum {
    SCORE_PERCENT   = 0,   // 百分制 0-100
    SCORE_FIVELEVEL = 1,   // 五级制 优秀/良好/中等/及格/不及格
    SCORE_PASSFAIL  = 2    // 两级制 通过/不通过
} ScoreType;

typedef enum {
    FIVE_EXCELLENT = 0,    // 优秀
    FIVE_GOOD      = 1,    // 良好
    FIVE_MEDIUM    = 2,    // 中等
    FIVE_PASS      = 3,    // 及格
    FIVE_FAIL      = 4     // 不及格
} FiveLevel;

/* ─────────────────────── 数据结构 ─────────────────────────── */

/** 一门课程 */
typedef struct {
    char      name[MAX_COURSE_NAME];    // 课程名 (可空字符串)
    double    credit;                   // 学分 (0 表示未填，不参与计算)
    char      score_str[MAX_SCORE_LEN]; // 成绩原始文本
    ScoreType type;                     // 评分制
} Course;

/** 一组成绩 (对应一个 Tab 页) */
typedef struct {
    char    name[MAX_NAME_LEN];  // 组名 / Tab 标题
    Course *courses;             // 课程数组
    int     count;               // 课程数量
    int     capacity;            // 数组容量
} ScoreSet;

/** 单组计算结果 */
typedef struct {
    double avg_grade;      // 算术平均分 (百分制课程)
    double weighted_avg;   // 加权平均分 (百分制课程)
    double gpa;            // GPA (百分制课程 + 五级制课程)
    double total_credit;   // 三种评分制中有效课程的总学分
    int    valid_count;    // 三种评分制中有效课程数
    int    percent_count;  // 参与平均分的百分制课程数
    int    gpa_count;      // 参与 GPA 的百分制和五级制课程数
    bool   has_data;       // 是否有有效课程
} CalcResult;

/** 撤销 / 重做栈节点 (深拷贝整个 sets 数组) */
typedef struct {
    ScoreSet *sets;
    int       count;
} HistorySnapshot;

/** 全局应用状态 */
typedef struct {
    /* 成绩数据 */
    ScoreSet *sets;         // 成绩组数组
    int       set_count;    // 组数量
    int       set_capacity; // 数组容量
    int       current_set;  // 当前选中组索引

    /* 配置 */
    int  precision;                 // 输出小数位数
    bool autosave;                  // 是否自动保存
    int  undo_depth;                // 撤销栈最大深度
    char config_path[MAX_PATH_LEN]; // 配置文件完整路径
    bool config_loaded;             // 是否已从文件加载
    bool dirty;                     // 数据是否已修改未存

    /* 撤销 / 重做 */
    HistorySnapshot *undo_stack;
    int              undo_count;
    HistorySnapshot *redo_stack;
    int              redo_count;

    /* GUI 运行时 (由 gui.c 赋值，业务层不关心具体类型) */
    void *gui;
} AppState;

/* ─────────────────────── 全局实例 ─────────────────────────── */

extern AppState *g_app;

/* ──────────────────── calc.c 接口 ─────────────────────────── */

/** 百分制分数 -> GPA */
double calc_percent_gpa(double score);

/** 判断五级制等级字符串是否合法 */
bool is_valid_fivelevel(const char *str);

/** 五级制等级字符串 -> GPA */
double calc_fivelevel_gpa(const char *level_str);

/** 判断两级制是否为 "通过" */
bool is_pass(const char *str);

/** 判断两级制成绩是否可计入总学分 */
bool is_valid_passfail(const char *str);

/** 将成绩字符串解析为可用于计算的数值 (百分制 = 本身，五级制 / 两级制 = -1表示跳过) */
double score_to_number(const Course *c);

/** 将成绩字符串解析为 GPA (两级制返回 -1 表示跳过) */
double score_to_gpa(const Course *c);

/** 计算一组成绩的所有指标 */
CalcResult calc_set(const ScoreSet *set);

/* ─────────────────── config.c 接口 ─────────────────────────── */

/** 从 INI 文件加载到 g_app，成功返回 true */
bool config_load(const char *path);

/** 将 g_app 保存到 INI 文件，成功返回 true */
bool config_save(const char *path);

/** 获取 exe 所在目录下的默认 config.ini UTF-8 路径，过长或失败时返回 false */
bool config_default_path(char *out_path, size_t len);

/** 检查 UTF-8 路径指向的配置文件是否存在 */
bool config_file_exists(const char *path);

/* ─────────────────── state.c 接口 ─────────────────────────── */

/* —— 深拷贝工具 —— */

/** 深拷贝成绩组数组 */
ScoreSet *state_clone_sets(const ScoreSet *src, int count, int *out_count);

/** 释放成绩组数组 */
void state_free_sets(ScoreSet *sets, int count);

/* ── 初始化 / 释放 ── */

/** 初始化应用状态 (默认一组空成绩)  */
void state_init(void);

/** 释放所有资源 */
void state_free(void);

/* —— 撤销 / 重做 —— */

/** 压入当前状态到撤销栈，内存不足时返回 false 且不改变历史 */
bool state_push_undo(void);

/** 撤销一步，成功返回 true */
bool state_undo(void);

/** 重做一步，成功返回 true */
bool state_redo(void);

/** 是否可以撤销 / 重做 */
bool state_can_undo(void);
bool state_can_redo(void);

/* —— 成绩组操作 —— */

/** 新增一组成绩，返回新组索引 */
int  state_add_set(const char *name);

/** 删除指定索引的组 (带模态确认由 GUI 层调用) */
bool state_remove_set(int index);

/** 重命名组 */
bool state_rename_set(int index, const char *new_name);

/* —— 课程操作 —— */

/** 在指定组末尾添加一门空课程，返回课程索引 */
int  state_add_course(int set_idx);

/** 删除指定组的指定课程 */
bool state_remove_course(int set_idx, int course_idx);

/* ─────────────────── gui.c 接口 ───────────────────────────── */

/** 模态确认框，返回 true = 确认 */
bool gui_confirm(const char *title, const char *msg);

/** 模态输入框，返回 malloc 的字符串，取消返回 NULL */
char *gui_input(const char *title, const char *prompt, const char *initial);

/** 状态栏显示消息 */
void gui_status(const char *fmt, ...);

/** 创建并显示主窗口，进入 IUP 消息循环 */
int  gui_run(int argc, char **argv);

#endif // GPASTUDIO_H
