CXX = g++

CXXFLAGS = -Iinclude -std=c++20 -Wall -Wextra
LEX_FLAGS =
YACC_FLAGS =

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

all: release

release: $(TARGET)
debug: $(TARGET)
debug: LEX_FLAGS += -d
debug: YACC_FLAGS += -Wcounterexamples --debug
debug: CXXFLAGS += -DDEBUG_ON

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIRS)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(OBJ_DIR)/$*.d -c $< -o $@

$(LEX_GEN): $(LEX_SRCS) $(YACC_GEN)
	@mkdir -p $(dir $@)
	flex $(LEX_FLAGS) -o $@ $<

$(YACC_GEN): $(YACC_SRCS) $(INC_DIRS)/ast.hpp $(INC_DIRS)/make_ast.hpp
	@mkdir -p $(dir $@)
	bison $(YACC_FLAGS) -d -o $@ $<

$(OBJ_DIR)/scanner.o: $(LEX_GEN)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/parser.o: $(SRC_DIRS)/parser.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(LEX_GEN) $(YACC_GEN)

protocol:
	make release
	find protocols -type f -name "*.inc" | xargs -I {} ./verinc {} -o tla

.PHONY: all clean debug release protocol
