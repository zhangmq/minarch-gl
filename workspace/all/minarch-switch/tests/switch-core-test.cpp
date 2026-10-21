// Host-side test for switch-core.hpp.
//
// The switcher cannot be run on the build host (aarch64 binary, SDL/GLES UI), so
// the part that actually mutates the SD card is isolated in a header with no
// SDL dependency and exercised here against a temporary directory tree.
//
//   make test
//
// Covered: install, idempotent reinstall, restore, re-enable, foreign pak with a
// colliding tag, and the "NextUI updated .system while GL was enabled" state.

#include "switch-core.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace mswitch;

static int failures = 0;

#define CHECK(cond)                                                          \
	do {                                                                     \
		if (!(cond)) {                                                       \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
			failures++;                                                      \
		}                                                                    \
	} while (0)

#define CHECK_EQ_STR(a, b)                                                    \
	do {                                                                      \
		std::string _a = (a), _b = (b);                                       \
		if (_a != _b) {                                                       \
			fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, \
			        _a.c_str(), _b.c_str());                                  \
			failures++;                                                       \
		}                                                                     \
	} while (0)

static const char *STOCK_ELF = "\x7f" "ELF stock-binary\n";
static const char *GL_ELF = "\x7f" "ELF gl-binary\n";

static std::string readAll(const std::string &p) {
	FILE *f = fopen(p.c_str(), "rb");
	if (!f) return "";
	std::string out;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
	fclose(f);
	return out;
}

static void writeStr(const std::string &p, const std::string &s) {
	FILE *f = fopen(p.c_str(), "wb");
	if (!f) { fprintf(stderr, "cannot write %s\n", p.c_str()); exit(2); }
	fwrite(s.data(), 1, s.size(), f);
	fclose(f);
}

static std::string mktempRoot() {
	char tmpl[] = "/tmp/minarch-switch-test.XXXXXX";
	char *d = mkdtemp(tmpl);
	if (!d) { perror("mkdtemp"); exit(2); }
	return d;
}

static void rmrf(const std::string &p) { removeTree(p); }

// The switcher writes two shell scripts at install time; POSIX sh has to accept
// them before they ever run on a device we cannot reach.
static bool shSyntaxOk(const std::string &p) {
	std::string cmd = "sh -n '" + p + "' 2>/dev/null";
	return system(cmd.c_str()) == 0;
}

