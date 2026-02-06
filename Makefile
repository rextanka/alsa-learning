CC = gcc
CFLAGS = -Wall -g -O0
LIBS = -lasound -lm
BIN_DIR = bin

# 1. Simple tools in hello-alsa (one source file per binary)
HELLO_SRCS = $(wildcard hello-alsa/*.c)
HELLO_BINS = $(patsubst hello-alsa/%.c, $(BIN_DIR)/%, $(HELLO_SRCS))

# 2. Composite tool: Oscillator CLI (Renamed from sine_gen)
# Updated source to osc_cli.c and target binary to osc_cli
OSC_SRCS = osc_cli/osc_cli.c osc_cli/oscillator.c osc_cli/alsa_output.c
OSC_BIN  = $(BIN_DIR)/osc_cli

# Total target list
all: $(HELLO_BINS) $(OSC_BIN)

# Rule for simple tools
$(BIN_DIR)/%: hello-alsa/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

# Rule for our composite Oscillator CLI tool
$(OSC_BIN): $(OSC_SRCS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -rf $(BIN_DIR)