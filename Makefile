CXX = g++
CXXFLAGS = -Iinclude -std=c++17 -Wall -Wextra

TARGET = bin/main

SRC_DIRS = src

OBJ_DIR = obj

SRCS = $(wildcard $(SRC_DIRS)/*.cpp)
LEX_SRCS = $(wildcard $(SRC_DIRS)/*.l)
YACC_SRCS = $(wildcard $(SRC_DIRS)/*.y)

OBJS = $(patsubst src/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
OBJS += $(OBJ_DIR)/scanner.o $(OBJ_DIR)/parser.o

LEX_GEN = src/scanner.cpp
YACC_GEN = src/parser.cpp src/parser.hpp

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(LEX_GEN): $(LEX_SRCS) $(YACC_GEN)
	@mkdir -p $(dir $@)
	flex -d -o $@ $<

$(YACC_GEN): $(YACC_SRCS)
	@mkdir -p $(dir $@)
	bison -d -o $@ $<

$(OBJ_DIR)/scanner.o: $(LEX_GEN)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/parser.o: src/parser.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(LEX_GEN) $(YACC_GEN)

.PHONY: all clean
