CC = gcc
CFLAGS = -Wall -g -O0
LIBS = -lasound -lm
BIN_DIR = bin

# 1. Simple tools in hello-alsa (one source file per binary)
HELLO_SRCS = $(wildcard hello-alsa/*.c)
HELLO_BINS = $(patsubst hello-alsa/%.c, $(BIN_DIR)/%, $(HELLO_SRCS))

# 2. Composite tool in sine_wave (multiple sources for one binary)
SINE_SRCS = sine_wave/sine_gen.c sine_wave/oscillator.c sine_wave/alsa_output.c
SINE_BIN  = $(BIN_DIR)/sine_gen

# Total target list
all: $(HELLO_BINS) $(SINE_BIN)

# Rule for simple tools
$(BIN_DIR)/%: hello-alsa/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

# Rule for our composite Sine Generator
$(SINE_BIN): $(SINE_SRCS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -rf $(BIN_DIR)