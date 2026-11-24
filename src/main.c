/**
 * @             Project: BUAA GPA Studio
 * @                File: main.c
 * @              Author: WangNan
 * @       Creation Date: 2026.10.14
 * @         Description: Desktop application entry point and state lifetime.
 * @ Version Information: Created by WangNan, 2026.10.14
**/

#include "gpastudio.h"

int main(int argc, char **argv) {
    state_init();
    int ret = gui_run(argc, argv);
    state_free();
    return ret;
}
