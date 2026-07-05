CC = gcc
CFLAGS = -Iinclude -Wall -Wextra
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
	$(CC) -o $@ $^

clean:
	-$(RM) $(call FixPath,$(OBJ)) $(TARGET) 2>nul || true
