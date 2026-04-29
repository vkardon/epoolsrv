# Target(s) to build
EXE = hashserver
DEBUG = true

# Compiler and linker to use
CC = g++
LD = $(CC)

# Configure Debug or Release build
CFLAGS = -std=gnu++17 -Wall -pthread
LDFLAGS = -pthread

ifeq "$(DEBUG)" "true"
  # Debug build
  CFLAGS += -g
else
  # Release build (-s to remove all symbol table and relocation info)
  CFLAGS += -O3 -DNDEBUG
  LDFLAGS += -s
endif

# Sources
PROJECT_HOME = .
OBJ_DIR = $(PROJECT_HOME)/_obj

SRCS = $(PROJECT_HOME)/main.cpp 

# Include directories
INCS = -I$(PROJECT_HOME)/../../include

# Libraries
LIBS = -lssl -lcrypto

# Objective files to build
OBJS =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS)))))

# Build target(s)
all: $(EXE)

$(EXE): $(OBJS)
	$(LD) $(LDFLAGS) -o $(EXE) $(OBJS) $(LIBS)

# Compile source files
# Add -MP to generate dependency list
# Add -MMD to not include system headers
$(OBJ_DIR)/%.o: $(PROJECT_HOME)/%.cpp Makefile   
	-mkdir -p $(OBJ_DIR)
	$(CC) -c -MP -MMD $(CFLAGS) $(INCS) -o $(OBJ_DIR)/$*.o $<
	
# Delete all intermediate files
clean clear:
	rm -rf $(EXE) $(OBJ_DIR)

# Read the dependency files.
# Note: use '-' prefix to don't display error or warning
# if include file do not exist (just remade it)
-include $(OBJS:.o=.d)


