CXX ?= c++
CPPFLAGS ?=
CXXFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=
PREFIX ?= /usr/local
DESTDIR ?=

.PHONY: all test install clean
all: build/hmon-core

build/hmon-core: $(wildcard src/*.hpp) src/main.cpp
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -Wall -Wextra -Werror -fstack-protector-strong src/main.cpp $(LDFLAGS) $(LDLIBS) -o $@

test: build/hmon-core
	./build/hmon-core --self-test

# A native HarmonyOS PC requires ELF signing before installation. Homebrew's
# OHOS build pipeline supplies signing; the Windows build script signs too.
install: build/hmon-core
	install -d "$(DESTDIR)$(PREFIX)/libexec/hmon" "$(DESTDIR)$(PREFIX)/bin" "$(DESTDIR)$(PREFIX)/share/doc/hmon"
	install -m 755 hmon build/hmon-core "$(DESTDIR)$(PREFIX)/libexec/hmon/"
	ln -sfn ../libexec/hmon/hmon "$(DESTDIR)$(PREFIX)/bin/hmon"
	install -m 644 README.md LICENSE docs/USAGE.txt "$(DESTDIR)$(PREFIX)/share/doc/hmon/"

clean:
	rm -f build/hmon-core
