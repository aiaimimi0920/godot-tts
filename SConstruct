#!/usr/bin/env python
import os
import sys

env = SConscript("thirdparty/godot-cpp/SConstruct")


env.Append(
    CPPDEFINES=[
        "HAVE_CONFIG_H",
        "PACKAGE=",
        "VERSION=",
        "CPU_CLIPS_POSITIVE=0",
        "CPU_CLIPS_NEGATIVE=0",
        "WEBRTC_APM_DEBUG_DUMP=0",
        "GLOG_NO_ABBREVIATED_SEVERITIES",
    ]
)

env.Append(CXXFLAGS=["/W0", "/wd4244", "/bigobj", "/wd4267", "/D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS"])

env.Prepend(CPPPATH=["thirdparty", "include","thirdparty/SummerTTS/eigen-3.4.0",
                     "thirdparty/SummerTTS/src/tn/header","thirdparty/SummerTTS/include",
                     "thirdparty/SummerTTS/src/header"])
env.Append(CPPPATH=["src/"])
sources = [Glob("src/*.cpp")]

sources.extend([
    Glob("thirdparty/libsamplerate/src/*.c"),
    Glob("thirdparty/SummerTTS/*.c"),
])


if env["platform"] == "macos" or env["platform"] == "ios":
    env.Append(LINKFLAGS=["-framework"])
    env.Append(LINKFLAGS=["Foundation"])
    env.Append(LINKFLAGS=["-framework"])
    env.Append(LINKFLAGS=["Metal"])
    env.Append(LINKFLAGS=["-framework"])
    env.Append(LINKFLAGS=["MetalKit"])
    env.Append(LINKFLAGS=["-framework"])
    env.Append(LINKFLAGS=["Accelerate"])
    env.Append(
        CPPDEFINES=[
            "GGML_USE_METAL",
            # Debug logs
            "GGML_METAL_NDEBUG",
            "GGML_USE_ACCELERATE"
        ]
    )
    sources.extend([
        Glob("thirdparty/whisper.cpp/ggml-metal.m"),
    ])
else:
    # CBlast and OpenCL only on non apple platform
    gflags_include_dir = os.environ.get('Gflags_INCLUDE_DIR')
    if gflags_include_dir:
        env.Append(CPPPATH=[gflags_include_dir])

    gflags_library = os.environ.get('Gflags_LIBRARY')
    if gflags_library:
        env.Append(LIBS=[gflags_library])

    glog_include_dir = os.environ.get('Glog_INCLUDE_DIR')
    if glog_include_dir:
        env.Append(CPPPATH=[glog_include_dir])

    glog_library = os.environ.get('Glog_LIBRARY')
    if glog_library:
        env.Append(LIBS=[glog_library])

    summertts_sources = [
        'thirdparty/SummerTTS/src/engipa/ipa.cpp',
        'thirdparty/SummerTTS/src/engipa/alphabet.cpp',
        "thirdparty/SummerTTS/src/tn/openfst/src/lib/compat.cc",
        "thirdparty/SummerTTS/src/tn/openfst/src/lib/flags.cc",
        "thirdparty/SummerTTS/src/tn/openfst/src/lib/fst.cc",
        "thirdparty/SummerTTS/src/tn/openfst/src/lib/fst-types.cc",
        "thirdparty/SummerTTS/src/tn/openfst/src/lib/mapped-file.cc",
        "thirdparty/SummerTTS/src/tn/openfst/src/lib/properties.cc",
        "thirdparty/SummerTTS/src/tn/openfst/src/lib/symbol-table.cc",
        "thirdparty/SummerTTS/src/tn/openfst/src/lib/symbol-table-ops.cc",
        "thirdparty/SummerTTS/src/tn/openfst/src/lib/util.cc",
        "thirdparty/SummerTTS/src/tn/openfst/src/lib/weight.cc",
        "thirdparty/SummerTTS/src/tn/processor.cc",
        "thirdparty/SummerTTS/src/tn/token_parser.cc",
        "thirdparty/SummerTTS/src/tn/utf8_string.cc",
        "thirdparty/SummerTTS/src/engipa/EnglishText2Id.cpp",
        "thirdparty/SummerTTS/src/engipa/InitIPASymbols.cpp",
        "thirdparty/SummerTTS/src/hz2py/hanzi2phoneid.cpp",
        "thirdparty/SummerTTS/src/hz2py/Hanz2Piny.cpp",
        "thirdparty/SummerTTS/src/hz2py/pinyinmap.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_conv1d.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_softmax.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_layer_norm.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_relu.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_gelu.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_tanh.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_flip.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_cumsum.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_softplus.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_clamp_min.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_sigmoid.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_conv1d_transposed.cpp",
        "thirdparty/SummerTTS/src/nn_op/nn_leaky_relu.cpp",
        "thirdparty/SummerTTS/src/platform/tts_file_io.cpp",
        "thirdparty/SummerTTS/src/platform/tts_logger.cpp",
        "thirdparty/SummerTTS/src/utils/utils.cpp",
        "thirdparty/SummerTTS/src/modules/iStft.cpp",
        "thirdparty/SummerTTS/src/modules/hann.cpp",
        "thirdparty/SummerTTS/src/modules/attention_encoder.cpp",
        "thirdparty/SummerTTS/src/modules/multi_head_attention.cpp",
        "thirdparty/SummerTTS/src/modules/ffn.cpp",
        "thirdparty/SummerTTS/src/modules/ConvFlow.cpp",
        "thirdparty/SummerTTS/src/modules/DDSConv.cpp",
        "thirdparty/SummerTTS/src/modules/ElementwiseAffine.cpp",
        "thirdparty/SummerTTS/src/modules/random_gen.cpp",
        "thirdparty/SummerTTS/src/modules/ResidualCouplingLayer.cpp",
        "thirdparty/SummerTTS/src/modules/ResBlock1.cpp",
        "thirdparty/SummerTTS/src/modules/WN.cpp",
        "thirdparty/SummerTTS/src/modules/pqmf.cpp",
        "thirdparty/SummerTTS/src/models/TextEncoder.cpp",
        "thirdparty/SummerTTS/src/models/StochasticDurationPredictor.cpp",
        "thirdparty/SummerTTS/src/models/FixDurationPredictor.cpp",
        "thirdparty/SummerTTS/src/models/DurationPredictor_base.cpp",
        "thirdparty/SummerTTS/src/models/ResidualCouplingBlock.cpp",
        "thirdparty/SummerTTS/src/models/Generator_base.cpp",
        "thirdparty/SummerTTS/src/models/Generator_hifigan.cpp",
        "thirdparty/SummerTTS/src/models/Generator_MS.cpp",
        "thirdparty/SummerTTS/src/models/Generator_Istft.cpp",
        "thirdparty/SummerTTS/src/models/Generator_MBB.cpp",
        "thirdparty/SummerTTS/src/models/SynthesizerTrn.cpp",
    ]

    sources.extend(summertts_sources)

if env["platform"] == "macos":
	library = env.SharedLibrary(
		"bin/addons/godot_tts/bin/libgodot_tts{}.framework/libgodot_tts{}".format(
			env["suffix"], env["suffix"]
		),
		source=sources,
	)
else:
	library = env.SharedLibrary(
		"bin/addons/godot_tts/bin/libgodot_tts{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
		source=sources,
	)
Default(library)
