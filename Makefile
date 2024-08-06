# xrandr-setup - XRandR configuration tool
# See LICENSE file for copyright and license details

PREFIX := /usr/local/bin
CC     := cc
CFLAGS := -std=c99 -pedantic -Wall -Wextra -Werror -Wno-deprecated-declarations -Os
LDFLAGS := -lX11 -lXrandr

all: xrandr-setup

main.o:
	$(CC) -o $@ main.c -c ${CFLAGS}

toml.o:
	$(CC) -o $@ toml.c -c ${CFLAGS}

xrandr-setup: main.o toml.o
	$(CC) -o $@ main.o toml.o ${LDFLAGS}

clean:
	@echo "cleaning xrandr-setup"
	rm -f xrandr-setup
	rm -f *.o

install: xrandr-setup
	@echo "installing xrandr-setup"
	mkdir -p $(PREFIX)
	cp xrandr-setup $(PREFIX)/

uninstall:
	@echo "uninstalling xrandr-setup"
	rm -f $(PREFIX)/xrandr-setup

.PHONY: all clean install uninstall