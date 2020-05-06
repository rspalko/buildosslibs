JP2_OBJS     = jp2.o jpb.o jpx.o mj2.o
IMAGE_OBJS   = kdu_tiff.o
SUPPORT_OBJS = kdu_region_compositor.o kdu_region_decompressor.o kdu_stripe_compressor.o kdu_stripe_decompressor.o
CACHE_OBJS   = kdu_cache.o
CLIENT_SERVER_OBJS = kdu_client_window.o kdcs_comms.o
CLIENT_OBJS  = kdu_client.o kdu_clientx.o
SERVER_OBJS  = kdu_serve.o kdu_servex.o
ROI_OBJS     = roi_sources.o

SIMD_ACCELERATED_OBJS =
ifdef INCLUDE_SSSE3
  SIMD_ACCELERATED_OBJS += ssse3_stripe_transfer.o
  SIMD_ACCELERATED_OBJS += ssse3_region_decompressor.o
endif
ifdef INCLUDE_SSE4
  SIMD_ACCELERATED_OBJS += sse4_region_decompressor.o
endif
ifdef INCLUDE_AVX2
  SIMD_ACCELERATED_OBJS += avx2_stripe_transfer.o
  SIMD_ACCELERATED_OBJS += avx2_region_decompressor.o
  SIMD_ACCELERATED_OBJS += avx2_region_compositor.o
endif
ifdef INCLUDE_NEON
  SIMD_ACCELERATED_OBJS += neon_stripe_transfer.o
  SIMD_ACCELERATED_OBJS += neon_region_decompressor.o
  SIMD_ACCELERATED_OBJS += neon_region_compositor.o
endif

ALL_OBJS = $(JP2_OBJS) $(IMAGE_OBJS) $(SUPPORT_OBJS)
ALL_OBJS += $(CACHE_OBJS) $(CLIENT_SERVER_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS)
ALL_OBJS += $(ROI_OBJS)
ALL_OBJS += $(SIMD_ACCELERATED_OBJS)
ALL_OBJS += args.o

clean:
	rm -f *.o *.so *.dll *.a rm *.jnilib

kdu_hyperdoc :: ../../apps/kdu_hyperdoc/kdu_hyperdoc.cpp ../../apps/kdu_hyperdoc/jni_builder.cpp ../../apps/kdu_hyperdoc/mni_builder.cpp ../../apps/kdu_hyperdoc/aux_builder.cpp ../../coresys/messaging/messaging.cpp ../../apps/args/args.cpp
	$(CC) $(C_OPT) \
	      -I../../coresys/common -I../../apps/args \
	      ../../apps/kdu_hyperdoc/kdu_hyperdoc.cpp \
	      ../../apps/kdu_hyperdoc/jni_builder.cpp \
	      ../../apps/kdu_hyperdoc/mni_builder.cpp \
	      ../../apps/kdu_hyperdoc/aux_builder.cpp \
	      ../../coresys/messaging/messaging.cpp \
	      ../../apps/args/args.cpp \
	      -o $(BIN_DIR)/kdu_hyperdoc $(LIBS)
	echo Building Documentation and Java Native API ...
	cd ../../documentation;	\
	   ../managed/make/$(BIN_DIR)/kdu_hyperdoc \
	     -o html_pages -s hyperdoc.src \
	     -java ../../java/kdu_jni ../managed/kdu_jni ../managed/kdu_aux \
	     ../managed/all_includes

$(AUX_SHARED_LIB_NAME) :: ../kdu_aux/kdu_aux.cpp $(ALL_OBJS) $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_aux/kdu_aux.cpp \
	      $(ALL_OBJS) $(LIB_SRC) \
	      -shared -o $(AUX_SHARED_LIB_NAME) $(LIBS) $(LDFLAGS) $(NETLIBS)
	cp $(AUX_SHARED_LIB_NAME) $(LIB_DIR)

$(AUX_STATIC_LIB_NAME) :: ../kdu_aux/kdu_aux.cpp $(ALL_OBJS)
	ar -rv $(LIB_DIR)/$(AUX_STATIC_LIB_NAME) $(ALL_OBJS)
	ranlib $(LIB_DIR)/$(AUX_STATIC_LIB_NAME)

$(JNI_LIB_NAME) :: ../kdu_jni/kdu_jni.cpp ../kdu_aux/kdu_aux.cpp $(ALL_OBJS) $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_jni/kdu_jni.cpp ../kdu_aux/kdu_aux.cpp \
	      $(ALL_OBJS) $(LIB_SRC) -fno-strict-aliasing \
	      -shared $(JNI_LINK_FLAGS) \
	      -o $(JNI_LIB_NAME) $(LIBS) $(NETLIBS)
	cp $(JNI_LIB_NAME) $(LIB_DIR)

