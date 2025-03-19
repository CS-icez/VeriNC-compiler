CXX = g++
CXXFLAGS = -Iinclude -std=c++20 -Wall -Wextra

TARGET = verinc

INC_DIRS = include

SRC_DIRS = src

OBJ_DIR = obj

SRCS = $(wildcard $(SRC_DIRS)/*.cpp)
LEX_SRCS = $(wildcard $(SRC_DIRS)/*.l)
YACC_SRCS = $(wildcard $(SRC_DIRS)/*.y)

OBJS = $(patsubst $(SRC_DIRS)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
OBJS += $(OBJ_DIR)/scanner.o $(OBJ_DIR)/parser.o

DEPS = $(OBJS:.o=.d)

LEX_GEN = $(SRC_DIRS)/scanner.cpp
YACC_GEN = $(SRC_DIRS)/parser.cpp $(SRC_DIRS)/parser.hpp

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIRS)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(OBJ_DIR)/$*.d -c $< -o $@

$(LEX_GEN): $(LEX_SRCS) $(YACC_GEN)
	@mkdir -p $(dir $@)
	flex -d -o $@ $<

$(YACC_GEN): $(YACC_SRCS) $(INC_DIRS)/ast.hpp $(INC_DIRS)/make_ast.hpp
	@mkdir -p $(dir $@)
	bison -Wcounterexamples --debug -d -o $@ $<

$(OBJ_DIR)/scanner.o: $(LEX_GEN)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/parser.o: $(SRC_DIRS)/parser.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(LEX_GEN) $(YACC_GEN)

.PHONY: all clean
