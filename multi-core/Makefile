# Compiler settings
CC = gcc
CFLAGS = -O2 -fopenmp

# Source files
BILINEAR_SRC = bilinear-interpolation.c
NEAREST_SRC = nearest-neighbour.c
BASIC_SRC = basic-read-write.c
BICUBIC_SRC = bicubic-interpolation.c

# Object files
BILINEAR_OBJ = $(BILINEAR_SRC:.c=.o)
NEAREST_OBJ = $(NEAREST_SRC:.c=.o)
BASIC_OBJ = $(BASIC_SRC:.c=.o)
BICUBIC_OBJ = $(BICUBIC_SRC:.c=.o)

# Executables
BILINEAR_EXE = bilinear-interpolation
NEAREST_EXE = nearest-scaling
BASIC_EXE = basic-read-write
BICUBIC_EXE = bicubic-interpolation

# Default target to build all executables
all: $(NEAREST_EXE) $(BASIC_EXE) $(BILINEAR_EXE)

# Rule to build nearest neighbor executable
$(NEAREST_EXE): $(NEAREST_OBJ)
	$(CC) $(NEAREST_OBJ) -o $(NEAREST_EXE) $(CFLAGS)

$(BASIC_EXE): $(BASIC_OBJ)
	$(CC) $(BASIC_OBJ) -o $(BASIC_EXE) $(CFLAGS)

$(BILINEAR_EXE): $(BILINEAR_OBJ)
	$(CC) $(BILINEAR_OBJ) -o $(BILINEAR_EXE) $(CFLAGS)

$(BICUBIC_EXE): $(BICUBIC_OBJ)
	$(CC) $(BICUBIC_OBJ) -o $(BICUBIC_EXE) $(CFLAGS)

# Rule to compile C source files to object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up object files and the executables
clean:
	rm -f $(NEAREST_OBJ) $(NEAREST_EXE) $(BASIC_OBJ) $(BASIC_EXE) $(BILINEAR_OBJ) $(BILINEAR_EXE) $(BICUBIC_EXE) $(BICUBIC_OBJ)
