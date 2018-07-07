EXE = hed

SRC_DIR = src
OBJ_DIR = obj

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

CPPFLAGS += -Iinclude
CFLAGS += -Wall

.PHONY: all clean

all: $(EXE)

debug: CFLAGS += -g -DDEBUG
debug: $(EXE)

install:
	cp $(EXE) /usr/bin

$(EXE): $(OBJ)
	@$(CC) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	@$(RM) $(OBJ)
