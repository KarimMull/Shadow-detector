CC = gcc

CFLAGS = -Wall -Wextra -O2 \
	-IC:/msys64/ucrt64/include/SDL2

FFMPEG_LIBS = \
	-lavdevice \
	-lavformat \
	-lavcodec \
	-lswscale \
	-lavutil

SDL_LIBS = \
	-lSDL2 \
	-lSDL2_ttf

TARGET = camera_yuv.exe

OBJS = \
	camera_yuv.o \
	camera_device.o \
	shadow_detector.o \
	video_encoder.o \
	ui_controls.o

PORTABLE_DIR = portable


# ============================================================
# MAIN BUILD
# ============================================================

all: $(TARGET)


$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) \
		$(FFMPEG_LIBS) \
		$(SDL_LIBS)


camera_yuv.o: camera_yuv.c camera_device.h shadow_detector.h video_encoder.h ui_controls.h
	$(CC) $(CFLAGS) -c camera_yuv.c -o camera_yuv.o


camera_device.o: camera_device.c camera_device.h
	$(CC) $(CFLAGS) -c camera_device.c -o camera_device.o


shadow_detector.o: shadow_detector.c shadow_detector.h
	$(CC) $(CFLAGS) -c shadow_detector.c -o shadow_detector.o


video_encoder.o: video_encoder.c video_encoder.h
	$(CC) $(CFLAGS) -c video_encoder.c -o video_encoder.o


ui_controls.o: ui_controls.c ui_controls.h
	$(CC) $(CFLAGS) -c ui_controls.c -o ui_controls.o


# ============================================================
# CLEAN
# ============================================================

clean:
	rm -f $(OBJS) $(TARGET)


# ============================================================
# PORTABLE
# ============================================================

portable: $(TARGET)

	@echo
	@echo ========================================
	@echo Creating portable package...
	@echo ========================================
	@echo

	rm -rf "$(PORTABLE_DIR)"
	mkdir -p "$(PORTABLE_DIR)"

	@echo [1/4] Copying application...
	cp "$(TARGET)" "$(PORTABLE_DIR)/"

	@echo
	@echo [2/4] Finding DLL dependencies...
	ldd "$(TARGET)" > portable_ldd.txt

	@for dll in $$(ldd "$(TARGET)" | \
		sed -n 's/.*=> \(\/ucrt64\/bin\/[^ ]*\.dll\).*/\1/p'); do \
		if [ -f "$$dll" ]; then \
			echo "  Copying $$dll"; \
			cp -n "$$dll" "$(PORTABLE_DIR)/"; \
		fi; \
	done

	@echo
	@echo [3/4] Checking FFmpeg, SDL2 and runtime DLLs...

	@for dll in \
		"C:/msys64/ucrt64/bin/avcodec-63.dll" \
		"C:/msys64/ucrt64/bin/avdevice-63.dll" \
		"C:/msys64/ucrt64/bin/avfilter-12.dll" \
		"C:/msys64/ucrt64/bin/avformat-63.dll" \
		"C:/msys64/ucrt64/bin/avutil-61.dll" \
		"C:/msys64/ucrt64/bin/swresample-7.dll" \
		"C:/msys64/ucrt64/bin/swscale-10.dll" \
		"C:/msys64/ucrt64/bin/SDL2.dll" \
		"C:/msys64/ucrt64/bin/SDL2_ttf.dll" \
		"C:/msys64/ucrt64/bin/libgcc_s_seh-1.dll" \
		"C:/msys64/ucrt64/bin/libstdc++-6.dll" \
		"C:/msys64/ucrt64/bin/libwinpthread-1.dll"; do \
		if [ -f "$$dll" ]; then \
			cp -n "$$dll" "$(PORTABLE_DIR)/"; \
			echo "  OK: $$dll"; \
		else \
			echo "  NOT FOUND: $$dll"; \
		fi; \
	done

	@echo
	@echo [4/4] Creating README...

	@printf '%s\n' \
		'CAMERA YUV - PORTABLE VERSION' \
		'================================' \
		'' \
		'Run camera_yuv.exe' \
		'' \
		'FFmpeg and SDL2 DLLs are included.' \
		'' \
		'Place arial.ttf next to the exe if' \
		'no system font is found.' \
		> "$(PORTABLE_DIR)/README.txt"

	@echo
	@echo ========================================
	@echo PORTABLE PACKAGE CREATED
	@echo ========================================
	@ls -lh "$(PORTABLE_DIR)"


.PHONY: all clean portable