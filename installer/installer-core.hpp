// installer-core.hpp -- the file-level half of the minarch-gl installer.
//
// Kept free of SDL and of every NextUI header so it can be compiled and tested
// on the build host (tests/installer-core-test.cpp).  The UI in minarch-gl-installer.cpp
// only decides *when* to call these functions; every decision about the device's
// files lives here.
//
// Layout (all under a Paths root, so tests can point it at a temp directory):
//
//   <sdcard>/.system/<platform>/bin/minarch.elf                the frontend that runs
//   <sdcard>/.userdata/<platform>/minarch-gl/minarch-gl.elf    our build, kept forever
//   <sdcard>/.userdata/<platform>/minarch-gl/minarch-stock.elf stock build, for Disable
//   <sdcard>/.userdata/<platform>/minarch-gl/state             "gl" | "stock"
//   <sdcard>/.userdata/<platform>/.hooks/boot.d/...            guard hook while enabled
//   <sdcard>/Emus/<platform>/<TAG>.pak[.disabled]              core paks we own
//
// The two backups live under .userdata on purpose: the NextUI updater deletes
// .system/<platform>/bin wholesale before extracting a release, so a backup
// kept next to minarch.elf would not survive an update.
#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <vector>

#include <openssl/evp.h>

namespace mgl {

struct Paths {
	std::string sdcard;    // e.g. /mnt/sdcard
	std::string platform;  // e.g. h700
	std::string pakDir;    // directory containing the running minarch-gl-installer.elf

	std::string binDir() const { return sdcard + "/.system/" + platform + "/bin"; }
	std::string userDataDir() const { return sdcard + "/.userdata/" + platform; }
	std::string emusDir() const { return sdcard + "/Emus/" + platform; }
	std::string payloadDir() const { return pakDir + "/payload"; }
	std::string minarchPath() const { return binDir() + "/minarch.elf"; }
	// Backups under .userdata: a NextUI update deletes .system/<plat>/bin
	// wholesale, and .userdata is the one place it never touches.
	std::string backupDir() const { return userDataDir() + "/minarch-gl"; }
	std::string stockPath() const { return backupDir() + "/minarch-stock.elf"; }
	std::string glPath() const { return backupDir() + "/minarch-gl.elf"; }
	std::string stateDir() const { return backupDir(); }
	std::string statePath() const { return stateDir() + "/state"; }
	std::string hookPath() const { return userDataDir() + "/.hooks/boot.d/minarch-gl-guard.sync.sh"; }
};

static const char *kPakMarker = "minarch-gl-only";

enum class State { Stock, Enabled, NeedsReinstall, Unknown };

///////////////////////////////////////////////////////////////////////////////
// file helpers

inline bool exists(const std::string &p) { return access(p.c_str(), F_OK) == 0; }

inline std::string readHead(const std::string &p, size_t n) {
	std::string out;
	FILE *f = fopen(p.c_str(), "rb");
	if (!f) return out;
	out.resize(n);
	size_t got = fread(&out[0], 1, n, f);
	fclose(f);
	out.resize(got);
	return out;
}

inline std::string readTrim(const std::string &p) {
	std::string s = readHead(p, 128);
	while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
	return s;
}

inline bool writeFile(const std::string &p, const std::string &data, mode_t mode) {
	FILE *f = fopen(p.c_str(), "wb");
	if (!f) return false;
	bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	if (fclose(f) != 0) ok = false;
	if (ok && chmod(p.c_str(), mode) != 0) {
		// FAT/exFAT have no permission bits: chmod is a no-op that may report an
		// error, and the mount options decide the mode.  Accept that only when
		// the file already has the mode we need, so a real failure on a
		// permission-carrying filesystem still aborts the install.
		struct stat st;
		if (stat(p.c_str(), &st) != 0 || (st.st_mode & 0777) != (mode & 0777)) ok = false;
	}
	return ok;
}

inline bool copyFile(const std::string &from, const std::string &to) {
	FILE *in = fopen(from.c_str(), "rb");
	if (!in) return false;
	FILE *out = fopen(to.c_str(), "wb");
	if (!out) { fclose(in); return false; }
	char buf[64 * 1024];
	size_t n;
	bool ok = true;
	while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
	}
	if (ferror(in)) ok = false;
	if (fclose(out) != 0) ok = false;
	fclose(in);
	return ok;
}

