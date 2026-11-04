# ==============================================================================
# PROJECT CONFIGURATION
# ==============================================================================
TARGET      := leer_csv
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
# EXEC        := $(TARGET)

# ==============================================================================
# RULES & TARGETS
# ==============================================================================
.PHONY: all clean datos datos-chico run

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

# Datos de prueba. Se generan solo si el archivo no existe.
matriz_1000x1000.csv:
	python3 tools/generador_csv.py -f 1000 -c 1000 -o $@

matriz_10000x10000.csv:
	python3 tools/generador_csv.py -f 10000 -c 10000 -o $@

datos-chico: matriz_1000x1000.csv

datos: matriz_10000x10000.csv

# Prueba completa del Sprint 1: leer, ordenar y guardar.
run: $(EXEC) matriz_1000x1000.csv
	./$(EXEC) matriz_1000x1000.csv 1000 1000 ordenada.csv

# Clean rule to wipe the build artifacts safely
clean:
	@echo "Cleaning project..."
	rm -rf $(OBJDIR) $(EXEC)
