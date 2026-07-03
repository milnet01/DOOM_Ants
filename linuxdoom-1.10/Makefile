################################################################
#
# $Id:$
#
# $Log:$
#
CC=  gcc  # C engine: 1997 C
CXX= g++  # 3D renderer back-end (DOOM-0008): C++ behind the plain-C seam

# -std=gnu11: this is 1997 C89. gnu11 keeps the K&R idioms it relies on
# (empty () = unspecified args for the action-function table in info.c)
# that C23 — gcc's current default — would reject.
# SDL2 replaces the legacy X11 video / sndserver audio (DOOM-0004).
# SDL2_mixer renders the in-WAD music as General MIDI (DOOM-0016).
SDL_CFLAGS:=$(shell pkg-config --cflags sdl2 SDL2_mixer)
SDL_LIBS:=$(shell pkg-config --libs sdl2 SDL2_mixer)

# Vulkan: the 3D renderer back-end (DOOM-0008). pkg-config if present, else
# the bare loader. A machine without Vulkan still builds and runs Classic.
VULKAN_LIBS:=$(shell pkg-config --libs vulkan 2>/dev/null || echo -lvulkan)

# -MMD -MP: emit a per-object .d header-dependency file (and phony header targets),
# so editing a shared header (r_defs.h, r_mesh.h, ...) rebuilds exactly the objects
# that include it — no more full `make clean` after a header edit, and no silently
# stale object with a mismatched struct layout. The .d files are -include'd below.
DEPFLAGS=-MMD -MP
CFLAGS=-g -Wall -std=gnu11 -DNORMALUNIX -DLINUX $(DEPFLAGS) $(SDL_CFLAGS) # -DUSEASM
# C++23 for the Vulkan back-end (ADR 0001; bumped from C++17 2026-07-03 per the
# latest-version dependency rule -- no caller changes needed). Same defines + SDL
# include path. NB the Windows cross-build (target `windows`) inherits this flag;
# its mingw g++ 13 accepts -std=c++23 and the back-end uses no C++23-only features.
CXXFLAGS=-g -Wall -std=c++23 -DNORMALUNIX -DLINUX $(DEPFLAGS) $(SDL_CFLAGS)
# Link with mold when present (a faster drop-in linker) — native build only; the
# Windows cross-target overrides LDFLAGS, so the mingw link is untouched. Detected
# once; empty (= the default ld) when mold isn't installed.
MOLD:=$(shell command -v mold 2>/dev/null)
LDFLAGS=$(if $(MOLD),-fuse-ld=mold,)
LIBS=$(SDL_LIBS) $(VULKAN_LIBS) -lm

# Parallel compile, but capped by FREE RAM so the build can never exhaust memory and
# hang the machine. A heavy C++ TU here peaks ~300 MB; we budget a generous 1 GB/job,
# so jobs = min(cores, MemAvailable_GB), floor 1. On a roomy box this is all cores; on
# a tight-RAM box it auto-throttles down instead of OOM-thrashing. Override explicitly
# with `make -jN` (or `-j1` for serial). mold threads the LINK step separately (brief,
# low RAM) — this cap is only about the compile processes, the real memory risk.
# Safe to parallelise now that the .d files above complete the dependency graph.
JOBS:=$(shell n=$$(nproc 2>/dev/null || echo 4); \
	m=$$(awk '/MemAvailable/{print int($$2/1048576)}' /proc/meminfo 2>/dev/null); \
	[ -z "$$m" ] && m=$$n; j=$$m; [ "$$j" -gt "$$n" ] && j=$$n; [ "$$j" -lt 1 ] && j=1; echo $$j)
MAKEFLAGS+=-j$(JOBS)

# Output binary name and any platform-extra link libs. The Windows cross-build
# (target `windows`, DOOM-0006) overrides these via a recursive make.
BIN=linuxxdoom
EXTRALIBS=

# subdirectory for objects
O=linux

# Shaders (DOOM-0008): GLSL compiled to SPIR-V with glslc, then embedded as C
# byte arrays with xxd so the binary is self-contained (no runtime shader
# files). r_vulkan.cpp #includes the generated *.spv.h headers.
SHADER_DIR=shaders
SHADER_SRCS=$(SHADER_DIR)/mesh.vert $(SHADER_DIR)/mesh.frag \
		$(SHADER_DIR)/overlay.vert $(SHADER_DIR)/overlay.frag \
		$(SHADER_DIR)/pathtrace.comp $(SHADER_DIR)/bake.comp \
		$(SHADER_DIR)/svgf_temporal.comp $(SHADER_DIR)/svgf_atrous.comp \
		$(SHADER_DIR)/svgf_composite.comp $(SHADER_DIR)/label.comp \
		$(SHADER_DIR)/taau.comp
