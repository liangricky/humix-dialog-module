SPHINXBASE_VER=5prealpha
POCKETSPHINX_VER=5prealpha
OBJ_SPHINXBASE=deps/sphinxbase-$(SPHINXBASE_VER)/src/libsphinxbase/.libs/libsphinxbase.a
OBJ_SPHINXAD=deps/sphinxbase-$(SPHINXBASE_VER)/src/libsphinxad/.libs/libsphinxad.a
OBJ_POCKETSPHINX=deps/pocketsphinx-$(POCKETSPHINX_VER)/src/libpocketsphinx/.libs/libpocketsphinx.a
OBJ_HUMIXSPEECH=humix-speech.o
DEBUG=#-g
LIBS=-lasound -lpthread -lm -lsndfile
CFLAGS=$(DEBUG) -O2 -Wall

all: humix-speech
#-Wl,--start-group xxxx.a. xxxx.a xxx.a -Wl,--end-group is used to resolve the circular dependencies
humix-speech: $(OBJ_HUMIXSPEECH)
	g++ $(CFLAGS) \
		-Wl,--whole-archive $(OBJ_SPHINXBASE) $(OBJ_SPHINXAD) $(OBJ_POCKETSPHINX) -Wl,--no-whole-archive  \
		-o $@ $(OBJ_HUMIXSPEECH) $(OBJ_SPHINXBASE) $(OBJ_SPHINXAD) $(OBJ_POCKETSPHINX) \
		$(LIBS)

$(OBJ_HUMIXSPEECH): $(OBJ_SPHINXBASE) $(OBJ_SPHINXAD) $(OBJ_POCKETSPHINX) humix-speech.c
	g++ $(CFLAGS) -I. -Ideps/sphinxbase-$(SPHINXBASE_VER)/include -Ideps/pocketsphinx-$(POCKETSPHINX_VER)/include $(LIBS) -c -o $@ humix-speech.c

$(OBJ_SPHINXBASE): deps
	tar -xf sphinxbase-$(SPHINXBASE_VER).tar.gz -C deps
	cd deps/sphinxbase-$(SPHINXBASE_VER); ./configure --enable-fixed
	make -C deps/sphinxbase-$(SPHINXBASE_VER)

$(OBJ_SPHINXAD): $(OBJ_SPHINXBASE)

$(OBJ_POCKETSPHINX): deps $(OBJ_SPHINXBASE)
	tar -xf pocketsphinx-$(POCKETSPHINX_VER).tar.gz -C deps
	cd deps/pocketsphinx-$(POCKETSPHINX_VER); ./configure
	make -C deps/pocketsphinx-$(POCKETSPHINX_VER)

deps:
	mkdir deps

clean:
	rm -rf deps $(OBJ_HUMIXSPEECH) humix-speech