args.o :: ../../apps/args/args.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -I../../coresys/common \
	      -c ../../apps/args/args.cpp -o args.o

jp2.o :: ../../apps/jp2/jp2.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/jp2/jp2.cpp \
	      -o jp2.o

jpb.o :: ../../apps/jp2/jpb.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/jp2/jpb.cpp \
	      -o jpb.o

jpx.o :: ../../apps/jp2/jpx.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/jp2/jpx.cpp \
	      -o jpx.o

mj2.o :: ../../apps/jp2/mj2.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/jp2/mj2.cpp \
	      -o mj2.o

kdu_tiff.o :: ../../apps/image/kdu_tiff.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/image/kdu_tiff.cpp \
	      -o kdu_tiff.o

kdu_region_decompressor.o :: ../../apps/support/kdu_region_decompressor.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/kdu_region_decompressor.cpp \
	      -o kdu_region_decompressor.o

ssse3_region_decompressor.o :: ../../apps/support/ssse3_region_decompressor.cpp
	$(CC) $(CFLAGS) $(SSSE3FLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/ssse3_region_decompressor.cpp \
	      -o ssse3_region_decompressor.o

sse4_region_decompressor.o :: ../../apps/support/sse4_region_decompressor.cpp
	$(CC) $(CFLAGS) $(SSE4FLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/sse4_region_decompressor.cpp \
	      -o sse4_region_decompressor.o

avx2_region_decompressor.o :: ../../apps/support/avx2_region_decompressor.cpp
	$(CC) $(CFLAGS) $(AVX2FLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/avx2_region_decompressor.cpp \
	      -o avx2_region_decompressor.o

neon_region_decompressor.o :: ../support/neon_region_decompressor.cpp
	$(CC) $(CFLAGS) $(NEONFLAGS) $(LIB_IMPORTS) \
	      -c ../support/neon_region_decompressor.cpp \
	      -o neon_region_decompressor.o

kdu_region_compositor.o :: ../../apps/support/kdu_region_compositor.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/kdu_region_compositor.cpp \
	      -o kdu_region_compositor.o

avx2_region_compositor.o :: ../../apps/support/avx2_region_compositor.cpp
	$(CC) $(CFLAGS) $(AVX2FLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/avx2_region_compositor.cpp \
	      -o avx2_region_compositor.o

neon_region_compositor.o :: ../../apps/support/neon_region_compositor.cpp
	$(CC) $(CFLAGS) $(NEONFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/neon_region_compositor.cpp \
	      -o neon_region_compositor.o

kdu_stripe_decompressor.o :: ../../apps/support/kdu_stripe_decompressor.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/kdu_stripe_decompressor.cpp \
	      -o kdu_stripe_decompressor.o

kdu_stripe_compressor.o :: ../../apps/support/kdu_stripe_compressor.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/kdu_stripe_compressor.cpp \
	      -o kdu_stripe_compressor.o

ssse3_stripe_transfer.o :: ../../apps/support/ssse3_stripe_transfer.cpp
	$(CC) $(CFLAGS) $(SSSE3FLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/ssse3_stripe_transfer.cpp \
	      -o ssse3_stripe_transfer.o

avx2_stripe_transfer.o :: ../../apps/support/avx2_stripe_transfer.cpp
	$(CC) $(CFLAGS) $(AVX2FLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/avx2_stripe_transfer.cpp \
	      -o avx2_stripe_transfer.o

neon_stripe_transfer.o :: ../../apps/support/neon_stripe_transfer.cpp
	$(CC) $(CFLAGS) $(NEONFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/support/neon_stripe_transfer.cpp \
	      -o neon_stripe_transfer.o

kdu_cache.o :: ../../apps/caching_sources/kdu_cache.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/caching_sources/kdu_cache.cpp \
	      -o kdu_cache.o

kdcs_comms.o :: ../../apps/client_server/kdcs_comms.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/client_server/kdcs_comms.cpp \
	      -o kdcs_comms.o

kdu_client_window.o :: ../../apps/client_server/kdu_client_window.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/client_server/kdu_client_window.cpp \
	      -o kdu_client_window.o

kdu_client.o :: ../../apps/kdu_client/kdu_client.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/kdu_client/kdu_client.cpp \
	      -o kdu_client.o

kdu_clientx.o :: ../../apps/kdu_client/kdu_clientx.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/kdu_client/kdu_clientx.cpp \
	      -o kdu_clientx.o

kdu_serve.o :: ../../apps/kdu_server/kdu_serve.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/kdu_server/kdu_serve.cpp \
	      -o kdu_serve.o

kdu_servex.o :: ../../apps/kdu_server/kdu_servex.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/kdu_server/kdu_servex.cpp \
	      -o kdu_servex.o

roi_sources.o :: ../../apps/kdu_compress/roi_sources.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../../apps/kdu_compress/roi_sources.cpp \
	      -o roi_sources.o
