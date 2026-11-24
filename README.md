# BUAA GPA Studio

[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](https://www.gnu.org/licenses/agpl-3.0.html)  ![Made by Wang Nan](https://img.shields.io/badge/Made%20by-Wang%20Nan-brightgreen)  ![Language](https://img.shields.io/badge/Language-C%20%2F%20HTML-orange)  ![Platform](https://img.shields.io/badge/Platform-Windows%20%2F%20Web-blueviolet)

BUAA 成绩和绩点计算工具，提供 Windows 桌面程序与网页版。[`BUAA-Score-Calculate`](https://github.com/theFool-wn/BUAA-Score-Calculate)的重构版本。

> 结果仅供参考，实际成绩与绩点以学校现行规定为准

## 功能特性

- 按成绩组管理课程，可添加、删除、重命名成绩组，添加或删除课程；课程序号随列表自动生成。
- 支持百分制、五级制和两级制课程，输入后实时更新算术平均分、加权平均分、GPA、总学分和有效课程数。
- 支持撤销、重做、手动保存与加载 INI。
- 可设置自动保存和结果小数位数。
- 网页端适配电脑、平板与手机。

## 计算方法

记 $P$ 为有效的百分制课程集合，$F$ 为有效的五级制课程集合，$G$ 为每门有效的百分制或五级制课程的绩点，$C$ 为对应课程学分，$X$ 为每门有效的百分制课程成绩。

平均学分绩点 (GPA)：
$$GPA = \frac{\sum_{i\in P\cup F} G_i \times C_i}{\sum_{i\in P\cup F} C_i}$$

加权平均分：
$$X_w = \frac{\sum_{i\in P} X_i \times C_i}{\sum_{i\in P} C_i}$$

算术平均分：
$$X_a = \frac{1}{|P|}\sum_{i\in P} X_i$$

百分制课程绩点：
$$G = \begin{cases} 4 - \dfrac{3 \times (100-X)^2}{1600}, & X \geq 60 \\[6pt] 0, & X < 60 \end{cases}$$

五级制课程绩点：

| 成绩 | 优秀 | 良好 | 中等 | 及格 | 不及格 |
| :---: | :---: | :---: | :---: | :---: | :---: |
| 绩点 | 4.0 | 3.5 | 2.8 | 1.7 | 0 |

两级制课程 (通过/不通过)：有效成绩计入总学分和有效课程数，不计入 GPA。

## 基本用法

### Windows 桌面端

1. 自行构建或下载 `BUAA GPA Studio.exe`，双击运行。首次启动时，程序在 exe 同目录默认创建 `config.ini`。
2. 点击“新建组”或“添加课程”，填写课程名、学分、成绩，并选择评分制。课程名和成绩可以暂时留空；学分为 0 的课程暂不计入计算。
3. 在底部查看实时结果。“加载”可打开另一份 INI，加载前会处理尚未保存的修改。
4. 结果区可切换自动保存并设置显示小数位数。

| 操作 | 快捷键 |
| --- | --- |
| 保存、加载 | `Ctrl+S`、`Ctrl+O` |
| 新建组、重命名当前组 | `Ctrl+N`、`F2` |
| 撤销、重做 | `Ctrl+Z`、`Ctrl+Y` 或 `Ctrl+Shift+Z` |
| 关于 | `Alt+F1` |

### 网页端

访问 [网页版](https://gpa.wangnan.net)。

网页端与桌面端使用相同的配置文件，使用方法相同。

点击“加载”或顶部的文件状态区域选择 INI，保存可写回该文件。未关联文件时自动保存开关禁用，手动保存会下载当前 INI。

顶部可快捷下载 Windows 桌面端文件。

## 构建与测试

环境要求 `gcc`、`windres` 和 `make`：

```cmd
make
```

生成的 `BUAA GPA Studio.exe` 位于项目根目录。IUP 的头文件和静态库已放在 `src/third_party/iup/`，不需要额外安装 IUP DLL。构建时从 `static/icon.svg` 生成 `work/app.ico`：脚本优先使用已安装的 `ImageMagick`，未安装时使用 PowerShell 的 System.Drawing 回退绘制。

```cmd
make test
node tests/test_web.js
make clean
```

`make test` 运行 C 核心测试；`node tests/test_web.js` 单独运行网页脚本测试，需要 Node.js。`make clean` 清理 `work/` 临时路径，`make release` 编译、运行 C 测试并清理 `work/`，`make distclean` 删除所有构建文件。

## 文件结构

```text
BUAA-GPA-Studio/
├── index.html                网页端界面、样式、计算与 INI 读写
├── Makefile                  桌面端构建、测试和清理规则
├── README.md
├── LICENSE
├── THIRD_PARTY_NOTICES.md    随附组件的许可说明
├── robots.txt
├── sitemap.xml
├── static/
│   ├── icon.svg              项目图标
│   ├── vendor/fontawesome/   本地 Font Awesome 6.7.2 Solid 图标与许可
│   └── vendor/mathjax/
│       ├── tex-svg-full.js   本地 MathJax 3.2.2 公式排版组件
│       └── LICENSE           MathJax 的 Apache-2.0 许可证
├── scripts/
│   └── svg_to_ico.ps1        将 SVG 转换为 Windows ICO
├── src/
│   ├── gpastudio.h           名称、版本、数据结构与模块接口
│   ├── main.c                桌面程序入口和状态生命周期
│   ├── calc.c                成绩验证、绩点映射和统计计算
│   ├── config.c              INI 校验、读取、写入与文件路径
│   ├── state.c               成绩组、课程及撤销/重做状态
│   ├── gui.c                 IUP 窗口、控件和交互回调
│   ├── iup_compat.c          随附 IUP 库的工具链兼容入口
│   ├── resource.rc           exe 图标和版本资源
│   └── third_party/iup/      随附的 IUP 头文件与静态库
└── tests/
    ├── test_core.c           C 核心测试
    ├── test_web.js           网页脚本测试
    └── fixtures/sample.ini   测试用 INI
```

## INI 配置格式

桌面端和网页端使用同一种 UTF-8 INI 格式，写出时带 BOM。节名 `[set1]`、`[set2]` 和课程键 `c1`、`c2` 必须连续编号。每门课程固定为“课程名|学分|成绩|评分制”四个字段。

```ini
[general]
version=1.0.0
sets=1
precision=5
autosave=true

[set1]
name=全部课程
courses=4
c1=高等数学|4.00|95|percent
c2=大学英语|2.00|88|percent
c3=大学物理|3.00|优秀|fivelevel
c4=体育|1.00|通过|passfail
```

| 字段 | 规则 |
| --- | --- |
| `sets` | 成绩组数量 |
| `precision` | INI 可写 0-15；桌面与网页端下拉框提供 0-7 |
| `autosave` | `true` 或 `false`；网页版只有关联可写 INI 后才允许自动写回文件 |
| `courses` | 课程数量 |
| 学分 | 非负有限十进制数，最多两位小数，不接受负号或科学计数法。0 表示暂不参与计算 |
| 评分制 | `percent`、`fivelevel`、`passfail`，分别对应百分制、五级制、两级制 |
| 文本长度 | 组名最多 255 字节，课程名最多 127 字节，成绩最多 63 字节；课程名和成绩允许为空 |

## 注意事项

- 网页草稿存储在浏览器本地。清理浏览器数据、更换浏览器或设备前，请下载 INI。
- 本项目结果仅供参考，不应直接作为正式成绩或评定依据。

## License

This project is licensed under the **[GNU Affero General Public License v3.0 (AGPL-3.0)](https://www.gnu.org/licenses/agpl-3.0.html)**.

You are free to **use, modify, and distribute** this project, including for commercial purposes, provided that:

* You **retain the copyright and license notices**.
* You **distribute modified versions under the same license**.
* You **make the corresponding source code available** as required by the AGPL-3.0, including when modified software is used to provide a network service.

See the [LICENSE](/LICENSE) file for details.

Copyright (c) 2026 Wang Nan
