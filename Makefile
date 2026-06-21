SHEDSKIN_LIBDIR=/usr/share/shedskin/lib
CC=g++
CCFLAGS=-O2 -march=native -Wno-deprecated $(CPPFLAGS) -I. -I${SHEDSKIN_LIBDIR}
LFLAGS=-lgc -lpcre $(LDFLAGS)

CPPFILES=rubik.cpp \
	${SHEDSKIN_LIBDIR}/time.cpp \
	${SHEDSKIN_LIBDIR}/sys.cpp \
	${SHEDSKIN_LIBDIR}/string.cpp \
	${SHEDSKIN_LIBDIR}/re.cpp \
	${SHEDSKIN_LIBDIR}/random.cpp \
	${SHEDSKIN_LIBDIR}/math.cpp \
	${SHEDSKIN_LIBDIR}/datetime.cpp \
	${SHEDSKIN_LIBDIR}/builtin.cpp

HPPFILES=rubik.hpp \
	${SHEDSKIN_LIBDIR}/time.hpp \
	${SHEDSKIN_LIBDIR}/sys.hpp \
	${SHEDSKIN_LIBDIR}/string.hpp \
	${SHEDSKIN_LIBDIR}/re.hpp \
	${SHEDSKIN_LIBDIR}/random.hpp \
	${SHEDSKIN_LIBDIR}/math.hpp \
	${SHEDSKIN_LIBDIR}/datetime.hpp \
	${SHEDSKIN_LIBDIR}/builtin.hpp

all:	rubik

rubik:	$(CPPFILES) $(HPPFILES)
	$(CC)  $(CCFLAGS) $(CPPFILES) $(LFLAGS) -o rubik

rubik_prof:	$(CPPFILES) $(HPPFILES)
	$(CC) -pg -ggdb $(CCFLAGS) $(CPPFILES) $(LFLAGS) -o rubik_prof

rubik_debug:	$(CPPFILES) $(HPPFILES)
	$(CC) -g -ggdb $(CCFLAGS) $(CPPFILES) $(LFLAGS) -o rubik_debug

clean:
	rm -f rubik rubik_prof rubik_debug

.PHONY: all clean