inline bool removeTree(const std::string &p) {
	struct stat st;
	if (lstat(p.c_str(), &st) != 0) return true;
	if (!S_ISDIR(st.st_mode)) return unlink(p.c_str()) == 0;
	DIR *d = opendir(p.c_str());
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) != NULL) {
			std::string n = e->d_name;
			if (n == "." || n == "..") continue;
			removeTree(p + "/" + n);
		}
		closedir(d);
	}
	return rmdir(p.c_str()) == 0;
}

inline bool copyTree(const std::string &src, const std::string &dst) {
	struct stat st;
	if (lstat(src.c_str(), &st) != 0) return false;
	if (!S_ISDIR(st.st_mode)) {
		if (!copyFile(src, dst)) return false;
		chmod(dst.c_str(), st.st_mode & 0777);
		return true;
	}
	mkdir(dst.c_str(), 0755);
	DIR *d = opendir(src.c_str());
	if (!d) return false;
	struct dirent *e;
	bool ok = true;
	while ((e = readdir(d)) != NULL) {
		std::string n = e->d_name;
		if (n == "." || n == "..") continue;
		if (!copyTree(src + "/" + n, dst + "/" + n)) ok = false;
	}
	closedir(d);
	return ok;
}

inline bool endsWith(const std::string &s, const std::string &suf) {
	return s.size() > suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// mkdir -p: the state/hook paths live below directories that may not exist yet
// (fresh card, or after the user wiped .userdata).
inline void mkdirs(const std::string &p) {
	for (size_t i = 1; i <= p.size(); i++) {
		if (i == p.size() || p[i] == '/') {
			std::string sub = p.substr(0, i);
			if (!sub.empty()) mkdir(sub.c_str(), 0755);
		}
	}
}

inline bool isELF(const std::string &p) {
	return readHead(p, 4) == std::string("\x7f" "ELF", 4);
}

// MD5 of a file's contents, first 8 hex digits; "" when unreadable.  It is both
// the identity of a build and how the tool tells which frontend is running:
// minarch.elf is a byte-identical copy of one of the two backups under
// .userdata.  (NextUI's version.txt names the *frontend* release, not this
// binary, so it cannot identify it.)
inline std::string fileHash(const std::string &path) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return std::string();
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (!ctx) { fclose(f); return std::string(); }
	unsigned char buf[16 * 1024];
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int mdlen = 0;
	bool ok = EVP_DigestInit_ex(ctx, EVP_md5(), NULL) == 1;
	size_t n;
	while (ok && (n = fread(buf, 1, sizeof buf, f)) > 0)
		ok = EVP_DigestUpdate(ctx, buf, n) == 1;
	ok = ok && EVP_DigestFinal_ex(ctx, md, &mdlen) == 1;
	EVP_MD_CTX_free(ctx);
	fclose(f);
	if (!ok || mdlen < 4) return std::string();
	char out[9];
	snprintf(out, sizeof out, "%02x%02x%02x%02x", md[0], md[1], md[2], md[3]);
	return std::string(out);
}

// A path as the user sees it: relative to the card root.
inline std::string cardPath(const Paths &p, const std::string &path) {
	const std::string prefix = p.sdcard + "/";
	return path.compare(0, prefix.size(), prefix) == 0 ? path.substr(prefix.size()) : path;
}

///////////////////////////////////////////////////////////////////////////////
// generated scripts

inline std::string guardScript(const Paths &p) {
	return std::string("#!/bin/sh\n") +
		"# minarch-gl guard -- installed by the installer.\n"
		"# A NextUI update deletes .system/<platform>/bin wholesale.  If that\n"
		"# happened while minarch-gl was the active frontend, hide the GL-only\n"
		"# core paks again so the stock frontend never tries to load a core it\n"
		"# cannot drive, and leave a marker for the Status screen.\n"
		"[ \"$(cat \"" + p.statePath() + "\" 2>/dev/null)\" = \"gl\" ] || exit 0\n"
		"cmp -s \"" + p.minarchPath() + "\" \"" + p.glPath() + "\" 2>/dev/null && exit 0\n"
		"for q in \"" + p.emusDir() + "\"/*.pak; do\n"
		"  [ -f \"$q/" + kPakMarker + "\" ] && mv \"$q\" \"$q.disabled\"\n"
		"done\n"
		"echo needs-reinstall > \"" + p.stateDir() + "/needs-reinstall\"\n";
}

