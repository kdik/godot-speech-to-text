ifeq ($(OS),Windows_NT)
  OS_NAME=win32
  DLL_SUFFIX=dll
else ifeq ($(shell uname -s),Linux)
  OS_NAME=linux
  DLL_SUFFIX=so
  C_FILE_SUFFIX=-linux
endif
BUILD_DIRECTORY=addons/speechtotext/lib/$(OS_NAME)

CC=gcc
INCLUDE_FLAGS:=-Iinclude/godot_headers -Iinclude/pocketsphinx
ifeq ($(OS),Windows_NT)
  INCLUDE_FLAGS+= -Iinclude/pthreads
endif
LDFLAGS=-Llib/$(OS_NAME)
ifeq ($(shell uname -s),Linux)
  CFLAGS=-fPIC
endif
LDLIBS=-lpocketsphinx -lsphinxbase
ifeq ($(OS),Windows_NT)
  LDLIBS+= -lpthreadGC2
else
   LDLIBS+= -lsphinxad
endif
ifeq ($(shell uname -s),Linux)
  LDLIBS+= -lpulse -lpulse-simple
endif

stt:
	$(CC) $(CFLAGS) -c $@$(C_FILE_SUFFIX).c $(INCLUDE_FLAGS) -o $(BUILD_DIRECTORY)/$@.o -Wno-incompatible-pointer-types-discards-qualifiers
	$(CC) $(CFLAGS) -shared $(BUILD_DIRECTORY)/$@.o $(LDFLAGS) $(LDLIBS) -o $(BUILD_DIRECTORY)/lib$@.$(DLL_SUFFIX)
clean:
	rm -r $(BUILD_DIRECTORY)/stt.o $(BUILD_DIRECTORY)/libstt.$(DLL_SUFFIX)
