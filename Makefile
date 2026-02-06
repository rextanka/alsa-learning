CC = gcc
# Added -g for debug symbols and -O0 to prevent optimization
CFLAGS = -Wall -g -O0
LIBS = -lasound
SRC_DIR = hello-alsa
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/*.c)
BINS = $(patsubst $(SRC_DIR)/%.c, $(BIN_DIR)/%, $(SRCS))

all: $(BINS)

$(BIN_DIR)/%: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

clean:
	rm -rf $(BIN_DIR)