SHADER_HDRS=$(SHADER_SRCS:=.spv.h)

# not too sophisticated dependency
OBJS=				\
		$(O)/doomdef.o		\
		$(O)/doomstat.o		\
		$(O)/dstrings.o		\
		$(O)/i_system.o		\
		$(O)/i_sound.o		\
		$(O)/mus2mid.o		\
		$(O)/i_video.o		\
		$(O)/i_net.o			\
		$(O)/tables.o			\
		$(O)/f_finale.o		\
		$(O)/f_wipe.o 		\
		$(O)/d_main.o			\
		$(O)/d_net.o			\
		$(O)/d_items.o		\
		$(O)/g_game.o			\
		$(O)/m_menu.o			\
		$(O)/m_misc.o			\
		$(O)/m_argv.o  		\
		$(O)/m_bbox.o			\
		$(O)/m_fixed.o		\
		$(O)/m_swap.o			\
		$(O)/m_cheat.o		\
		$(O)/m_random.o		\
		$(O)/am_map.o			\
		$(O)/p_ceilng.o		\
		$(O)/p_doors.o		\
		$(O)/p_enemy.o		\
		$(O)/p_floor.o		\
		$(O)/p_inter.o		\
		$(O)/p_lights.o		\
		$(O)/p_map.o			\
		$(O)/p_maputl.o		\
		$(O)/p_plats.o		\
		$(O)/p_pspr.o			\
		$(O)/p_setup.o		\
		$(O)/p_sight.o		\
		$(O)/p_spec.o			\
		$(O)/p_switch.o		\
		$(O)/p_mobj.o			\
		$(O)/p_telept.o		\
		$(O)/p_tick.o			\
		$(O)/p_saveg.o		\
		$(O)/p_user.o			\
		$(O)/r_backend.o		\
		$(O)/r_mesh.o			\
		$(O)/r_vulkan.o		\
		$(O)/r_bsp.o			\
		$(O)/r_data.o			\
		$(O)/r_draw.o			\
		$(O)/r_main.o			\
		$(O)/r_plane.o		\
		$(O)/r_segs.o			\
		$(O)/r_sky.o			\
		$(O)/r_things.o		\
		$(O)/w_wad.o			\
		$(O)/wi_stuff.o		\
		$(O)/v_video.o		\
		$(O)/st_lib.o			\
		$(O)/st_stuff.o		\
		$(O)/hu_stuff.o		\
		$(O)/hu_lib.o			\
		$(O)/s_sound.o		\
		$(O)/z_zone.o			\
		$(O)/info.o				\
		$(O)/sounds.o

.PHONY:	all clean windows test nee-test

all:	 $(O)/$(BIN)

