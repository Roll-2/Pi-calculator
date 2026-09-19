# ============================================================
# Makefile di Pi-visual — visualizzatore π a due viewport
#
#   make          compila Pi-visual
#   make run      esegue ./Pi-visual
#   make clean    rimuove binario e oggetti
#
# Requisiti (macOS ARM, Homebrew): sdl2, sdl2_ttf, zlib (di sistema)
# ============================================================

CXX      = clang++
CXXFLAGS = -std=c++17 -O2 -Wall -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 -Ivisual
LDFLAGS  = -L/opt/homebrew/lib
LDLIBS   = -framework OpenGL -framework Cocoa -lSDL2 -lSDL2_ttf -lz

OBJS = Pi-visual.o

all: Pi-visual

Pi-visual: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

Pi-visual.o: Pi-visual.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: Pi-visual
	./Pi-visual

clean:
	rm -f Pi-visual $(OBJS)

.PHONY: all run clean
