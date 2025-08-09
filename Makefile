# Target(s) to build
EXE_SRV = server
EXE_CLN = client
DEBUG = true

# Compiler and linker to use
CC = g++
LD = $(CC)

# Configure Debug or Release build
CFLAGS = -std=gnu++17 -Wall -pthread
LDFLAGS = -pthread -Wl,-rpath,'$$ORIGIN/$(PROTOBUF_INSTALL)/lib'

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

# Protobuf files 
ARC = $(shell uname -m)
PROTOBUF_INSTALL = $(PROJECT_HOME)/protobuf.3.20.1.$(ARC)
PROTOC = $(PROTOBUF_INSTALL)/bin/protoc
PROTO_OUT  = $(OBJ_DIR)/_generate
PROTO_HOME = $(PROJECT_HOME)/protos
PROTO_SRCS = $(PROTO_HOME)/hello.proto

# Include directories
INCS = -I$(PROJECT_HOME)/include \
       -I$(PROTOBUF_INSTALL)/include \
       -I$(PROTO_OUT)

# Libraries
LIBS = -L$(PROTOBUF_INSTALL)/lib -lprotobuf

# Protobuf files to generate from *.proto files 
PROTO_NAMES = $(basename $(notdir $(PROTO_SRCS)))

PROTOC_CC   = $(addprefix $(PROTO_OUT)/, $(addsuffix .pb.cc, $(PROTO_NAMES)))
PROTOC_OBJS = $(addprefix $(OBJ_DIR)/,   $(addsuffix .pb.o,  $(PROTO_NAMES)))

# Objective files to build
OBJS_SRV =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_SRV)))))
OBJS_SRV += $(PROTOC_OBJS)

OBJS_CLN =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_CLN)))))
OBJS_CLN += $(PROTOC_OBJS)

# Build target(s)
all: $(EXE_SRV) $(EXE_CLN)

$(EXE_SRV): $(PROTOC_CC) $(OBJS_SRV)
	$(LD) $(LDFLAGS) -o $(EXE_SRV) $(OBJS_SRV) $(LIBS)

$(EXE_CLN): $(PROTOC_CC) $(OBJS_CLN)
	$(LD) $(LDFLAGS) -o $(EXE_CLN) $(OBJS_CLN) $(LIBS)

# Compile source files
# Add -MP to generate dependency list
# Add -MMD to not include system headers
$(OBJ_DIR)/%.o: $(PROJECT_HOME)/%.cpp Makefile   
	-mkdir -p $(OBJ_DIR)
	$(CC) -c -MP -MMD $(CFLAGS) $(INCS) -o $(OBJ_DIR)/$*.o $<
	
# Compile protobuf source files
$(OBJ_DIR)/%.o: $(PROTO_OUT)/%.cc Makefile
	-mkdir -p $(OBJ_DIR)
	$(CC) -c $(CFLAGS) $(INCS) -I$(PROTO_OUT) -o $(OBJ_DIR)/$*.o $<

# Generate protobuf files
$(PROTO_OUT)/%.pb.cc: $(PROTO_HOME)/%.proto Makefile
	@echo ">>> Generating proto files from $<..."
	-mkdir -p $(PROTO_OUT)
	$(PROTOC) --cpp_out=$(PROTO_OUT) --proto_path=$(PROTO_HOME) $<

# Delete all intermediate files
clean clear:
	rm -rf $(EXE_SRV) $(EXE_CLN) $(OBJ_DIR)

# Read the dependency files.
# Note: use '-' prefix to don't display error or warning
# if include file do not exist (just remade it)
-include $(OBJS_SRV:.o=.d)
-include $(OBJS_CLN:.o=.d)


