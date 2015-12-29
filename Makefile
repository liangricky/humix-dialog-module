SPHINXBASE_VER=5prealpha
POCKETSPHINX_VER=5prealpha
OBJ_SPHINXBASE=deps/sphinxbase-$(SPHINXBASE_VER)/src/libsphinxbase/.libs/libsphinxbase.a
OBJ_SPHINXAD=deps/sphinxbase-$(SPHINXBASE_VER)/src/libsphinxad/.libs/libsphinxad.a
OBJ_POCKETSPHINX=deps/pocketsphinx-$(POCKETSPHINX_VER)/src/libpocketsphinx/.libs/libpocketsphinx.a
OBJ_HUMIXSPEECH=build/humix-speech.o
HUMIXMODULE=build/HumixSpeech.node
DEBUG=#-g
LIBS=-lasound -lpthread -lm -lsndfile
CPPFLAGS=$(DEBUG) -fPIC -O2 -Wall -ffunction-sections -fdata-sections -fno-omit-frame-pointer -fno-rtti -fno-exceptions -std=gnu++0x
NODEVER=$(shell node -v |sed s/v//)
INCLUDES=-I$(HOME)/.node-gyp/$(NODEVER)/include/node -Inode_modules/nan
NODEINC=$(HOME)/.node-gyp/$(NODEVER)/include/node

all: $(HUMIXMODULE)
#-Wl,--start-group xxxx.a. xxxx.a xxx.a -Wl,--end-group is used to resolve the circular dependencies
$(HUMIXMODULE): $(OBJ_HUMIXSPEECH)
	g++ -shared -pthread -rdynamic -Wl,-soname=HumixSpeech.node $(CPPFLAGS) \
		-Wl,--start-group $(OBJ_SPHINXBASE) $(OBJ_SPHINXAD) $(OBJ_POCKETSPHINX) -Wl,--end-group  \
		-Wl,--whole-archive $(OBJ_SPHINXBASE) $(OBJ_SPHINXAD) $(OBJ_POCKETSPHINX) -Wl,--no-whole-archive \
		-o $@ $(LIBS)

$(OBJ_HUMIXSPEECH): $(OBJ_SPHINXBASE) $(OBJ_SPHINXAD) $(OBJ_POCKETSPHINX) src/humix-speech.cpp build $(NODEINC)
	g++ $(CPPFLAGS) -I. -Ideps/sphinxbase-$(SPHINXBASE_VER)/include -Ideps/pocketsphinx-$(POCKETSPHINX_VER)/include $(INCLUDES) $(LIBS) -c -o $@ src/humix-speech.cpp

$(OBJ_SPHINXBASE): deps
	tar -xf sphinxbase-$(SPHINXBASE_VER).tar.gz -C deps
	#cd deps/sphinxbase-$(SPHINXBASE_VER); ./configure --enable-fixed
	cd deps/sphinxbase-$(SPHINXBASE_VER); export CFLAGS=" -g -O2 -Wall -fPIC"; ./configure --enable-fixed
	make -C deps/sphinxbase-$(SPHINXBASE_VER)

$(OBJ_SPHINXAD): $(OBJ_SPHINXBASE)

$(OBJ_POCKETSPHINX): deps $(OBJ_SPHINXBASE)
	tar -xf pocketsphinx-$(POCKETSPHINX_VER).tar.gz -C deps
	#cd deps/pocketsphinx-$(POCKETSPHINX_VER); ./configure
	cd deps/pocketsphinx-$(POCKETSPHINX_VER); export CFLAGS=" -g -O2 -Wall -fPIC"; ./configure
	make -C deps/pocketsphinx-$(POCKETSPHINX_VER)

$(NODEINC):
	node-gyp install

deps:
	mkdir deps

build:
	mkdir build

clean:
	rm -rf deps $(OBJ_HUMIXSPEECH) $(HUMIXMODULE)