///////////////////////////////////////////////////////////////////////////////
// state

inline State currentState(const Paths &p) {
	const std::string active = fileHash(p.minarchPath());
	if (active.empty()) return State::Unknown;
	const std::string gl = fileHash(p.glPath());
	if (!gl.empty() && active == gl) return State::Enabled;
	if (isELF(p.minarchPath())) return State::Stock;
	return State::Unknown;
}

inline const char *stateName(State s) {
	switch (s) {
	case State::Stock: return "NextUI stock";
	case State::Enabled: return "minarch-gl (GL)";
	case State::NeedsReinstall: return "Needs reinstall";
	default: return "Unknown";
	}
}

inline bool hasMarker(const std::string &pak) { return exists(pak + "/" + kPakMarker); }

inline void forEachDir(const std::string &dir, const std::function<void(const std::string &, const std::string &)> &fn) {
	DIR *d = opendir(dir.c_str());
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		std::string name = e->d_name;
		if (name == "." || name == "..") continue;
		fn(dir + "/" + name, name);
	}
	closedir(d);
}

inline int countInstalledCores(const Paths &p) {
	int n = 0;
	forEachDir(p.emusDir(), [&](const std::string &path, const std::string &name) {
		if (endsWith(name, ".pak") && hasMarker(path)) n++;
	});
	return n;
}

inline int countHiddenCores(const Paths &p) {
	int n = 0;
	forEachDir(p.emusDir(), [&](const std::string &path, const std::string &name) {
		if (endsWith(name, ".pak.disabled") && hasMarker(path)) n++;
	});
	return n;
}

inline int countPayloadCores(const Paths &p) {
	int n = 0;
	forEachDir(p.payloadDir(), [&](const std::string &, const std::string &name) {
		if (endsWith(name, ".pak")) n++;
	});
	return n;
}

// Turn <TAG>.pak.disabled back into <TAG>.pak for every marked pak.
inline int enableMarkedPaks(const Paths &p) {
	int n = 0;
	forEachDir(p.emusDir(), [&](const std::string &path, const std::string &name) {
		if (!endsWith(name, ".pak.disabled") || !hasMarker(path)) return;
		std::string target = path.substr(0, path.size() - strlen(".disabled"));
		if (!exists(target) && rename(path.c_str(), target.c_str()) == 0) n++;
	});
	return n;
}

// Hide every marked <TAG>.pak from the stock frontend.
inline int disableMarkedPaks(const Paths &p) {
	int n = 0;
	forEachDir(p.emusDir(), [&](const std::string &path, const std::string &name) {
		if (!endsWith(name, ".pak") || !hasMarker(path)) return;
		std::string target = path + ".disabled";
		// A stale disabled copy (failed earlier restore, or a manual copy) would
		// make the rename fail and leave the pak visible to the stock frontend.
		if (exists(target)) removeTree(target);
		if (rename(path.c_str(), target.c_str()) == 0) n++;
	});
	return n;
}

// Copy payload/<TAG>.pak into Emus/.  Only replaces a pak that is ours (marker
// present); a foreign pak with the same tag is left alone.
inline bool installPayloadPaks(const Paths &p, std::string &err, int &installed, int &foreign) {
	if (!exists(p.payloadDir())) { err = "pak payload missing from the tool pak"; return false; }
	bool ok = true;
	forEachDir(p.payloadDir(), [&](const std::string &src, const std::string &name) {
		if (!ok || !endsWith(name, ".pak")) return;
		const std::string dst = p.emusDir() + "/" + name;
		if (exists(dst) && !hasMarker(dst)) { foreign++; return; }
		removeTree(dst);
		removeTree(dst + ".disabled");
		if (!copyTree(src, dst)) { err = "cannot install " + name + " (SD card full?)"; ok = false; return; }
		installed++;
	});
	return ok;
}

