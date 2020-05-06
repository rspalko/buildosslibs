all :: kdu_compress kdu_expand kdu_buffered_compress kdu_buffered_expand kdu_transcoder kdu_jp2info kdu_maketlm kdu_render simple_example_c simple_example_d kdu_v_compress kdu_v_expand kdu_merge kdu_server kdu_server_admin kdu_vex_fast kdu_vcom_fast kdu_hyperdoc kdu_text_extractor

all_but_hyperdoc :: kdu_compress kdu_expand kdu_buffered_compress kdu_buffered_expand kdu_transcoder kdu_jp2info kdu_maketlm kdu_render simple_example_c simple_example_d kdu_v_compress kdu_v_expand kdu_merge kdu_server kdu_server_admin kdu_vex_fast kdu_vcom_fast kdu_text_extractor

clean:
	rm -f *.o *.so *.dll *.a

SIMD_STRIPE_TRANSFER_OBJS =
SIMD_REGION_DECOMPRESSOR_OBJS =
SIMD_REGION_COMPOSITOR_OBJS =
SIMD_VEX_TRANSFER_OBJS =
ifdef INCLUDE_SSSE3
  SIMD_STRIPE_TRANSFER_OBJS += ssse3_stripe_transfer.o
  SIMD_REGION_DECOMPRESSOR_OBJS += ssse3_region_decompressor.o
endif
ifdef INCLUDE_SSE4
  SIMD_REGION_DECOMPRESSOR_OBJS += sse4_region_decompressor.o
endif
ifdef INCLUDE_AVX2
  SIMD_STRIPE_TRANSFER_OBJS += avx2_stripe_transfer.o
  SIMD_REGION_DECOMPRESSOR_OBJS += avx2_region_decompressor.o
  SIMD_REGION_COMPOSITOR_OBJS += avx2_region_compositor.o
  SIMD_VEX_TRANSFER_OBJS += avx2_vex_transfer.o
endif
ifdef INCLUDE_NEON
  SIMD_STRIPE_TRANSFER_OBJS += neon_stripe_transfer.o
  SIMD_REGION_DECOMPRESSOR_OBJS += neon_region_decompressor.o
  SIMD_REGION_COMPOSITOR_OBJS += neon_region_compositor.o
endif

kdu_compress :: args.o image_in.o kdu_tiff.o palette.o jp2.o jpx.o kdu_client_window.o roi_sources.o ../kdu_compress/kdu_compress.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_compress/kdu_compress.cpp \
	      args.o image_in.o kdu_tiff.o palette.o jp2.o jpx.o \
	      kdu_client_window.o roi_sources.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_compress $(LIBS) $(LDFLAGS) $(TIFF_LIBS)

kdu_expand :: args.o image_out.o kdu_tiff.o jp2.o jpx.o kdu_client_window.o ../kdu_expand/kdu_expand.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_expand/kdu_expand.cpp \
	      args.o image_out.o kdu_tiff.o jp2.o jpx.o \
	      kdu_client_window.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_expand $(LIBS) $(LDFLAGS)

kdu_buffered_compress :: kdu_stripe_compressor.o $(SIMD_STRIPE_TRANSFER_OBJS) args.o jp2.o ../kdu_buffered_compress/kdu_buffered_compress.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_buffered_compress/kdu_buffered_compress.cpp \
	      args.o jp2.o kdu_stripe_compressor.o \
	      $(SIMD_STRIPE_TRANSFER_OBJS) $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_buffered_compress $(LIBS) $(LDFLAGS)

kdu_buffered_expand :: kdu_stripe_decompressor.o $(SIMD_STRIPE_TRANSFER_OBJS) args.o jp2.o ../kdu_buffered_expand/kdu_buffered_expand.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_buffered_expand/kdu_buffered_expand.cpp \
	      args.o jp2.o kdu_stripe_decompressor.o \
	      $(SIMD_STRIPE_TRANSFER_OBJS) $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_buffered_expand $(LIBS) $(LDFLAGS)

kdu_transcoder :: args.o jp2.o jpx.o kdu_client_window.o ../kdu_transcode/kdu_transcode.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) ../kdu_transcode/kdu_transcode.cpp \
	      args.o jp2.o jpx.o kdu_client_window.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_transcode $(LIBS) $(LDFLAGS)

kdu_jp2info :: args.o jp2.o jpx.o jpb.o mj2.o kdu_client_window.o ../kdu_jp2info/kdu_jp2info.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_jp2info/kdu_jp2info.cpp \
	      args.o jp2.o jpx.o jpb.o mj2.o \
	      kdu_client_window.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_jp2info $(LIBS) $(LDFLAGS)

