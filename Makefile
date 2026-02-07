CC = gcc
CFLAGS = -Wall -g -O0
LIBS = -lasound -lm
BIN_DIR = bin

# --- Project: osc_cli_adsr ---
ADSR_DIR = osc_cli_adsr
ADSR_BIN = $(BIN_DIR)/osc_cli_adsr

# List of object files for the ADSR project
ADSR_OBJS = $(ADSR_DIR)/osc_cli.o \
            $(ADSR_DIR)/oscillator.o \
            $(ADSR_DIR)/alsa_output.o \
            $(ADSR_DIR)/envelope.o

# --- Project: Legacy osc_cli ---
OSC_DIR = osc_cli
OSC_BIN = $(BIN_DIR)/osc_cli
OSC_OBJS = $(OSC_DIR)/osc_cli.o \
           $(OSC_DIR)/oscillator.o \
           $(OSC_DIR)/alsa_output.o

# --- Project: hello-alsa ---
HELLO_SRCS = $(wildcard hello-alsa/*.c)
HELLO_BINS = $(patsubst hello-alsa/%.c, $(BIN_DIR)/%, $(HELLO_SRCS))

# --- Default Target ---
all: $(HELLO_BINS) $(OSC_BIN) $(ADSR_BIN)

# --- Header Dependencies (Information Hiding & Safety) ---
# These ensure that changing a .h file triggers a recompile of the .c file
$(ADSR_DIR)/osc_cli.o: $(ADSR_DIR)/osc_cli.c $(ADSR_DIR)/oscillator.h $(ADSR_DIR)/envelope.h $(ADSR_DIR)/alsa_output.h
$(ADSR_DIR)/oscillator.o: $(ADSR_DIR)/oscillator.c $(ADSR_DIR)/oscillator.h $(ADSR_DIR)/envelope.h
$(ADSR_DIR)/envelope.o: $(ADSR_DIR)/envelope.c $(ADSR_DIR)/envelope.h
$(ADSR_DIR)/alsa_output.o: $(ADSR_DIR)/alsa_output.c $(ADSR_DIR)/alsa_output.h

# Dependencies for the legacy osc_cli project
$(OSC_DIR)/osc_cli.o: $(OSC_DIR)/osc_cli.c $(OSC_DIR)/oscillator.h $(OSC_DIR)/alsa_output.h
$(OSC_DIR)/oscillator.o: $(OSC_DIR)/oscillator.c $(OSC_DIR)/oscillator.h

# --- Link Rules ---
$(ADSR_BIN): $(ADSR_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

$(OSC_BIN): $(OSC_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

$(BIN_DIR)/%: hello-alsa/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

# --- Pattern Rule for Compilation ---
# This rule handles the conversion of any .c file to a .o file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# --- Cleanup ---
clean:
	rm -rf $(BIN_DIR) $(ADSR_DIR)/*.o $(OSC_DIR)/*.o