# BUAA GPA Studio -- Windows / MinGW-w64 / GNU Make
.DEFAULT_GOAL := all
SHELL := cmd.exe
.SHELLFLAGS := /C

CC := gcc
WINDRES := x86_64-w64-mingw32-windres
POWERSHELL := $(shell where pwsh.exe >nul 2>&1 && echo pwsh.exe || echo powershell.exe)

IUP_DIR := src/third_party/iup
WORK_DIR := work
OBJ_DIR := $(WORK_DIR)/obj
TARGET := BUAA\ GPA\ Studio.exe
SVG := static/icon.svg
ICO := $(WORK_DIR)/app.ico
RESOURCE := $(OBJ_DIR)/resource.o
TEST_EXE := $(WORK_DIR)/test_core.exe

SOURCES := src/main.c src/calc.c src/config.c src/state.c src/gui.c src/iup_compat.c
OBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
TEST_OBJECTS := $(OBJ_DIR)/test_core.o $(OBJ_DIR)/calc.o $(OBJ_DIR)/config.o $(OBJ_DIR)/state.o

CFLAGS := -std=c11 -Os -Wall -Wextra -Wno-cast-function-type -DUNICODE -D_UNICODE -Isrc -I$(IUP_DIR)/include
LDFLAGS := -Os -s -mwindows -static-libgcc -L$(IUP_DIR)
LDLIBS := -liup -lgdi32 -lcomctl32 -lole32 -luuid -luser32 -lkernel32

.PHONY: all test release clean distclean
all: $(TARGET)

# Build the Windows GUI application.
$(TARGET): $(OBJECTS) $(RESOURCE)
	$(CC) $(LDFLAGS) -o "$@" $(OBJECTS) $(RESOURCE) $(LDLIBS)

# Compile normal C sources; the shared header is an explicit dependency.
$(OBJ_DIR)/%.o: src/%.c src/gpastudio.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

# Generate the ICO from the single SVG source; PS1 chooses ImageMagick or fallback.
$(ICO): $(SVG) scripts/svg_to_ico.ps1 | $(WORK_DIR)
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File "scripts/svg_to_ico.ps1" -SvgPath "$(SVG)" -IcoPath "$(ICO)"

# resource.rc refers explicitly to the generated work/app.ico.
$(RESOURCE): src/resource.rc $(ICO) | $(OBJ_DIR)
	$(WINDRES) -I. -Isrc -O coff "src/resource.rc" -o "$@"

# Standalone console test: no GUI main() and no -mwindows.
$(OBJ_DIR)/test_core.o: tests/test_core.c src/gpastudio.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

$(TEST_EXE): $(TEST_OBJECTS) tests/fixtures/sample.ini | $(WORK_DIR)
	$(CC) -o "$@" $(TEST_OBJECTS) -lm

test: $(TEST_EXE)
	"$(TEST_EXE)"

# Build + test, then delete all generated files except the final EXE.
release: all test
	$(MAKE) clean

$(WORK_DIR) $(OBJ_DIR):
	@if not exist "$@" mkdir "$@"

# Preserve the final EXE; delete all intermediate files and test artifacts.
clean:
	@if exist "$(WORK_DIR)" rmdir /s /q "$(WORK_DIR)"

# Delete the EXE as well.
distclean: clean
	@if exist "BUAA GPA Studio.exe" del /q "BUAA GPA Studio.exe"