kdu_maketlm :: ../kdu_maketlm/kdu_maketlm.cpp ../../coresys/messaging/messaging.cpp
	$(CC) $(CFLAGS) \
	      ../kdu_maketlm/kdu_maketlm.cpp \
	      ../../coresys/messaging/messaging.cpp \
	      -o $(BIN_DIR)/kdu_maketlm $(LIBS) $(LDFLAGS)

kdu_render :: kdu_region_decompressor.o $(SIMD_REGION_DECOMPRESSOR_OBJS) kdu_region_compositor.o  $(SIMD_REGION_COMPOSITOR_OBJS) args.o jp2.o jpx.o mj2.o kdu_client_window.o ../kdu_render/kdu_render.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_render/kdu_render.cpp \
	      args.o kdu_region_decompressor.o kdu_region_compositor.o \
	      $(SIMD_REGION_DECOMPRESSOR_OBJS) $(SIMD_REGION_COMPOSITOR_OBJS) \
	      jp2.o jpx.o mj2.o kdu_client_window.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_render $(LIBS) $(LDFLAGS)

kdu_v_compress :: args.o mj2.o jp2.o jpb.o jpx.o kdu_client_window.o $(SIMD_STRIPE_TRANSFER_OBJS) ../kdu_v_compress/kdu_v_compress.cpp ../kdu_v_compress/v_compress_jpx.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_v_compress/kdu_v_compress.cpp \
	      ../kdu_v_compress/v_compress_jpx.cpp \
	      $(SIMD_STRIPE_TRANSFER_OBJS) \
	      args.o mj2.o jp2.o jpb.o jpx.o \
	      kdu_client_window.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_v_compress $(LIBS) $(LDFLAGS)

kdu_v_expand :: args.o mj2.o jp2.o jpb.o jpx.o kdu_client_window.o $(SIMD_STRIPE_TRANSFER_OBJS) ../kdu_v_expand/kdu_v_expand.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) ../kdu_v_expand/kdu_v_expand.cpp \
	      $(SIMD_STRIPE_TRANSFER_OBJS) \
	      args.o mj2.o jp2.o jpb.o jpx.o \
	      kdu_client_window.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_v_expand $(LIBS) $(LDFLAGS)

kdu_merge :: args.o mj2.o jp2.o jpx.o kdu_client_window.o ../kdu_merge/kdu_merge.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) ../kdu_merge/kdu_merge.cpp \
	      args.o mj2.o jp2.o jpx.o kdu_client_window.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_merge $(LIBS) $(LDFLAGS)

kdu_server :: args.o jp2.o kdcs_comms.o kdu_client_window.o kdu_security.o ../kdu_server/kdu_server.cpp ../kdu_server/connection.cpp ../kdu_server/sources.cpp ../kdu_server/kdu_serve.cpp ../kdu_server/kdu_servex.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -I../jp2 ../kdu_server/kdu_server.cpp \
	      ../kdu_server/connection.cpp \
	      ../kdu_server/sources.cpp ../kdu_server/kdu_serve.cpp \
	      ../kdu_server/kdu_servex.cpp args.o jp2.o kdcs_comms.o \
	      kdu_client_window.o kdu_security.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_server $(LIBS) $(LDFLAGS) $(NETLIBS)

kdu_server_admin :: args.o kdcs_comms.o kdu_security.o ../kdu_server_admin/kdu_server_admin.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) -I../kdu_server \
	      ../kdu_server_admin/kdu_server_admin.cpp \
	      args.o kdcs_comms.o kdu_security.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_server_admin $(LIBS) $(LDFLAGS) $(NETLIBS)

simple_example_c :: kdu_stripe_compressor.o $(SIMD_STRIPE_TRANSFER_OBJS) ../simple_example/simple_example_c.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS)\
	      ../simple_example/simple_example_c.cpp \
	      kdu_stripe_compressor.o \
	      $(SIMD_STRIPE_TRANSFER_OBJS) $(LIB_SRC) \
	      -o $(BIN_DIR)/simple_example_c $(LIBS) $(LDFLAGS)

simple_example_d :: kdu_stripe_decompressor.o $(SIMD_STRIPE_TRANSFER_OBJS) ../simple_example/simple_example_d.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) ../simple_example/simple_example_d.cpp \
	      kdu_stripe_decompressor.o \
	      $(SIMD_STRIPE_TRANSFER_OBJS) $(LIB_SRC) \
	      -o $(BIN_DIR)/simple_example_d $(LIBS) $(LDFLAGS)

