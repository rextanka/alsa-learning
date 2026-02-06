CC = gcc
CFLAGS = -Wall -g -O0
LIBS = -lasound
SRC_DIR = hello-alsa
BIN_DIR = bin

# Force re-evaluation of wildcards
SRCS = $(wildcard $(SRC_DIR)/*.c)
BINS = $(patsubst $(SRC_DIR)/%.c, $(BIN_DIR)/%, $(SRCS))

.PHONY: all clean

all: $(BINS)

$(BIN_DIR)/%: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

clean:
	rm -rf $(BIN_DIR)