# =============================================================================
# toylibc Makefile
#
# 完全不依赖 glibc: freestanding + 自定义 _start + 静态链接
#
# 文件映射:
#   src/start.S    — _start 入口 (汇编, 设 argc/argv → call main → exit)
#   src/syscall.c  — read, write, brk, mmap, munmap, exit (inline asm → syscall)
#   src/string.c   — strlen, memcpy, memset, memcmp (纯用户态)
#   src/heap.c     — malloc, free, calloc, realloc (brk + mmap 双层分配)
#   src/stdio.c    — printf, puts (va_list 格式解析 + write)
#
# 目标:
#   make          → libtoy.a + 测试程序
#   make test     → 构建并运行测试
#   make clean    → 删除所有产物
# =============================================================================

CC       := gcc
AR       := ar
CFLAGS   := -Wall -Wextra -g -O2 -ffreestanding -mno-red-zone
INCLUDES := -I include

BUILD_DIR := build
LIB       := $(BUILD_DIR)/libtoy.a
TEST_BIN  := $(BUILD_DIR)/test

# ---- 库目标文件 ----
LIB_OBJS := $(BUILD_DIR)/syscall.o \
            $(BUILD_DIR)/string.o  \
            $(BUILD_DIR)/heap.o    \
            $(BUILD_DIR)/stdio.o

.PHONY: all clean test examples

all: $(LIB) $(TEST_BIN)

# ---- 静态库 ----
$(LIB): $(LIB_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(AR) rcs $@ $^

# ---- 库 .c → .o ----
$(BUILD_DIR)/%.o: src/%.c include/toylibc.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# ---- 测试程序 ----
# -nostartfiles:  不用 crt0, 用自己的 _start
# -nodefaultlibs: 不自动链接 -lc -lm -lpthread
# -static:        纯静态, 不依赖任何 .so
$(TEST_BIN): $(BUILD_DIR)/start.o $(BUILD_DIR)/test.o $(LIB)
	$(CC) -nostartfiles -nodefaultlibs -static \
	      $^ -o $@

# _start 入口
$(BUILD_DIR)/start.o: src/start.S
	@mkdir -p $(BUILD_DIR)
	$(CC) -c $< -o $@

# 测试 .c → .o
$(BUILD_DIR)/test.o: test/test.c include/toylibc.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# ---- 运行测试 ----
test: $(TEST_BIN)
	@echo "========================================="
	@echo "  Running toylibc test..."
	@echo "========================================="
	@echo ""
	$(TEST_BIN)
	@echo ""
	@echo ">>> exit code: $$?"

# ---- 示例程序 ----
examples: $(LIB)
	@mkdir -p $(BUILD_DIR)
	$(CC) -I include $(CFLAGS) -nostartfiles -nodefaultlibs -static \
	      $(BUILD_DIR)/start.o examples/hello.c $(LIB) -o $(BUILD_DIR)/hello
	@echo "  → build/hello (编译完成)"

clean:
	rm -rf $(BUILD_DIR)
