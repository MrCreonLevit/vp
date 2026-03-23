# Top-level Makefile for Viewpoints

BUILD_DIR   := build
APP_BUNDLE  := $(BUILD_DIR)/vp.app
DIST_DIR    := dist

.PHONY: all clean app dmg

all:
	@cmake -S . -B $(BUILD_DIR)
	@cmake --build $(BUILD_DIR) -j$(shell sysctl -n hw.ncpu)

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)

# Build a standalone macOS .app bundle (Apple Silicon)
app:
	@cmake -S . -B $(BUILD_DIR)
	@rm -f $(APP_BUNDLE)/Contents/MacOS/vp
	@cmake --build $(BUILD_DIR) -j$(shell sysctl -n hw.ncpu)
	@echo "==> Bundling dynamic libraries..."
	@dylibbundler -od -b \
		-x $(APP_BUNDLE)/Contents/MacOS/vp \
		-d $(APP_BUNDLE)/Contents/Frameworks/ \
		-p @executable_path/../Frameworks/
	@echo "==> Fixing duplicate rpaths..."
	@for lib in $(APP_BUNDLE)/Contents/Frameworks/*.dylib; do \
		count=$$(otool -l "$$lib" 2>/dev/null | grep -c "@executable_path/../Frameworks/"); \
		if [ "$$count" -gt 1 ]; then \
			install_name_tool -delete_rpath "@executable_path/../Frameworks/" "$$lib" 2>/dev/null; \
			install_name_tool -add_rpath "@executable_path/../Frameworks/" "$$lib" 2>/dev/null; \
		fi; \
	done
	@echo "==> Ad-hoc code signing..."
	@codesign --force --deep --sign - $(APP_BUNDLE)
	@echo "==> Done: $(APP_BUNDLE)"

# Package as a DMG for distribution
dmg: app
	@mkdir -p $(DIST_DIR)
	@rm -f $(DIST_DIR)/Viewpoints.dmg
	@echo "==> Creating DMG..."
	@hdiutil create -volname Viewpoints \
		-srcfolder $(APP_BUNDLE) \
		-ov -format UDZO \
		$(DIST_DIR)/Viewpoints.dmg
	@echo "==> Done: $(DIST_DIR)/Viewpoints.dmg"