clean:
	rm -f *.o *~ *.flc
	rm -f linux/* mingw/*
	rm -f $(SHADER_DIR)/*.spv $(SHADER_DIR)/*.spv.h

# ---- Unit tests ----
# Every tests/*_test.cpp is a self-contained unit test (includes the header under
# test; no WAD, no GPU, no Vulkan link). Built to its own binary under $(O), so the
# set scales without the build getting longer:
#   * incremental — a test binary is a real file target, recompiled only when its
#     source or (via the auto-generated .d header deps) an included header changes.
#     Unchanged tests are skipped, so `make test` stays cheap as tests are added.
#   * parallel — `make -j test` compiles every test concurrently (then runs them).
#   * fast at runtime — -O2 (these CPU-only tests do millions of iterations; the
#     optimisation is free speed since test code never ships).
# Adding a test is one file: drop tests/<name>_test.cpp in — no Makefile edit.
TEST_SRCS=$(wildcard tests/*_test.cpp)
TEST_BINS=$(TEST_SRCS:tests/%.cpp=$(O)/%)
TEST_CXXFLAGS=$(CXXFLAGS) -O2 -MMD -MP

$(O)/%_test:	tests/%_test.cpp | $(O)
	$(CXX) $(TEST_CXXFLAGS) $< -o $@

# Build all tests (parallel-friendly: the binaries are prerequisites, so -j builds
# them concurrently) then run each. A non-zero exit from any test fails the target.
test:	$(TEST_BINS)
	@for t in $(TEST_BINS); do echo "== $$t =="; $$t || exit 1; done

# Back-compat alias for the original single test (now incremental — only rebuilds
# tests/nee_sampling_test.cpp when it or nee_sampling.h changes).
nee-test:	$(O)/nee_sampling_test
	$(O)/nee_sampling_test

# Auto-generated header dependencies for the tests (-MMD), so editing a shared
# header under test rebuilds exactly the affected tests — no manual `make clean`.
-include $(TEST_BINS:=.d)

# Auto-generated header dependencies for the engine objects (-MMD above), so editing
# a shared header rebuilds exactly the affected objects — the same incremental, no-
# `make clean` workflow the tests already have. Leading '-' ignores the .d files that
# don't exist yet on a fresh build.
-include $(OBJS:.o=.d) $(O)/i_main.d

# Link with the C++ driver (g++) so libstdc++ is pulled in for the Vulkan
# back-end. Linking the C objects this way is harmless.
$(O)/$(BIN):	$(OBJS) $(O)/i_main.o
	$(CXX) $(LDFLAGS) $(OBJS) $(O)/i_main.o \
	-o $(O)/$(BIN) $(LIBS) $(EXTRALIBS)

# ---- Windows cross-build (DOOM-0006) ----
# mingw-w64 cross-compiler (zypper: mingw64-cross-gcc{,-c++}) plus upstream
# SDL2 / SDL2_mixer / Vulkan dev libs staged under ../mingw-deps/prefix (see
# mingw-deps/README.md). Re-enters make with the cross toolchain and Windows
# flags so the one OBJS list and pattern rules are reused unchanged. Winsock
# (-lws2_32) replaces BSD sockets; vulkan-1.dll ships with the user's GPU
# driver, so it is linked (-lvulkan-1) but not bundled. Static libgcc/libstdc++
# keep the GCC runtime out of the distributable.
WIN_PREFIX=../mingw-deps/prefix
windows:	$(SHADER_HDRS)
	mkdir -p mingw
	$(MAKE) O=mingw BIN=doom_ants.exe \
	  CC=x86_64-w64-mingw32-gcc \
	  CXX=x86_64-w64-mingw32-g++ \
	  SDL_CFLAGS="-I$(WIN_PREFIX)/include -I$(WIN_PREFIX)/include/SDL2" \
	  SDL_LIBS="-L$(WIN_PREFIX)/lib -lSDL2_mixer -lSDL2" \
	  VULKAN_LIBS="-L$(WIN_PREFIX)/lib -lvulkan-1" \
	  LDFLAGS="-static-libgcc -static-libstdc++" \
	  EXTRALIBS="-lws2_32" \
	  mingw/doom_ants.exe

# Create the object output dir on demand. Order-only prerequisite (| $(O)) so its
# mtime never forces a rebuild; needed on a fresh clone where linux/ doesn't exist
# yet and -MMD would fail writing linux/*.d.
$(O):
	mkdir -p $(O)

$(O)/%.o:	%.c | $(O)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)/%.o:	%.cpp | $(O)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# GLSL -> SPIR-V -> embedded C header. Stage is inferred from the .vert/.frag/
# .comp extension by glslc; xxd -n pins a stable array name (mesh_vert_spv, ...).
# --target-env=vulkan1.2 is required by pathtrace.comp (VK_KHR_ray_query +
# buffer_reference need SPIR-V 1.5); harmless for the 1.0-era raster shaders.
$(SHADER_DIR)/%.spv.h:	$(SHADER_DIR)/%
	glslc --target-env=vulkan1.2 -O $< -o $(SHADER_DIR)/$*.spv
	xxd -i -n $(subst .,_,$*)_spv $(SHADER_DIR)/$*.spv > $@

# The path-tracer compute shaders #include the shared shading header
# (pt_common.glsl); glslc emits no auto-dep here, so name it explicitly or an edit
# to the shared math would silently skip the recompile (the same hazard the C
# objects have — see the "make clean after a shared header" project note). Listed
# after the default goal (all:) so it stays a dependency, not make's first target.
$(SHADER_DIR)/pathtrace.comp.spv.h $(SHADER_DIR)/bake.comp.spv.h: \
		$(SHADER_DIR)/pt_common.glsl
# pathtrace.comp also #includes the tone pipeline (Workbench-exported scalar curves
# + the hand-written PBR-Neutral operator), step 4b-iii. The SVGF composite reuses
# the same tone operator (step 6a).
$(SHADER_DIR)/pathtrace.comp.spv.h $(SHADER_DIR)/svgf_composite.comp.spv.h: \
		$(SHADER_DIR)/formulas/formulas.glsl \
		$(SHADER_DIR)/formulas/pbr_neutral_tonemap.glsl

# The Vulkan back-end embeds the compiled shaders.
$(O)/r_vulkan.o:	$(SHADER_HDRS)

#############################################################
#
#############################################################