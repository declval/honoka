CXX = g++
CXXFLAGS = -O3 -Wall -Wconversion -Wextra -march=native -pedantic -std=c++23
LIBS = -lsqlite3
OBJS = main.o
PREFIX ?= /usr/local
PROGRAM = honoka

all: ${PROGRAM}

main.o: main.cpp
	${CXX} ${CXXFLAGS} -c -o $@ $<

${PROGRAM}: ${OBJS}
	${CXX} ${OBJS} ${LIBS} -o $@

.PHONY: clean install uninstall

clean:
	rm -f ${OBJS} ${PROGRAM}

install: all
	mkdir -p ${PREFIX}/bin
	install -p ${PROGRAM} ${PREFIX}/bin

uninstall:
	rm -f ${PREFIX}/bin/${PROGRAM}