int main() {
	const std::string root = mktempRoot();
	Paths paths;
	paths.sdcard = root;
	paths.platform = "h700";
	paths.pakDir = root + "/Tools/h700/Minarch GL.pak";

	mkdirs(paths.binDir());
	mkdirs(paths.emusDir());
	mkdirs(paths.payloadDir() + "/LIBRETRO_FLYCAST.pak");
	mkdirs(paths.payloadDir() + "/LIBRETRO_MUPEN64PLUS.pak");

	writeStr(paths.minarchPath(), STOCK_ELF);
	writeStr(paths.payloadDir() + "/minarch-gl.elf", GL_ELF);
	writeStr(paths.payloadDir() + "/LIBRETRO_FLYCAST.pak/launch.sh", "#!/bin/sh\nexit 0\n");
	writeStr(paths.payloadDir() + "/LIBRETRO_FLYCAST.pak/" + kPakMarker, "");
	writeStr(paths.payloadDir() + "/LIBRETRO_FLYCAST.pak/flycast_libretro.so", "core");
	writeStr(paths.payloadDir() + "/LIBRETRO_MUPEN64PLUS.pak/launch.sh", "#!/bin/sh\nexit 0\n");
	writeStr(paths.payloadDir() + "/LIBRETRO_MUPEN64PLUS.pak/" + kPakMarker, "");
	writeStr(paths.payloadDir() + "/LIBRETRO_MUPEN64PLUS.pak/mupen64plus_next_libretro.so", "core");

	// A foreign pak with a colliding tag (the store's DC.pak, say) and one with a
	// tag we also ship, to prove we never delete someone else's folder.
	mkdirs(paths.emusDir() + "/DC.pak");
	writeStr(paths.emusDir() + "/DC.pak/launch.sh", "#!/bin/sh\n");
	mkdirs(paths.emusDir() + "/LIBRETRO_MUPEN64PLUS.pak");
	writeStr(paths.emusDir() + "/LIBRETRO_MUPEN64PLUS.pak/foreign.txt", "not ours\n");

	CHECK(currentState(paths) == State::Stock);

	// --- install -----------------------------------------------------------
	InstallResult r = installGl(paths);
	CHECK(r.ok);
	CHECK_EQ_STR(r.err, "");
	CHECK(r.installed == 1);   // FLYCAST installs; MUPEN tag is occupied by a foreign pak
	CHECK(r.foreign == 1);
	CHECK(currentState(paths) == State::Enabled);

	CHECK(isOurDispatch(paths.minarchPath()));
	CHECK(exists(paths.glPath()));
	CHECK(exists(paths.stockPath()));
	CHECK_EQ_STR(readAll(paths.stockPath()), STOCK_ELF);
	CHECK_EQ_STR(readTrim(paths.statePath()), "gl");
	CHECK(exists(paths.hookPath()));
	CHECK(readAll(paths.hookPath()).find(kDispatchMarker) != std::string::npos);
	CHECK(shSyntaxOk(paths.minarchPath()));
	CHECK(shSyntaxOk(paths.hookPath()));
	CHECK(exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak/" + kPakMarker));
	CHECK(exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak/flycast_libretro.so"));
	CHECK(exists(paths.emusDir() + "/DC.pak/launch.sh"));                    // untouched
	CHECK(exists(paths.emusDir() + "/LIBRETRO_MUPEN64PLUS.pak/foreign.txt")); // not replaced
	CHECK(countInstalledCores(paths) == 1);

	// --- reinstall is idempotent and keeps the original backup -------------
	r = installGl(paths);
	CHECK(r.ok);
	CHECK_EQ_STR(readAll(paths.stockPath()), STOCK_ELF);
	CHECK(isOurDispatch(paths.minarchPath()));
	CHECK_EQ_STR(readAll(paths.minarchPath()), dispatchScript(paths));

	// --- restore -----------------------------------------------------------
	RestoreResult rr = restoreStock(paths);
	CHECK(rr.ok);
	CHECK(rr.disabled == 1);
	CHECK(currentState(paths) == State::Stock);
	CHECK_EQ_STR(readAll(paths.minarchPath()), STOCK_ELF);
	CHECK(!exists(paths.glPath()));
	CHECK(!exists(paths.stockPath()));
	CHECK(!exists(paths.hookPath()));
	CHECK_EQ_STR(readTrim(paths.statePath()), "stock");
	CHECK(exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak.disabled/" + kPakMarker));
	CHECK(!exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak"));
	CHECK(countInstalledCores(paths) == 0);
	CHECK(exists(paths.emusDir() + "/DC.pak/launch.sh")); // still untouched

	// --- a stale .disabled copy must not keep the visible pak alive --------
	CHECK(exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak.disabled/" + kPakMarker));
	mkdirs(paths.emusDir() + "/LIBRETRO_FLYCAST.pak");
	writeStr(paths.emusDir() + "/LIBRETRO_FLYCAST.pak/" + kPakMarker, "");
	rr = restoreStock(paths);
	CHECK(rr.ok);
	CHECK(!exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak"));
	CHECK(exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak.disabled/" + kPakMarker));

	// --- enable again re-exposes the hidden paks ---------------------------
	r = installGl(paths);
	CHECK(r.ok);
	CHECK(exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak/" + kPakMarker));
	CHECK(!exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak.disabled"));
	CHECK(currentState(paths) == State::Enabled);

	// --- NextUI update: .system replaced, state still says gl --------------
	rmrf(paths.binDir());
	mkdirs(paths.binDir());
	const char *NEW_STOCK = "\x7f" "ELF stock-v2\n";
	writeStr(paths.minarchPath(), NEW_STOCK);
	CHECK(currentState(paths) == State::NeedsReinstall);

	r = installGl(paths);
	CHECK(r.ok);
	CHECK(currentState(paths) == State::Enabled);
	CHECK_EQ_STR(readAll(paths.stockPath()), NEW_STOCK); // backs up the *new* stock

	// --- restore without a backup must refuse, and must not eat the frontend -
	unlink(paths.stockPath().c_str());
	rr = restoreStock(paths);
	CHECK(!rr.ok);
	CHECK(isOurDispatch(paths.minarchPath())); // still a working dispatch, not deleted
	CHECK(exists(paths.glPath()));

	rmrf(root);
	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("switch-core: all checks passed\n");
	return 0;
}