kdu_vex_fast :: args.o jp2.o mj2.o jpx.o kdu_client_window.o $(SIMD_STRIPE_TRANSFER_OBJS) $(SIMD_VEX_TRANSFER_OBJS) ../kdu_vex_fast/kdu_vex_fast.cpp ../kdu_vex_fast/kdu_vex.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_vex_fast/kdu_vex_fast.cpp \
	      ../kdu_vex_fast/kdu_vex.cpp \
	      $(SIMD_STRIPE_TRANSFER_OBJS) $(SIMD_VEX_TRANSFER_OBJS) \
	      args.o jp2.o mj2.o jpx.o kdu_client_window.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_vex_fast $(LIBS) $(LDFLAGS)

kdu_vcom_fast :: args.o jp2.o mj2.o jpx.o kdu_client_window.o $(SIMD_STRIPE_TRANSFER_OBJS) kdu_stripe_compressor.o ../kdu_vcom_fast/kdu_vcom_fast.cpp ../kdu_vcom_fast/kdu_vcom.cpp ../kdu_vcom_fast/vcom_compress_jpx.cpp $(LIB_SRC)
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      ../kdu_vcom_fast/kdu_vcom_fast.cpp \
	      ../kdu_vcom_fast/kdu_vcom.cpp \
	      ../kdu_vcom_fast/vcom_compress_jpx.cpp \
	      $(SIMD_STRIPE_TRANSFER_OBJS) kdu_stripe_compressor.o \
	      args.o jp2.o mj2.o jpx.o kdu_client_window.o $(LIB_SRC) \
	      -o $(BIN_DIR)/kdu_vcom_fast $(LIBS) $(LDFLAGS)

kdu_hyperdoc :: ../kdu_hyperdoc/kdu_hyperdoc.cpp ../kdu_hyperdoc/jni_builder.cpp ../kdu_hyperdoc/mni_builder.cpp ../kdu_hyperdoc/aux_builder.cpp ../../coresys/messaging/messaging.cpp ../args/args.cpp
	$(CC) $(CFLAGS) \
	      ../kdu_hyperdoc/kdu_hyperdoc.cpp \
	      ../kdu_hyperdoc/jni_builder.cpp \
	      ../kdu_hyperdoc/mni_builder.cpp \
	      ../kdu_hyperdoc/aux_builder.cpp \
	      ../../coresys/messaging/messaging.cpp \
	      ../args/args.cpp \
	      -o $(BIN_DIR)/kdu_hyperdoc $(LIBS) $(LDFLAGS)
	echo Building Documentation and Java Native API ...
	cd ../../documentation;	\
	   ../apps/make/$(BIN_DIR)/kdu_hyperdoc -o html_pages -s hyperdoc.src \
	      -java ../../java/kdu_jni ../managed/kdu_jni ../managed/kdu_aux \
	     ../managed/all_includes

kdu_text_extractor :: ../kdu_text_extractor/kdu_text_extractor.cpp ../../coresys/messaging/messaging.cpp ../args/args.cpp
	$(CC) $(CFLAGS) ../kdu_text_extractor/kdu_text_extractor.cpp \
	      ../../coresys/messaging/messaging.cpp \
	      ../args/args.cpp \
	      -o $(BIN_DIR)/kdu_text_extractor $(LIBS) $(LDFLAGS)
	cd ../../language; \
	   ../apps/make/$(BIN_DIR)/kdu_text_extractor -quiet -s coresys.src; \
	   ../apps/make/$(BIN_DIR)/kdu_text_extractor -quiet -s jp2.src; \
	   ../apps/make/$(BIN_DIR)/kdu_text_extractor -quiet -s jpx.src; \
	   ../apps/make/$(BIN_DIR)/kdu_text_extractor -quiet -s mj2.src; \
	   ../apps/make/$(BIN_DIR)/kdu_text_extractor -quiet -s jpb.src; \
	   ../apps/make/$(BIN_DIR)/kdu_text_extractor -quiet -s client.src; \
	   ../apps/make/$(BIN_DIR)/kdu_text_extractor -quiet -s misc.src

$(LIB_SRC) :: $(LIB_DIR)/$(LIB_SRC)
	cp $(LIB_DIR)/$(LIB_SRC) .

args.o :: ../args/args.cpp
	$(CC) $(CFLAGS) $(TIFF_FLAGS) $(LIB_IMPORTS) \
	      -c ../args/args.cpp \
	      -o args.o

image_in.o :: ../image/image_in.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../image/image_in.cpp \
	      -o image_in.o

image_out.o :: ../image/image_out.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../image/image_out.cpp \
	      -o image_out.o

kdu_tiff.o :: ../image/kdu_tiff.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../image/kdu_tiff.cpp \
	      -o kdu_tiff.o

palette.o :: ../image/palette.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../image/palette.cpp \
	      -o palette.o

