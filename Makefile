CC = gcc

build/dev.o: source/dev.c source/dev.h
	$(CC) -c source/dev.c -o build/dev.o

build/input.o: source/input.c source/input.h
	$(CC) -c source/input.c -o build/input.o

build/bot.o: source/bot.c source/dev.c source/input.c source/bot.h source/dev.h source/input.h source/env.h
	$(CC) -c source/bot.c -o build/bot.o

main.exe: source/main.c build/dev.o build/input.o build/bot.o
	$(CC) -o main.exe source/main.c build/dev.o build/input.o build/bot.o -lwinhttp -mwindows -O3

main: main.exe
all: main

.PHONY: clean run rel main

clean:
	del /Q main.exe build/dev.o build/input.o build/bot.o

run: main.exe
	.\main.exe

rel: source/main.c build/dev.o build/input.o build/bot.o
	$(CC) -o main.exe source/main.c build/dev.o build/input.o build/bot.o -lwinhttp -mwindows -s -O3
	strip main.exe
	upx main.exe -o main_rel.exe -9
	mv main_rel.exe main.exe