///////////////////////////////////////////////////////////////////////////////
// install / restore

struct InstallResult {
	bool ok = false;
	std::string err;
	int installed = 0;
	int foreign = 0;
	int reenabled = 0;
};

inline InstallResult installGl(const Paths &p) {
	InstallResult r;
	const std::string payload = p.payloadDir() + "/minarch-gl.elf";
	const std::string backup = p.glPath();
	if (!exists(payload) && !exists(backup)) {
		r.err = "minarch-gl.elf is missing from the pak and from the backup";
		return r;
	}
	// The pak is the install source.  If its payload cannot be read (pak
	// replaced mid-copy, card error) the copy kept under .userdata still works,
	// so an update of the tool pak is not needed to re-enable.
	const std::string source = exists(payload) ? payload : backup;
	mkdirs(p.backupDir());

	const std::string active = p.minarchPath();
	const std::string activeHash = fileHash(active);
	const std::string backupHash = fileHash(backup);
	const bool activeIsGl = !activeHash.empty() && !backupHash.empty() && activeHash == backupHash;

	// Save whatever runs now unless it already is our build: that copy is what
	// Disable restores, and a NextUI update cannot delete it.
	if (!activeIsGl && exists(active)) {
		const std::string s_tmp = p.stockPath() + ".new";
		if (!copyFile(active, s_tmp)) { r.err = "cannot back up the running minarch.elf (SD card full?)"; return r; }
		if (rename(s_tmp.c_str(), p.stockPath().c_str()) != 0) { r.err = "cannot save minarch-stock.elf"; unlink(s_tmp.c_str()); return r; }
	}

	const std::string gl_tmp = backup + ".new";
	if (!copyFile(source, gl_tmp)) { r.err = "cannot copy minarch-gl.elf (SD card full?)"; return r; }
	if (rename(gl_tmp.c_str(), backup.c_str()) != 0) { r.err = "cannot install minarch-gl.elf"; unlink(gl_tmp.c_str()); return r; }

	// minarch.elf is a byte-identical copy of the backup, not a wrapper script:
	// that is what makes the hash comparison in currentState() meaningful.
	const std::string m_tmp = active + ".new";
	if (!copyFile(backup, m_tmp)) { r.err = "cannot copy minarch.elf (SD card full?)"; return r; }
	chmod(m_tmp.c_str(), 0755);
	if (rename(m_tmp.c_str(), active.c_str()) != 0) { r.err = "cannot replace minarch.elf"; unlink(m_tmp.c_str()); return r; }

	mkdirs(p.stateDir());
	writeFile(p.statePath(), "gl\n", 0644);
	mkdirs(p.userDataDir() + "/.hooks/boot.d");
	writeFile(p.hookPath(), guardScript(p), 0755);

	// The frontend is in place now, so the core paks are safe to expose.
	if (!installPayloadPaks(p, r.err, r.installed, r.foreign)) {
		r.err = "frontend is installed, but " + r.err;
		return r;
	}
	r.reenabled = enableMarkedPaks(p);
	sync();
	r.ok = true;
	return r;
}

struct RestoreResult {
	bool ok = false;
	std::string err;
	int disabled = 0;
};

inline RestoreResult restoreStock(const Paths &p) {
	RestoreResult r;
	const std::string active = p.minarchPath();
	const std::string activeHash = fileHash(active);
	const std::string backupHash = fileHash(p.glPath());
	const bool activeIsGl = !activeHash.empty() && !backupHash.empty() && activeHash == backupHash;

	if (activeIsGl) {
		// Restore only when our build is what is running.  After a NextUI update
		// the active binary is already a stock one (a newer release); copying an
		// older backup over it would silently downgrade the frontend.
		if (!exists(p.stockPath())) {
			r.err = "minarch-stock.elf is missing; cannot restore it automatically";
			return r;
		}
		// Copy to a temp name and rename over it: never leave the device without
		// a minarch.elf, even if the copy fails half-way.
		const std::string m_tmp = active + ".new";
		if (!copyFile(p.stockPath(), m_tmp)) { r.err = "cannot restore minarch.elf (SD card full?)"; return r; }
		chmod(m_tmp.c_str(), 0755);
		if (rename(m_tmp.c_str(), active.c_str()) != 0) { r.err = "cannot restore minarch.elf"; unlink(m_tmp.c_str()); return r; }
	}
	// minarch-gl.elf and the stock backup both stay: Enable can reinstall from
	// the .userdata copy even when the pak is gone, and a later NextUI update
	// does not delete them.
	mkdirs(p.stateDir());
	writeFile(p.statePath(), "stock\n", 0644);
	unlink(p.hookPath().c_str());
	r.disabled = disableMarkedPaks(p);
	sync();
	r.ok = true;
	return r;
}


