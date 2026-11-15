# ==============================================================================
# PROJECT CONFIGURATION
# ==============================================================================
TARGET      := ordenar_matriz
SRCDIR      := .
OBJDIR      := obj
BINDIR      := .

# Compiler and Flags
CXX         := g++
CXXFLAGS    := -Wall -Wextra -std=c++17 -O3
#CPPFLAGS    := -Iinclude # Include paths (e.g., if you have a separate 'include' folder)
CPPFLAGS    :=
# Bibliotecas del proyecto: -lrt para shm_open/mmap y -lpthread para hilos.
LDLIBS      := -lrt -lpthread

# ==============================================================================
# FILE DISCOVERY & CORRESPONDENCE
# ==============================================================================
# Find all .cpp files in the source directory
SRCS        := $(shell find $(SRCDIR) -type f -name "*.cpp")

# Map source files to object files in the object directory
# e.g., src/main.cpp -> obj/main.o
OBJS        := $(patsubst $(SRCDIR)/%.cpp, $(OBJDIR)/%.o, $(SRCS))

# Generate dependency file names (.d) from object file names
DEPS        := $(OBJS:.o=.d)

# Final binary path
EXEC        := $(BINDIR)/$(TARGET)

# ==============================================================================
# RULES & TARGETS
# ==============================================================================
.PHONY: all clean entrega

# Default rule to build the executable
all: $(EXEC)

# Rule to link object files into the final executable
$(EXEC): $(OBJS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

# Rule to compile C++ source files into object files
# -MMD -MP flags automatically generate .d dependency files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

# Include generated dependency files (ignores them if they don't exist yet)
-include $(DEPS)

# Arma la carpeta de entrega (codigo + matriz_a.txt, sin duplicar los 16.8 GB).
entrega: $(EXEC)
	./empaquetar_entrega.sh

# Clean rule to wipe the build artifacts safely
clean:
	@echo "Cleaning project..."
	rm -rf $(OBJDIR) $(EXEC)