kdu_region_decompressor.o :: ../support/kdu_region_decompressor.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../support/kdu_region_decompressor.cpp \
	      -o kdu_region_decompressor.o

ssse3_region_decompressor.o :: ../support/ssse3_region_decompressor.cpp
	$(CC) $(CFLAGS) $(SSSE3FLAGS) $(LIB_IMPORTS) \
	      -c ../support/ssse3_region_decompressor.cpp \
	      -o ssse3_region_decompressor.o

sse4_region_decompressor.o :: ../support/sse4_region_decompressor.cpp
	$(CC) $(CFLAGS) $(SSE4FLAGS) $(LIB_IMPORTS) \
	      -c ../support/sse4_region_decompressor.cpp \
	      -o sse4_region_decompressor.o

avx2_region_decompressor.o :: ../support/avx2_region_decompressor.cpp
	$(CC) $(CFLAGS) $(AVX2FLAGS) $(LIB_IMPORTS) \
	      -c ../support/avx2_region_decompressor.cpp \
	      -o avx2_region_decompressor.o

neon_region_decompressor.o :: ../support/neon_region_decompressor.cpp
	$(CC) $(CFLAGS) $(NEONFLAGS) $(LIB_IMPORTS) \
	      -c ../support/neon_region_decompressor.cpp \
	      -o neon_region_decompressor.o

kdu_region_compositor.o :: ../support/kdu_region_compositor.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../support/kdu_region_compositor.cpp \
	      -o kdu_region_compositor.o

avx2_region_compositor.o :: ../support/avx2_region_compositor.cpp
	$(CC) $(CFLAGS) $(AVX2FLAGS) $(LIB_IMPORTS) \
	      -c ../support/avx2_region_compositor.cpp \
	      -o avx2_region_compositor.o

neon_region_compositor.o :: ../support/neon_region_compositor.cpp
	$(CC) $(CFLAGS) $(NEONFLAGS) $(LIB_IMPORTS) \
	      -c ../support/neon_region_compositor.cpp \
	      -o neon_region_compositor.o

kdu_stripe_decompressor.o :: ../support/kdu_stripe_decompressor.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../support/kdu_stripe_decompressor.cpp \
	      -o kdu_stripe_decompressor.o

kdu_stripe_compressor.o :: ../support/kdu_stripe_compressor.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../support/kdu_stripe_compressor.cpp \
	      -o kdu_stripe_compressor.o

ssse3_stripe_transfer.o :: ../support/ssse3_stripe_transfer.cpp
	$(CC) $(CFLAGS) $(SSSE3FLAGS) $(LIB_IMPORTS) \
	      -c ../support/ssse3_stripe_transfer.cpp \
	      -o ssse3_stripe_transfer.o

avx2_stripe_transfer.o :: ../support/avx2_stripe_transfer.cpp
	$(CC) $(CFLAGS) $(AVX2FLAGS) $(LIB_IMPORTS) \
	      -c ../support/avx2_stripe_transfer.cpp \
	      -o avx2_stripe_transfer.o

neon_stripe_transfer.o :: ../support/neon_stripe_transfer.cpp
	$(CC) $(CFLAGS) $(NEONFLAGS) $(LIB_IMPORTS) \
	      -c ../support/neon_stripe_transfer.cpp \
	      -o neon_stripe_transfer.o

avx2_vex_transfer.o :: ../kdu_vex_fast/avx2_vex_transfer.cpp
	$(CC) $(CFLAGS) $(AVX2FLAGS) $(LIB_IMPORTS) \
	      -c ../kdu_vex_fast/avx2_vex_transfer.cpp \
	      -o avx2_vex_transfer.o

jp2.o :: ../jp2/jp2.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../jp2/jp2.cpp \
	      -o jp2.o

jpx.o :: ../jp2/jpx.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../jp2/jpx.cpp \
	      -o jpx.o

mj2.o :: ../jp2/mj2.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../jp2/mj2.cpp \
	      -o mj2.o

jpb.o :: ../jp2/jpb.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../jp2/jpb.cpp \
	      -o jpb.o

roi_sources.o :: ../kdu_compress/roi_sources.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../kdu_compress/roi_sources.cpp \
	      -o roi_sources.o

kdcs_comms.o :: ../client_server/kdcs_comms.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../client_server/kdcs_comms.cpp \
	      -o kdcs_comms.o

kdu_client_window.o :: ../client_server/kdu_client_window.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../client_server/kdu_client_window.cpp \
	      -o kdu_client_window.o

kdu_security.o :: ../kdu_server/kdu_security.cpp
	$(CC) $(CFLAGS) $(LIB_IMPORTS) \
	      -c ../kdu_server/kdu_security.cpp \
	      -o kdu_security.o
