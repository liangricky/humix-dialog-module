{
  "targets": [
    {
      "target_name": "HumixSpeech",
      "sources": [
        "./src/WavUtil.cpp",
        "./src/StreamTTS.cpp",
        "./src/NaoSpeech.cpp",
        "./src/HumixSpeech.cpp"
      ],
      "include_dirs": [ "<!(node -e \"require('nan')\")",
        "./deps/sphinxbase-5prealpha/include",
        "./deps/pocketsphinx-5prealpha/include",
        "../../ctc-linux64-atom-2.1.4.13/libnaoqi/include",
        "../../ctc-linux64-atom-2.1.4.13/boost/include/boost-1_55/",
      ],
      "libraries": [ "-Wl,--whole-archive",
        "../deps/sphinxbase-5prealpha/src/libsphinxbase/.libs/libsphinxbase.a",
        "../deps/sphinxbase-5prealpha/src/libsphinxad/.libs/libsphinxad.a",
        "../deps/pocketsphinx-5prealpha/src/libpocketsphinx/.libs/libpocketsphinx.a",
        "-Wl,--no-whole-archive",
        "-lasound", "-lpthread", "-lsndfile", "-lFLAC++",
        "-L../../../ctc-linux64-atom-2.1.4.13/libnaoqi/lib",
        "-lalproxies",
      ],
      "cflags_cc!": [ "-fno-rtti", "-fno-exceptions" ],
      "cflags!": [ "-fno-exceptions" ],
    },
    {
      "target_name": "action_after_build",
      "type": "none",
      "dependencies": [ "HumixSpeech" ],
      "copies": [{
        "destination": "./lib/",
        "files": [
          "<(PRODUCT_DIR)/HumixSpeech.node"
        ]},
        {"destination": "./node_modules/watson-developer-cloud/services/speech_to_text",
        "files": [
          "./watson-fix/v1.js"
        ]}
      ]
    }

  ]
}
