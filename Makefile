SPHINXBASE_VER=5prealpha
POCKETSPHINX_VER=5prealpha
OBJ_SPHINXBASE=deps/sphinxbase-$(SPHINXBASE_VER)/src/libsphinxbase/.libs/libsphinxbase.a
OBJ_SPHINXAD=deps/sphinxbase-$(SPHINXBASE_VER)/src/libsphinxad/.libs/libsphinxad.a
OBJ_POCKETSPHINX=deps/pocketsphinx-$(POCKETSPHINX_VER)/src/libpocketsphinx/.libs/libpocketsphinx.a
HUMIXMODULE=lib/HumixSpeech.node

all: $(HUMIXMODULE)
#-Wl,--start-group xxxx.a. xxxx.a xxx.a -Wl,--end-group is used to resolve the circular dependencies
$(HUMIXMODULE): $(OBJ_SPHINXBASE) $(OBJ_SPHINXAD) $(OBJ_POCKETSPHINX) src/HumixSpeech.cpp
	node-gyp configure --arch=ia32
	node-gyp build --arch=ia32
	
$(OBJ_SPHINXBASE):
	tar -xf deps/sphinxbase-$(SPHINXBASE_VER).tar.gz -C deps
	cd deps/sphinxbase-$(SPHINXBASE_VER); export CFLAGS=" -march=atom -O2 -Wall -fPIC"; ./configure --enable-fixed
	make -C deps/sphinxbase-$(SPHINXBASE_VER)/src/libsphinxbase
	make -C deps/sphinxbase-$(SPHINXBASE_VER)/src/libsphinxad

$(OBJ_SPHINXAD): $(OBJ_SPHINXBASE)

$(OBJ_POCKETSPHINX): $(OBJ_SPHINXBASE)
	tar -xf deps/pocketsphinx-$(POCKETSPHINX_VER).tar.gz -C deps
	cd deps/pocketsphinx-$(POCKETSPHINX_VER); export CFLAGS=" -march=atom -O2 -Wall -fPIC"; ./configure
	make -C deps/pocketsphinx-$(POCKETSPHINX_VER)/src/libpocketsphinx

clean:
	rm -rf deps/sphinxbase-$(SPHINXBASE_VER) deps/pocketsphinx-$(POCKETSPHINX_VER)  build $(HUMIXMODULE)