///////////////////////////////////////////////////////////////////////////////
// cores, ROM folders and what the Status screen shows

// The paks this tool installs, and the ROM folders NextUI would show for them.
//
// A folder belongs to a pak by the tag in its trailing parentheses -- that is
// exactly how NextUI reads it (getEmuName() in common/utils.c: last '(' up to
// the following ')').  Users may therefore name the folder anything, as long as
// the tag is right, and this file only ever compares the tag.
struct CoreSpec {
	const char *tag;                // LIBRETRO_FLYCAST
	const char *core;               // flycast: the .so name / the version line
	const char *const *systems;     // default folder names, NULL-terminated
};

inline const char *const kFlycastSystems[] = {"Sega Dreamcast", "Sega Naomi", NULL};
inline const char *const kMupenSystems[] = {"Nintendo 64", NULL};
inline const char *const kYabaSystems[] = {"Sega Saturn", "Sega Titan Video", NULL};

inline const CoreSpec kCores[] = {
	{"LIBRETRO_FLYCAST", "flycast", kFlycastSystems},
	{"LIBRETRO_MUPEN64PLUS", "mupen64plus_next", kMupenSystems},
	{"LIBRETRO_YABASANSHIRO", "yabasanshiro", kYabaSystems},
};
constexpr int kCoreCount = (int)(sizeof(kCores) / sizeof(kCores[0]));

inline std::string romsDir(const Paths &p) { return p.sdcard + "/Roms"; }

// "Sega Dreamcast (LIBRETRO_FLYCAST)" and what NextUI would name it.
inline std::string romFolderName(const char *system, const char *tag) {
	return std::string(system) + " (" + tag + ")";
}

