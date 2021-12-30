.PHONY = build container-build

CC = gcc
CFLAGS = -O2 -Wall -Wextra

build:
	podman build -t ghcr.io/reedsemmel/mc-supervisor:dev .

mcsv: src/mcsv.c
	$(CC) $(CFLAGS) -o $@ $<

mcutil: src/mcutil.c
	$(CC) $(CFLAGS) -o $@ $<

container-build: mcsv mcutil
