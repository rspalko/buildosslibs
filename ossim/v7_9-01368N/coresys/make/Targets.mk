clean:
	rm -f *.o *.so *.a *.dll

GENERIC_OBJS = block_coding_common.o block_decoder.o block_encoder.o decoder.o encoder.o mq_decoder.o mq_encoder.o blocks.o codestream.o compressed.o kernels.o messaging.o params.o colour.o analysis.o synthesis.o multi_transform.o roi.o kdu_arch.o kdu_threads.o

SSSE3_OBJS = ssse3_coder_local.o ssse3_colour_local.o ssse3_dwt_local.o
SSE4_OBJS = sse4_multi_transform_local.o
AVX_OBJS = avx_coder_local.o avx_colour_local.o
AVX2_OBJS = avx2_coder_local.o avx2_colour_local.o avx2_dwt_local.o

NEON_OBJS = neon_coder_local.o neon_colour_local.o neon_dwt_local.o neon_multi_transform_local.o

ALL_OBJS = $(GENERIC_OBJS)
ifdef INCLUDE_SSSE3
  ALL_OBJS += $(SSSE3_OBJS)
endif
ifdef INCLUDE_SSE4
  ALL_OBJS += $(SSE4_OBJS)
endif
ifdef INCLUDE_AVX
  ALL_OBJS += $(AVX_OBJS)
endif
ifdef INCLUDE_AVX2
  ALL_OBJS += $(AVX2_OBJS)
endif
ifdef INCLUDE_NEON
  ALL_OBJS += $(NEON_OBJS)
endif

$(STATIC_LIB_NAME) :: $(ALL_OBJS)
	ar -rv $(LIB_DIR)/$(STATIC_LIB_NAME) $(ALL_OBJS)
	ranlib $(LIB_DIR)/$(STATIC_LIB_NAME)

$(SHARED_LIB_NAME) :: $(ALL_OBJS) $(LIB_RESOURCES)
	$(CC) $(CFLAGS) -shared -o $(LIB_DIR)/$(SHARED_LIB_NAME) \
	      $(ALL_OBJS) $(LIB_RESOURCES)

kdu_arch.o :: ../common/kdu_arch.cpp
	$(CC) $(CFLAGS) -c ../common/kdu_arch.cpp \
	      -o kdu_arch.o

kdu_threads.o :: ../threads/kdu_threads.cpp
	$(CC) $(CFLAGS) -c ../threads/kdu_threads.cpp \
	      -o kdu_threads.o

mq_encoder.o :: ../coding/mq_encoder.cpp
	$(CC) $(CFLAGS) -c ../coding/mq_encoder.cpp \
	      -o mq_encoder.o
mq_decoder.o :: ../coding/mq_decoder.cpp
	$(CC) $(CFLAGS) -c ../coding/mq_decoder.cpp \
	      -o mq_decoder.o

block_coding_common.o :: ../coding/block_coding_common.cpp
	$(CC) $(CFLAGS) -c ../coding/block_coding_common.cpp \
	      -o block_coding_common.o
block_encoder.o :: ../coding/block_encoder.cpp
	$(CC) $(CFLAGS) -c ../coding/block_encoder.cpp \
	      -o block_encoder.o
block_decoder.o :: ../coding/block_decoder.cpp
	$(CC) $(CFLAGS) -c ../coding/block_decoder.cpp \
	      -o block_decoder.o

encoder.o :: ../coding/encoder.cpp
	$(CC) $(CFLAGS) -c ../coding/encoder.cpp \
	      -o encoder.o
decoder.o :: ../coding/decoder.cpp
	$(CC) $(CFLAGS) -c ../coding/decoder.cpp \
	      -o decoder.o
ssse3_coder_local.o :: ../coding/ssse3_coder_local.cpp
	$(CC) $(CFLAGS) $(SSSE3FLAGS) -c ../coding/ssse3_coder_local.cpp \
	      -o ssse3_coder_local.o
avx_coder_local.o :: ../coding/avx_coder_local.cpp
	$(CC) $(CFLAGS) $(AVXFLAGS) -c ../coding/avx_coder_local.cpp \
	      -o avx_coder_local.o
