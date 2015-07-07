SPHINXBASE_VER=5prealpha
POCKETSPHINX_VER=5prealpha
OBJ_SPHINXBASE=deps/sphinxbase-$(SPHINXBASE_VER)/src/libsphinxbase/.libs/libsphinxbase.a
OBJ_SPHINXAD=deps/sphinxbase-$(SPHINXBASE_VER)/src/libsphinxad/.libs/libsphinxad.a
OBJ_POCKETSPHINX=deps/pocketsphinx-$(POCKETSPHINX_VER)/src/libpocketsphinx/.libs/libpocketsphinx.a
OBJ_HUMIXSPEECH=humix-speech.o
DEBUG=#-g
CFLAGS=$(DEBUG) -O2 -Wall

all: humix-speech


.PHONY: sphinxbase 
sphinxbase:  $(OBJ_SPHINXBASE)

.PHONY: sphinxad
sphinxad: $(OBJ_SPHINXAD)

.PHONY: pocketsphinx
pocketsphinx: $(OBJ_POCKETSPHINX)

humix-speech: $(OBJ_HUMIXSPEECH)
	gcc $(CFLAGS) \
		-Wl,--whole-archive $(OBJ_SPHINXBASE) $(OBJ_SPHINXAD) $(OBJ_POCKETSPHINX) -Wl,--no-whole-archive  \
		-o $@ $(OBJ_HUMIXSPEECH) $(OBJ_SPHINXBASE) $(OBJ_SPHINXAD) $(OBJ_POCKETSPHINX) \
		-lasound -lm

$(OBJ_HUMIXSPEECH): sphinxbase sphinxad pocketsphinx humix-speech.c
	gcc $(CFLAGS) -I. -Ideps/sphinxbase-$(SPHINXBASE_VER)/include -Ideps/pocketsphinx-$(POCKETSPHINX_VER)/include -lasound -lpthread -lm -c -o $@ humix-speech.c

$(OBJ_SPHINXBASE): deps
	tar -xf sphinxbase-$(SPHINXBASE_VER).tar.gz -C deps
	cd deps/sphinxbase-$(SPHINXBASE_VER); ./configure --enable-fixed
	make -C deps/sphinxbase-$(SPHINXBASE_VER) all

$(OBJ_SPHINXAD): $(OBJ_SPHINXBASE)

$(OBJ_POCKETSPHINX): deps
	tar -xf pocketsphinx-$(POCKETSPHINX_VER).tar.gz -C deps
	cd deps/pocketsphinx-$(POCKETSPHINX_VER); ./configure
	make -C deps/pocketsphinx-$(POCKETSPHINX_VER) all


deps:
	mkdir deps

clean:
	rm -rf deps $(OBJ_HUMIXSPEECH) humix-speech
