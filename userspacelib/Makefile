# Компилятор С
CC := gcc

# Флаги сборки
CFLAGS ?= -g3 -O0
LDFLAGS ?= -lc -ldl

# Флаги для shared library
SHARED_FLAGS = -shared -fPIC

# Папки
OUT_DIR ?= build
HEADER_DIR ?= ./
SRC_DIR ?= ./
BIN_DIR ?= bin

# Имена файлов
TARGET ?= program
HOOK ?= hook.so

# Автоматический поиск исходников
SOURCES := $(wildcard $(SRC_DIR)/*.c)
SOURCES := $(filter-out $(SRC_DIR)/mallochook.c, $(SOURCES))
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(OUT_DIR)/%.o)

# Файлы для хука
HOOK_SRC = $(SRC_DIR)/mallochook.c
HOOK_OBJ = $(OUT_DIR)/mallochook.o

# Цели
executable: $(BIN_DIR)/$(TARGET) hook

hook: $(BIN_DIR)/$(HOOK)

run: executable
	setarch `uname -m` -R env LD_PRELOAD=$(BIN_DIR)/$(HOOK) $(BIN_DIR)/$(TARGET)

debug: executable
	gdb -ex "skip file env.c" -ex "run" --args env LD_PRELOAD=$(BIN_DIR)/$(HOOK) $(BIN_DIR)/$(TARGET)


# Сборка основной программы
$(BIN_DIR)/$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# Сборка обычных объектных файлов
$(OUT_DIR)/%.o: $(SRC_DIR)/%.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -I$(HEADER_DIR) -c $< -o $@

# Сборка объектного файла для хука
$(HOOK_OBJ): $(HOOK_SRC) | $(OUT_DIR)
	$(CC) $(CFLAGS) -I$(HEADER_DIR) -fPIC -c $< -o $@

# Сборка shared library
$(BIN_DIR)/$(HOOK): $(HOOK_OBJ) | $(BIN_DIR)
	$(CC) $(SHARED_FLAGS) $^ -o $@ $(LDFLAGS)

# Создание папок
$(BIN_DIR) $(OUT_DIR):
	mkdir -p $@

clean:
	rm -rf $(OUT_DIR) $(BIN_DIR) vgcore* *.log

.PHONY: executable run clean hook
