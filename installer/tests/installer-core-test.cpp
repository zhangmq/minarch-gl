// Host-side test for installer-core.hpp.
//
// The installer cannot be run on the build host (aarch64 binary, SDL/GLES UI), so
// the part that actually mutates the SD card is isolated in a header with no
// SDL dependency and exercised here against a temporary directory tree.
//
//   make test
//
// Covered: install, idempotent reinstall, restore, re-enable, foreign pak with a
// colliding tag, and the "NextUI updated .system while GL was enabled" state.

#include "installer-core.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace mgl;

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
	char tmpl[] = "/tmp/minarch-gl-installer-test.XXXXXX";
	char *d = mkdtemp(tmpl);
	if (!d) { perror("mkdtemp"); exit(2); }
	return d;
}

static void rmrf(const std::string &p) { removeTree(p); }

// The installer writes shell scripts at install time; POSIX sh has to accept
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

	// minarch.elf is a byte copy of our build, not a wrapper: the hash check
	// depends on it, and a NextUI update replaces it with the stock binary.
	CHECK_EQ_STR(readAll(paths.minarchPath()), GL_ELF);
	CHECK_EQ_STR(readAll(paths.glPath()), GL_ELF);
	CHECK_EQ_STR(readAll(paths.stockPath()), STOCK_ELF);
	CHECK_EQ_STR(readTrim(paths.statePath()), "gl");
	CHECK(exists(paths.hookPath()));
	CHECK(readAll(paths.hookPath()).find("cmp -s") != std::string::npos);
	CHECK(shSyntaxOk(paths.hookPath()));
	CHECK(exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak/" + kPakMarker));
	CHECK(exists(paths.emusDir() + "/LIBRETRO_FLYCAST.pak/flycast_libretro.so"));
	CHECK(exists(paths.emusDir() + "/DC.pak/launch.sh"));                    // untouched
	CHECK(exists(paths.emusDir() + "/LIBRETRO_MUPEN64PLUS.pak/foreign.txt")); // not replaced
	CHECK(countInstalledCores(paths) == 1);

	// --- reinstall is idempotent and keeps the original backup -------------
	r = installGl(paths);
	CHECK(r.ok);
	CHECK_EQ_STR(readAll(paths.stockPath()), STOCK_ELF); // active is already ours
	CHECK_EQ_STR(readAll(paths.minarchPath()), GL_ELF);

	// --- restore -----------------------------------------------------------
	RestoreResult rr = restoreStock(paths);
	CHECK(rr.ok);
	CHECK(rr.disabled == 1);
	CHECK(currentState(paths) == State::Stock);
	CHECK_EQ_STR(readAll(paths.minarchPath()), STOCK_ELF);
	CHECK_EQ_STR(readAll(paths.glPath()), GL_ELF);       // backup is kept
	CHECK_EQ_STR(readAll(paths.stockPath()), STOCK_ELF); // restored, kept for next time
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

	// --- NextUI update: .system replaced, backups survive in .userdata -----
	rmrf(paths.binDir());
	mkdirs(paths.binDir());
	const char *NEW_STOCK = "\x7f" "ELF stock-v2\n";
	writeStr(paths.minarchPath(), NEW_STOCK);
	CHECK(currentState(paths) == State::Stock);         // hashes differ: stock is running
	CHECK(exists(paths.glPath()));                      // ... and our build is still here
	{
		const StatusInfo si = statusInfo(paths);
		CHECK_EQ_STR(si.activated, "stock");
		CHECK_EQ_STR(si.activatedHash, fileHash(paths.minarchPath()));
		CHECK_EQ_STR(si.backup, cardPath(paths, paths.glPath()));
		CHECK_EQ_STR(si.backupHash, fileHash(paths.glPath()));
	}

	r = installGl(paths);
	CHECK(r.ok);
	CHECK(currentState(paths) == State::Enabled);
	CHECK_EQ_STR(readAll(paths.stockPath()), NEW_STOCK); // backs up the *new* stock

	// --- restore without a backup must refuse, and must not eat the frontend -
	unlink(paths.stockPath().c_str());
	rr = restoreStock(paths);
	CHECK(!rr.ok);
	CHECK_EQ_STR(readAll(paths.minarchPath()), GL_ELF);  // still our working build
	CHECK(exists(paths.glPath()));

	// --- ROM folders: NextUI's naming rule and the enable-time offer -------
	// A folder belongs to a pak by the tag in its trailing parentheses, exactly
	// as NextUI reads it (getEmuName(): last '(' up to the following ')').
	CHECK_EQ_STR(folderTag("Sega Dreamcast (LIBRETRO_FLYCAST)"), "LIBRETRO_FLYCAST");
	CHECK_EQ_STR(folderTag("Dreamcast (LIBRETRO_FLYCAST)"), "LIBRETRO_FLYCAST");
	CHECK_EQ_STR(folderTag("Nintendo 64 (LIBRETRO_MUPEN64PLUS) (main)"), "main"); // last parens wins, like NextUI
	CHECK_EQ_STR(folderTag("Dreamcast"), "");
	CHECK_EQ_STR(folderTag("Sega Dreamcast (LIBRETRO_FLYCAST"), "");
	mkdirs(paths.sdcard + "/Roms");
	CHECK(missingDefaultRomFolders(paths).size() == 5);   // DC, Naomi, N64, Saturn, ST-V

	// A hand-made folder with the right tag counts, and only that tag does.
	mkdirs(romsDir(paths) + "/Dreamcast (LIBRETRO_FLYCAST)");
	mkdirs(romsDir(paths) + "/Sega Saturn (SATURN)");     // stock tag: not ours
	const std::vector<std::string> flycastDirs = romFoldersForTag(paths, "LIBRETRO_FLYCAST");
	CHECK(flycastDirs.size() == 1);
	CHECK_EQ_STR(flycastDirs.empty() ? "" : flycastDirs[0], "Dreamcast (LIBRETRO_FLYCAST)");
	CHECK(romFoldersForTag(paths, "LIBRETRO_YABASANSHIRO").empty());

	// Creating skips what already exists (the default DC name is still missing,
	// the hand-made one must not be touched) and returns what it made.
	const std::vector<std::string> missing = missingDefaultRomFolders(paths);
	CHECK(missing.size() == 5);
	CHECK(createRomFolders(paths, missing) == 5);
	CHECK(createRomFolders(paths, missing) == 0);         // idempotent
	CHECK(isDir(romsDir(paths) + "/Sega Dreamcast (LIBRETRO_FLYCAST)"));
	CHECK(isDir(romsDir(paths) + "/Sega Titan Video (LIBRETRO_YABASANSHIRO)"));
	CHECK(missingDefaultRomFolders(paths).empty());
	// the default pair plus the hand-made "Dreamcast (...)" folder from above
	CHECK(romFoldersForTag(paths, "LIBRETRO_FLYCAST").size() == 3);

	// --- Status: activated/backup from hashes, cores, tag-filtered dirs ----
	writeStr(paths.payloadDir() + "/LIBRETRO_FLYCAST.pak/version", "flycast aa97a6d6\n");
	CHECK_EQ_STR(coreVersion(paths, "LIBRETRO_FLYCAST"), "flycast aa97a6d6"); // payload fallback
	writeStr(paths.emusDir() + "/LIBRETRO_FLYCAST.pak/version", "flycast deadbeef\n");
	CHECK_EQ_STR(coreVersion(paths, "LIBRETRO_FLYCAST"), "flycast deadbeef"); // installed wins
	CHECK_EQ_STR(coreVersion(paths, "LIBRETRO_YABASANSHIRO"), "");            // not recorded

	const StatusInfo st = statusInfo(paths);
	CHECK_EQ_STR(st.activated, "minarch-gl");
	CHECK_EQ_STR(st.activatedHash, fileHash(paths.minarchPath()));
	CHECK_EQ_STR(st.backup, "missing");                  // the stock backup was removed above
	CHECK_EQ_STR(st.backupHash, "-");
	CHECK(st.cores.size() == 3);
	CHECK_EQ_STR(st.cores[0].name, "flycast");
	CHECK_EQ_STR(st.cores[0].hash, "deadbeef");
	CHECK_EQ_STR(st.cores[1].name, "mupen64plus_next");
	CHECK_EQ_STR(st.cores[1].hash, "missing");
	CHECK_EQ_STR(st.cores[2].name, "yabasanshiro");
	CHECK(st.cores[2].dirs.size() == 2);
	CHECK_EQ_STR(st.cores[2].dirs[0], "Roms/Sega Saturn (LIBRETRO_YABASANSHIRO)");
	CHECK_EQ_STR(st.cores[2].dirs[1], "Roms/Sega Titan Video (LIBRETRO_YABASANSHIRO)");
	// Only folders carrying the tag are listed: the hand-made DC folder counts,
	// the foreign "Sega Saturn (SATURN)" one does not.
	CHECK(st.cores[0].dirs.size() == 3);
	CHECK_EQ_STR(st.cores[0].dirs[0], "Roms/Dreamcast (LIBRETRO_FLYCAST)");
	CHECK_EQ_STR(st.cores[0].dirs[2], "Roms/Sega Naomi (LIBRETRO_FLYCAST)");
	CHECK(st.cores[1].dirs.size() == 1);
	CHECK_EQ_STR(st.cores[1].dirs[0], "Roms/Nintendo 64 (LIBRETRO_MUPEN64PLUS)");

	const std::string text = statusLines(paths);
	CHECK(text.find("Activated  minarch-gl") != std::string::npos);
	CHECK(text.find("Backup  missing") != std::string::npos);
	CHECK(text.find("\nflycast  deadbeef") != std::string::npos);
	CHECK(text.find("Roms/Sega Saturn (SATURN)") == std::string::npos); // foreign tag never listed
	CHECK(text.find("[") == std::string::npos);           // no decoration

	rmrf(root);
	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("installer-core: all checks passed\n");
	return 0;
}
