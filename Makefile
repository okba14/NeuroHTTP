CC = gcc
ASM = nasm
ASMFLAGS = -f elf64
CFLAGS = -Wall -Wextra -std=c11 -O3 -march=native -mtune=native -flto -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -no-pie -flto -lpthread -ldl -lm -lcurl -lssl -lcrypto -lnghttp2 -luring
DEBUG_CFLAGS = -Wall -Wextra -std=c11 -g -O0 -DDEBUG -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
               -fsanitize=address -fsanitize=undefined -fstack-protector-strong -fno-omit-frame-pointer
DEBUG_LDFLAGS = -no-pie -lpthread -ldl -lm -lcurl -lssl -lcrypto -lnghttp2 -luring \
                -fsanitize=address -fsanitize=undefined -fstack-protector-strong

SRC_DIR = src
ASM_DIR = src/asm
AI_DIR = src/ai
INCLUDE_DIR = include
AI_INCLUDE_DIR = include/ai
BUILD_DIR = build
BIN_DIR = bin
TEST_DIR = tests
PLUGIN_DIR = plugins
CONFIG_DIR = config

SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
ASM_FILES = $(wildcard $(ASM_DIR)/*.s)
AI_FILES = $(wildcard $(AI_DIR)/*.c)

OBJ_FILES = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC_FILES))
ASM_OBJ_FILES = $(patsubst $(ASM_DIR)/%.s,$(BUILD_DIR)/%.o,$(ASM_FILES))
AI_OBJ_FILES = $(patsubst $(AI_DIR)/%.c,$(BUILD_DIR)/ai/%.o,$(AI_FILES))

TEST_FILES = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJ_FILES = $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/tests/%.o,$(TEST_FILES))
PLUGIN_FILES = $(wildcard $(PLUGIN_DIR)/*.c)
PLUGIN_SO_FILES = $(patsubst $(PLUGIN_DIR)/%.c,$(BUILD_DIR)/plugins/%.so,$(PLUGIN_FILES))

TARGET = $(BIN_DIR)/aionic
DEBUG_TARGET = $(BIN_DIR)/aionic-debug

all: dirs $(TARGET)
debug: dirs $(DEBUG_TARGET)

dirs:
	mkdir -p $(BUILD_DIR) $(BUILD_DIR)/ai $(BUILD_DIR)/tests $(BUILD_DIR)/plugins $(BIN_DIR) logs

$(TARGET): $(OBJ_FILES) $(ASM_OBJ_FILES) $(AI_OBJ_FILES)
	$(CC) $(OBJ_FILES) $(ASM_OBJ_FILES) $(AI_OBJ_FILES) -o $@ $(LDFLAGS)

$(DEBUG_TARGET): CFLAGS = $(DEBUG_CFLAGS)
$(DEBUG_TARGET): LDFLAGS = $(DEBUG_LDFLAGS)
$(DEBUG_TARGET): $(OBJ_FILES) $(ASM_OBJ_FILES) $(AI_OBJ_FILES)
	$(CC) $(OBJ_FILES) $(ASM_OBJ_FILES) $(AI_OBJ_FILES) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(AI_INCLUDE_DIR) -c $< -o $@

$(BUILD_DIR)/ai/%.o: $(AI_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(AI_INCLUDE_DIR) -c $< -o $@

$(BUILD_DIR)/crc32.o: $(ASM_DIR)/crc32.s
	$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD_DIR)/json_fast.o: $(ASM_DIR)/json_fast.s
	$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD_DIR)/memcpy_asm.o: $(ASM_DIR)/memcpy_asm.s
	$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD_DIR)/tests/%.o: $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(AI_INCLUDE_DIR) -c $< -o $@

$(BUILD_DIR)/plugins/%.so: $(PLUGIN_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(AI_INCLUDE_DIR) -fPIC -shared $< -o $@

plugins: $(PLUGIN_SO_FILES)

tests: $(TEST_OBJ_FILES)
	$(CC) $(TEST_OBJ_FILES) $(OBJ_FILES) $(ASM_OBJ_FILES) $(AI_OBJ_FILES) -o $(BIN_DIR)/run_tests $(LDFLAGS)

test: tests
	./$(BIN_DIR)/run_tests

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/aionic
	mkdir -p /etc/aionic
	cp -r $(CONFIG_DIR) /etc/aionic/
	cp -r $(PLUGIN_DIR) /etc/aionic/
	chmod -R 755 /etc/aionic

uninstall:
	rm -f /usr/local/bin/aionic
	rm -rf /etc/aionic

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

rebuild: clean all

run: $(TARGET)
	./$(TARGET)

run-debug: $(DEBUG_TARGET)
	./$(DEBUG_TARGET)

benchmark: $(TARGET)
	@echo "Run: ab -n 10000 -c 100 http://localhost:8080/"

docs:
	doxygen docs/Doxyfile 2>/dev/null || echo "Doxygen not found"

analyze:
	cppcheck --enable=all --std=c11 $(SRC_DIR) $(AI_DIR) 2>/dev/null || echo "cppcheck not found"

format:
	clang-format -i -style=file $(SRC_DIR)/*.c $(AI_DIR)/*.c $(INCLUDE_DIR)/*.h $(AI_INCLUDE_DIR)/*.h 2>/dev/null || echo "clang-format not found"

memcheck: $(DEBUG_TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose ./$(DEBUG_TARGET) 2>/dev/null || echo "valgrind not found"

profile: $(DEBUG_TARGET)
	gprof ./$(DEBUG_TARGET) gmon.out > profile.txt 2>/dev/null || echo "gprof not found"

help:
	@echo "AIONIC Makefile"
	@echo "================"
	@echo "  all         - Build the main executable"
	@echo "  debug       - Build debug version"
	@echo "  plugins     - Build plugins"
	@echo "  tests       - Build and run tests"
	@echo "  test        - Run tests"
	@echo "  install     - Install to system"
	@echo "  uninstall   - Uninstall from system"
	@echo "  clean       - Remove build files"
	@echo "  rebuild     - Clean and rebuild"
	@echo "  run         - Run the server"
	@echo "  run-debug   - Run debug server"
	@echo "  benchmark   - Run benchmarks"
	@echo "  docs        - Generate documentation"
	@echo "  analyze     - Static analysis"
	@echo "  format      - Format code"
	@echo "  memcheck    - Check memory leaks"
	@echo "  profile     - Profile application"
	@echo "  help        - Show this help"

.PHONY: all debug dirs plugins tests test install uninstall clean rebuild run run-debug benchmark docs analyze format memcheck profile help
