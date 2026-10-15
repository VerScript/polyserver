CC = gcc
CFLAGS = -Iinclude -Isrc -Wall -Wextra -O2
SRC = $(wildcard src/*.c) $(wildcard src/compiler/*.c)
OBJ = $(SRC:.c=.o)

ifeq ($(OS),Windows_NT)
    TARGET = verscript.exe
    RM = del /Q /F
    FixPath = $(subst /,\,$1)
else
    TARGET = verscript
    RM = rm -f
    FixPath = $1
endif

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ -lm

clean:
	-$(RM) $(call FixPath,$(OBJ)) $(TARGET) 2>nul || true
