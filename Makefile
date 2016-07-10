CXX = c++
WARNINGS = -Wall -Weffc++ -Wshadow -Wextra
#DEBUG = -ggdb3 -DDEBUG
DEBUG=-O3 -flto
DFLAGS = -fPIC
LDFLAGS = -pthread
CXXFLAGS = --std=c++11 $(LDFLAGS) $(DEBUG) $(WARNINGS) $(DFLAGS) -I.

all: example

example: example.C pack.h
	$(CXX) -o $@ $< $(CXXFLAGS)

clean:
	-rm example *.o 2>/dev/null

love:
	@echo Not war?

.PHONY: examples clean all

