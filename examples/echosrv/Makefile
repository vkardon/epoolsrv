# Target(s) to build
EXE_SRV = server
EXE_CLN = client
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

SRCS_SRV = $(PROJECT_HOME)/server.cpp 

SRCS_CLN = $(PROJECT_HOME)/client.cpp

# Include directories
INCS = -I$(PROJECT_HOME)/../../include

# Libraries
LIBS = 

# Objective files to build
OBJS_SRV =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_SRV)))))

OBJS_CLN =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_CLN)))))

# Build target(s)
all: $(EXE_SRV) $(EXE_CLN)

$(EXE_SRV): $(OBJS_SRV)
	$(LD) $(LDFLAGS) -o $(EXE_SRV) $(OBJS_SRV) $(LIBS)

$(EXE_CLN): $(OBJS_CLN)
	$(LD) $(LDFLAGS) -o $(EXE_CLN) $(OBJS_CLN) $(LIBS)

# Compile source files
# Add -MP to generate dependency list
# Add -MMD to not include system headers
$(OBJ_DIR)/%.o: $(PROJECT_HOME)/%.cpp Makefile   
	-mkdir -p $(OBJ_DIR)
	$(CC) -c -MP -MMD $(CFLAGS) $(INCS) -o $(OBJ_DIR)/$*.o $<
	
# Delete all intermediate files
clean clear:
	rm -rf $(EXE_SRV) $(EXE_CLN) $(OBJ_DIR)

# Read the dependency files.
# Note: use '-' prefix to don't display error or warning
# if include file do not exist (just remade it)
-include $(OBJS_SRV:.o=.d)
-include $(OBJS_CLN:.o=.d)


