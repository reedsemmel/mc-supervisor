.PHONY = build container-build

CC = gcc
CFLAGS = -O2 -Wall -Wextra

build:
	podman build -t registry.gitlab.com/renilux/mcsv:dev .

mcsv: src/mcsv.c
	$(CC) $(CFLAGS) -o $@ $<

mcutil: src/mcutil.c
	$(CC) $(CFLAGS) -o $@ $<

container-build: mcsv mcutil