avx2_coder_local.o :: ../coding/avx2_coder_local.cpp
	$(CC) $(CFLAGS) $(AVX2FLAGS) -c ../coding/avx2_coder_local.cpp \
	      -o avx2_coder_local.o
neon_coder_local.o :: ../coding/neon_coder_local.cpp
	$(CC) $(CFLAGS) $(NEONFLAGS) -c ../coding/neon_coder_local.cpp \
	      -o neon_coder_local.o

codestream.o :: ../compressed/codestream.cpp
	$(CC) $(CFLAGS) -c ../compressed/codestream.cpp \
	      -o codestream.o
compressed.o :: ../compressed/compressed.cpp
	$(CC) $(CFLAGS) -c ../compressed/compressed.cpp \
	      -o compressed.o
blocks.o :: ../compressed/blocks.cpp
	$(CC) $(CFLAGS) -c ../compressed/blocks.cpp \
	      -o blocks.o

kernels.o :: ../kernels/kernels.cpp
	$(CC) $(CFLAGS) -c ../kernels/kernels.cpp \
	      -o kernels.o

messaging.o :: ../messaging/messaging.cpp
	$(CC) $(CFLAGS) -c ../messaging/messaging.cpp \
	      -o messaging.o

params.o :: ../parameters/params.cpp
	$(CC) $(CFLAGS) -c ../parameters/params.cpp \


colour.o :: ../transform/colour.cpp
	$(CC) $(CFLAGS) -c ../transform/colour.cpp \
	      -o colour.o
ssse3_colour_local.o :: ../transform/ssse3_colour_local.cpp
	$(CC) $(CFLAGS) $(SSSE3FLAGS) -c ../transform/ssse3_colour_local.cpp \
	      -o ssse3_colour_local.o
avx_colour_local.o :: ../transform/avx_colour_local.cpp
	$(CC) $(CFLAGS) $(AVXFLAGS) -c ../transform/avx_colour_local.cpp \
	      -o avx_colour_local.o
avx2_colour_local.o :: ../transform/avx2_colour_local.cpp
	$(CC) $(CFLAGS) $(AVX2FLAGS) -c ../transform/avx2_colour_local.cpp \
	      -o avx2_colour_local.o
neon_colour_local.o :: ../transform/neon_colour_local.cpp
	$(CC) $(CFLAGS) $(NEONFLAGS) -c ../transform/neon_colour_local.cpp \
	      -o neon_colour_local.o

analysis.o :: ../transform/analysis.cpp
	$(CC) $(CFLAGS) -c ../transform/analysis.cpp \
	      -o analysis.o
synthesis.o :: ../transform/synthesis.cpp
	$(CC) $(CFLAGS) -c ../transform/synthesis.cpp \
	      -o synthesis.o
multi_transform.o :: ../transform/multi_transform.cpp
	$(CC) $(CFLAGS) -c ../transform/multi_transform.cpp \
	      -o multi_transform.o
ssse3_dwt_local.o :: ../transform/ssse3_dwt_local.cpp
	$(CC) $(CFLAGS) $(SSSE3FLAGS) -c ../transform/ssse3_dwt_local.cpp \
	      -o ssse3_dwt_local.o
avx2_dwt_local.o :: ../transform/avx2_dwt_local.cpp
	$(CC) $(CFLAGS) $(AVX2FLAGS) -c ../transform/avx2_dwt_local.cpp \
	      -o avx2_dwt_local.o
neon_dwt_local.o :: ../transform/neon_dwt_local.cpp
	$(CC) $(CFLAGS) $(NEONFLAGS) -c ../transform/neon_dwt_local.cpp \
	      -o neon_dwt_local.o
sse4_multi_transform_local.o :: ../transform/sse4_multi_transform_local.cpp
	$(CC) $(CFLAGS) $(SSE4FLAGS) -c ../transform/sse4_multi_transform_local.cpp \
	      -o sse4_multi_transform_local.o
neon_multi_transform_local.o :: ../transform/neon_multi_transform_local.cpp
	$(CC) $(CFLAGS) $(NEONFLAGS) -c ../transform/neon_multi_transform_local.cpp \
	      -o neon_multi_transform_local.o

roi.o :: ../roi/roi.cpp
	$(CC) $(CFLAGS) -c ../roi/roi.cpp \
	      -o roi.o
