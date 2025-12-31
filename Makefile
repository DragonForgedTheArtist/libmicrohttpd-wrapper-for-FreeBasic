CC      := gcc
AR      := ar
CFLAGS  := -O2 -I/mingw64/include
LDFLAGS := -lws2_32 -lwinpthread
MHD_A   := /mingw64/lib/libmicrohttpd.a

OBJS := libmhd_wrap.o

all: test_wrap.exe libmhd_wrap.a libmhd_wrap.dll test_wrap_static.exe

libmhd_wrap.o: libmhd_wrap.c
	$(CC) -c $(CFLAGS) $< -o $@

libmhd_wrap.a: $(OBJS)
	$(AR) rcs $@ $^

# Build the DLL, and generate the import lib as a side effect
libmhd_wrap.dll: $(OBJS)
	$(CC) -shared -o $@ $^ $(MHD_A) $(LDFLAGS) \
	    -Wl,--out-implib,libmhd_wrap.dll.a

libmhd_wrap.dll.a: libmhd_wrap.dll
	@true

# FB links via #inclib "mhd_wrap", so it needs the import lib present
test_wrap.exe: test_wrap.bas libmhd_wrap.dll.a libmhd_wrap.dll
	winpty fbc $< -x $@

test_wrap_static.exe: test_wrap.bas libmhd_wrap.a
	winpty fbc -static $< -l microhttpd -l winpthread -l ws2_32 -x $@

clean:
	rm -f *.o *.a *.dll *.exe