inline bool isDir(const std::string &p) {
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// The tag NextUI would read from a folder name, or "" when there is none.
inline std::string folderTag(const std::string &name) {
	const size_t open = name.rfind('(');
	if (open == std::string::npos) return "";
	const size_t close = name.find(')', open + 1);
	if (close == std::string::npos) return "";
	return name.substr(open + 1, close - open - 1);
}

// Every directory under Roms/ whose tag matches, so a user-made folder counts.
inline std::vector<std::string> romFoldersForTag(const Paths &p, const char *tag) {
	std::vector<std::string> out;
	forEachDir(romsDir(p), [&](const std::string &path, const std::string &name) {
		if (isDir(path) && folderTag(name) == tag) out.push_back(name);
	});
	return out;
}

// The default folders that do not exist yet (existing ones are skipped).
inline std::vector<std::string> missingDefaultRomFolders(const Paths &p) {
	std::vector<std::string> out;
	for (int i = 0; i < kCoreCount; i++) {
		for (const char *const *sys = kCores[i].systems; *sys; sys++) {
			const std::string name = romFolderName(*sys, kCores[i].tag);
			if (!isDir(romsDir(p) + "/" + name)) out.push_back(name);
		}
	}
	return out;
}

// mkdir Roms/<folder> for every entry of `folders`; returns how many were made.
inline int createRomFolders(const Paths &p, const std::vector<std::string> &folders) {
	mkdirs(romsDir(p));
	int made = 0;
	for (const std::string &f : folders) {
		const std::string path = romsDir(p) + "/" + f;
		if (isDir(path)) continue;
		if (mkdir(path.c_str(), 0755) == 0) made++;
	}
	return made;
}

// Build-time version of a core pak ("flycast aa97a6d6"), written by
// tools/build.sh; prefer the installed copy, fall back to the payload.
inline std::string coreVersion(const Paths &p, const char *tag) {
	std::string v = readTrim(p.emusDir() + "/" + tag + ".pak/version");
	if (v.empty()) v = readTrim(p.payloadDir() + "/" + tag + ".pak/version");
	return v;
}

inline std::string humanState(const Paths &p) {
	switch (currentState(p)) {
	case State::Stock: return "disabled";
	case State::Enabled: return "enabled";
	case State::NeedsReinstall: return "needs reinstall";
	default: return "unknown";
	}
}

// The short system name shown next to a ROM folder row ("Dreamcast", "N64", …).
// The hash part of a core's recorded version ("flycast aa97a6d6" -> "aa97a6d6").
inline std::string coreHash(const Paths &p, const char *tag, const char *core) {
	std::string v = coreVersion(p, tag);
	const std::string prefix = std::string(core) + " ";
	if (v.compare(0, prefix.size(), prefix) == 0) v.erase(0, prefix.size());
	return v;
}

// One core row of the Status screen plus the directories its tag owns.
struct StatusCore {
	std::string name;               // flycast
	std::string hash;               // aa97a6d6, or "missing"
	std::vector<std::string> dirs;  // Roms/<folder>, card-relative
};

// What the Status screen shows.  "Activated" is decided by comparing the
// running minarch.elf with our backup: a NextUI update replaces the binary, so
// the hashes differ and the page reports stock even though our backup is still
// there under .userdata.
struct StatusInfo {
	std::string activated;      // "minarch-gl" | "stock" | "missing"
	std::string activatedHash;  // 8 hex digits, or "-"
	std::string backup;         // card-relative path, or "missing"
	std::string backupHash;     // 8 hex digits, or "-"
	std::vector<StatusCore> cores;
};

inline StatusInfo statusInfo(const Paths &p) {
	StatusInfo s;
	const std::string active = p.minarchPath();
	const std::string activeHash = fileHash(active);
	const std::string glHash = fileHash(p.glPath());

	if (activeHash.empty()) {
		s.activated = "missing";
		s.activatedHash = "-";
	} else if (!glHash.empty() && activeHash == glHash) {
		s.activated = "minarch-gl";
		s.activatedHash = activeHash;
	} else if (isELF(active)) {
		s.activated = "stock";
		s.activatedHash = activeHash;
	} else {
		s.activated = "missing";
		s.activatedHash = "-";
	}

	// The other frontend is the backup: our build while stock runs, the saved
	// stock build while ours runs.
	const std::string backup = (s.activated == "minarch-gl") ? p.stockPath() : p.glPath();
	const std::string backupHash = fileHash(backup);
	if (backupHash.empty()) {
		s.backup = "missing";
		s.backupHash = "-";
	} else {
		s.backup = cardPath(p, backup);
		s.backupHash = backupHash;
	}

	for (int i = 0; i < kCoreCount; i++) {
		const CoreSpec &c = kCores[i];
		StatusCore sc;
		sc.name = c.core;
		const std::string hash = coreHash(p, c.tag, c.core);
		sc.hash = hash.empty() ? "missing" : hash;
		// The directories are whatever carries this tag in its name, exactly as
		// NextUI groups them; there is no per-system list to keep in sync.
		for (const std::string &name : romFoldersForTag(p, c.tag))
			sc.dirs.push_back("Roms/" + name);
		std::sort(sc.dirs.begin(), sc.dirs.end());
		s.cores.push_back(sc);
	}
	return s;
}

// Plain text version for --status (scripts).
inline std::string statusLines(const Paths &p) {
	const StatusInfo s = statusInfo(p);
	std::string out;
	out += "Activated  " + s.activated + "\n";
	out += "Activated Version  " + s.activatedHash + "\n";
	out += "Backup  " + s.backup + "\n";
	out += "Backup Version  " + s.backupHash + "\n";
	for (const StatusCore &c : s.cores) {
		out += "\n" + c.name + "  " + c.hash + "\n";
		for (const std::string &d : c.dirs) out += "  " + d + "\n";
	}
	return out;
}

} // namespace mgl
