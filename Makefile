HEADERS = bloom.h crack.h hash160.h warpwallet.h
OBJ_MAIN = brainflayer.o hex2blf.o blfchk.o ecmtabgen.o hexln.o filehex.o addr2hex.o
OBJ_UTIL = hex.o bloom.o mmapf.o hsearchf.o ec_pubkey_fast.o ripemd160_256.o dldummy.o
OBJ_ALGO = $(patsubst %.c,%.o,$(wildcard algo/*.c))
OBJECTS = $(OBJ_MAIN) $(OBJ_UTIL) $(OBJ_ALGO)

# Platform detection
ifeq ($(OS),Windows_NT)
  EXT  = .exe
  # -lws2_32 needed for winsock2 (ntohl/htonl); -lrt is Linux-only
  LIBS = -lws2_32 -lcrypto -lgmp
else
  EXT  =
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Linux)
    LIBS = -lrt -lcrypto -lgmp
  else
    # macOS, BSD, etc.
    LIBS = -lcrypto -lgmp
  endif
endif

BINARIES = brainflayer$(EXT) hexln$(EXT) hex2blf$(EXT) blfchk$(EXT) ecmtabgen$(EXT) filehex$(EXT) addr2hex$(EXT)
CFLAGS = -O3 \
         -flto -funsigned-char -falign-functions=16 -falign-loops=16 -falign-jumps=16 \
         -Wall -Wextra -Wno-pointer-sign -Wno-sign-compare -Wno-deprecated-declarations \
         -pedantic -std=gnu99
COMPILE = gcc $(CFLAGS)

TESTS = tests/normalize_test$(EXT)
BENCH_DICT = bench/bench_dict.txt
BENCH_B ?= 1024
VALGRIND ?= valgrind

all: $(BINARIES)

secp256k1/.libs/libsecp256k1.a:
	cd secp256k1; make distclean || true
	cd secp256k1; ./autogen.sh
	cd secp256k1; ./configure
	cd secp256k1; make

secp256k1/include/secp256k1.h: secp256k1/.libs/libsecp256k1.a

scrypt-jane/scrypt-jane.o: scrypt-jane/scrypt-jane.h scrypt-jane/scrypt-jane.c
	cd scrypt-jane; gcc -O3 -DSCRYPT_SALSA -DSCRYPT_SHA256 -c scrypt-jane.c -o scrypt-jane.o

brainflayer.o: brainflayer.c secp256k1/include/secp256k1.h

algo/warpwallet.o: algo/warpwallet.c scrypt-jane/scrypt-jane.h

algo/brainwalletio.o: algo/brainwalletio.c scrypt-jane/scrypt-jane.h

algo/brainv2.o: algo/brainv2.c scrypt-jane/scrypt-jane.h

ec_pubkey_fast.o: ec_pubkey_fast.c secp256k1/include/secp256k1.h
	$(COMPILE) -Wno-unused-function -c $< -o $@

%.o: %.c
	$(COMPILE) -c $< -o $@

hexln$(EXT): hexln.o hex.o
	$(COMPILE) $^ $(LIBS) -o $@

blfchk$(EXT): blfchk.o hex.o bloom.o mmapf.o hsearchf.o
	$(COMPILE) $^ $(LIBS) -o $@

hex2blf$(EXT): hex2blf.o hex.o bloom.o mmapf.o
	$(COMPILE) $^ $(LIBS) -lm -o $@

ecmtabgen$(EXT): ecmtabgen.o mmapf.o ec_pubkey_fast.o
	$(COMPILE) $^ $(LIBS) -o $@

filehex$(EXT): filehex.o hex.o
	$(COMPILE) $^ $(LIBS) -o $@

addr2hex$(EXT): addr2hex.o hex.o
	$(COMPILE) $^ $(LIBS) -o $@

brainflayer$(EXT): brainflayer.o $(OBJ_UTIL) $(OBJ_ALGO) \
             secp256k1/.libs/libsecp256k1.a scrypt-jane/scrypt-jane.o
	$(COMPILE) $^ $(LIBS) -o $@

clean:
	rm -f $(BINARIES) $(OBJECTS) $(TESTS) tests/normalize_test.o
	rm -f bench/bench_dict.txt

.PHONY: test
test: hexln$(EXT) blfchk$(EXT) hex2blf$(EXT) brainflayer$(EXT) $(TESTS)
	./tests/normalize_test$(EXT)

.PHONY: memcheck
memcheck: brainflayer$(EXT)
	@echo "Running memcheck (valgrind required) ..."
	@if command -v $(VALGRIND) >/dev/null 2>&1; then \
		tmp_dict=$$(mktemp); \
		printf 'alpha\nbravo\r\ncharlie\rdelta' > $$tmp_dict; \
		$(VALGRIND) --quiet --leak-check=full --error-exitcode=1 ./brainflayer -t sha256 -B 8 -i $$tmp_dict >/dev/null; \
		rm -f $$tmp_dict; \
	else \
		echo "valgrind not found; skipping memcheck (set VALGRIND to override)"; \
	fi

# sanitize: build all binaries (including brainflayer) with AddressSanitizer.
# Note: brainflayer links secp256k1 and scrypt-jane which must already be built.
.PHONY: sanitize
sanitize:
	@echo "Building with AddressSanitizer ..."
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -fsanitize=address -fno-omit-frame-pointer" \
	        hexln$(EXT) blfchk$(EXT) hex2blf$(EXT) brainflayer$(EXT) $(TESTS)
	./tests/normalize_test$(EXT)

.PHONY: bench
bench: brainflayer$(EXT) $(BENCH_DICT)
	@echo "Benchmarking brainflayer (B=$(BENCH_B))"
	@/usr/bin/time -f "brainflayer elapsed %e s" ./brainflayer -t sha256 -B $(BENCH_B) -i $(BENCH_DICT) >/dev/null

$(BENCH_DICT):
	@python3 bench/generate_bench_dict.py

tests/normalize_test$(EXT): tests/normalize_test.o hex.o
	$(COMPILE) $^ $(LIBS) -o $@

tests/normalize_test.o: tests/normalize_test.c hex.h
	$(COMPILE) -c $< -o $@
