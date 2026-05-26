# ─────────────────────────────────────────────────────────────────────────────
# ThreadPoolLib Makefile
# ─────────────────────────────────────────────────────────────────────────────

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread
IFLAGS   := -Iinclude
BUILDDIR := build

.PHONY: all demo tests clean

all: demo tests

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# ── Build + run demo ─────────────────────────────────────────────────────────
demo: $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(IFLAGS) src/ThreadPool.cpp examples/demo.cpp -o $(BUILDDIR)/demo
	@echo "\n── Running Demo ─────────────────────────────"
	./$(BUILDDIR)/demo

# ── Build + run tests ────────────────────────────────────────────────────────
tests: $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(IFLAGS) src/ThreadPool.cpp tests/test_threadpool.cpp -o $(BUILDDIR)/tests
	@echo "\n── Running Tests ────────────────────────────"
	./$(BUILDDIR)/tests

# ── Static library ───────────────────────────────────────────────────────────
lib: $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(IFLAGS) -c src/ThreadPool.cpp -o $(BUILDDIR)/ThreadPool.o
	ar rcs $(BUILDDIR)/libthreadpool.a $(BUILDDIR)/ThreadPool.o
	@echo "Library built: $(BUILDDIR)/libthreadpool.a"

clean:
	rm -rf $(BUILDDIR)
