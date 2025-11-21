.PHONY: test tools

all: test

CXX = clang++
CXX = /opt/homebrew/opt/llvm/bin/clang++
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic -O3 -DNDEBUG -march=native -mtune=native -flto -fno-stack-protector -fomit-frame-pointer -fno-pic
LDFLAGS =

TOOLS = Stomp

tools: $(TOOLS)

$(TOOLS): %: tools/%.cpp *.hpp tools/*.hpp
	$(CXX) $(CXXFLAGS) $(CXXINCLUDE) -o $@ $< $(LDFLAGS)

GTEST_CFLAGS = `pkg-config --cflags gtest_main`
GTEST_LIBS = `pkg-config --libs gtest_main`

TEST_SOURCES = $(wildcard tests/*.cpp)
TEST_OBJECTS = $(TEST_SOURCES:.cpp=.o)

tests/%.o: tests/%.cpp *.hpp
	$(CXX) $(CXXFLAGS) $(GTEST_CFLAGS) -c -o $@ $<

testapp: $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(GTEST_LIBS)

test: testapp
	./testapp
