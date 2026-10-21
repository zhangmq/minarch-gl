// switch-core.hpp -- the file-level half of the minarch-gl switcher.
//
// Kept free of SDL and of every NextUI header so it can be compiled and tested
// on the build host (tests/switch-core-test.cpp).  The UI in minarch-switch.cpp
// only decides *when* to call these functions; every decision about the device's
// files lives here.
//
// Layout (all under a Paths root, so tests can point it at a temp directory):
//
//   <sdcard>/.system/<platform>/bin/minarch.elf          dispatch script or stock ELF
//   <sdcard>/.system/<platform>/bin/minarch-stock.elf    stock binary, moved aside
//   <sdcard>/.system/<platform>/bin/minarch-gl.elf       copied from payload/
//   <sdcard>/.userdata/<platform>/minarch-gl/state       "gl" | "stock"
//   <sdcard>/.userdata/<platform>/.hooks/boot.d/...      guard hook while enabled
//   <sdcard>/Emus/<platform>/<TAG>.pak[.disabled]        core paks we own
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

namespace mswitch {

struct Paths {
	std::string sdcard;    // e.g. /mnt/sdcard
	std::string platform;  // e.g. h700
	std::string pakDir;    // directory containing the running minarch-switch.elf

	std::string binDir() const { return sdcard + "/.system/" + platform + "/bin"; }
	std::string userDataDir() const { return sdcard + "/.userdata/" + platform; }
	std::string emusDir() const { return sdcard + "/Emus/" + platform; }
	std::string payloadDir() const { return pakDir + "/payload"; }
	std::string minarchPath() const { return binDir() + "/minarch.elf"; }
	std::string stockPath() const { return binDir() + "/minarch-stock.elf"; }
	std::string glPath() const { return binDir() + "/minarch-gl.elf"; }
	std::string stateDir() const { return userDataDir() + "/minarch-gl"; }
	std::string statePath() const { return stateDir() + "/state"; }
	std::string hookPath() const { return userDataDir() + "/.hooks/boot.d/minarch-gl-guard.sync.sh"; }
};

static const char *kDispatchMarker = "MINARCH-GL-DISPATCH";
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
	if (ok && chmod(p.c_str(), mode) != 0) ok = false;
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

inline bool isOurDispatch(const std::string &p) {
	return readHead(p, 256).find(kDispatchMarker) != std::string::npos;
}

inline bool isELF(const std::string &p) {
	return readHead(p, 4) == std::string("\x7f" "ELF", 4);
}

///////////////////////////////////////////////////////////////////////////////
// generated scripts

inline std::string dispatchScript(const Paths &p) {
	return std::string("#!/bin/sh\n") +
		"# " + kDispatchMarker + "\n"
		"# Generated by the minarch-gl switcher (Tools/" + p.platform + "/Minarch GL.pak).\n"
		"# Delete this file and rename minarch-stock.elf to minarch.elf to undo by hand.\n"
		"d=$(cd \"$(dirname \"$0\")\" && pwd)\n"
		"if [ -x \"$d/minarch-gl.elf\" ]; then exec \"$d/minarch-gl.elf\" \"$@\"; fi\n"
		"exec \"$d/minarch-stock.elf\" \"$@\"\n";
}

inline std::string guardScript(const Paths &p) {
	return std::string("#!/bin/sh\n") +
		"# " + kDispatchMarker + " guard -- installed by the switcher.\n"
		"# A NextUI update replaces .system/ wholesale.  If that happens while\n"
		"# minarch-gl is marked enabled, hide the GL-only paks again so a stock\n"
		"# frontend never tries to load a core it cannot drive.\n"
		"[ \"$(cat \"" + p.statePath() + "\" 2>/dev/null)\" = \"gl\" ] || exit 0\n"
		"grep -q " + kDispatchMarker + " \"" + p.minarchPath() + "\" 2>/dev/null && exit 0\n"
		"for q in \"" + p.emusDir() + "\"/*.pak; do\n"
		"  [ -f \"$q/" + kPakMarker + "\" ] && mv \"$q\" \"$q.disabled\"\n"
		"done\n"
		"echo needs-reinstall > \"" + p.stateDir() + "/needs-reinstall\"\n";
}

///////////////////////////////////////////////////////////////////////////////
// state

inline State currentState(const Paths &p) {
	const std::string m = p.minarchPath();
	if (isOurDispatch(m)) return exists(p.glPath()) ? State::Enabled : State::NeedsReinstall;
	if (isELF(m)) return readTrim(p.statePath()) == "gl" ? State::NeedsReinstall : State::Stock;
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

inline bool ensureStockBackup(const Paths &p, std::string &err) {
	const std::string m = p.minarchPath(), s = p.stockPath();
	if (exists(s)) return true;
	if (!exists(m)) { err = "minarch.elf not found"; return false; }
	if (!copyFile(m, s)) { err = "cannot back up minarch.elf (SD card full?)"; return false; }
	return true;
}

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
	if (!exists(payload)) { r.err = "payload missing from pak: " + payload; return r; }
	if (!ensureStockBackup(p, r.err)) return r;

	const std::string gl_tmp = p.glPath() + ".new";
	if (!copyFile(payload, gl_tmp)) { r.err = "cannot copy minarch-gl.elf (SD card full?)"; return r; }
	if (rename(gl_tmp.c_str(), p.glPath().c_str()) != 0) {
		r.err = "cannot install minarch-gl.elf"; unlink(gl_tmp.c_str()); return r;
	}

	const std::string m_tmp = p.minarchPath() + ".new";
	if (!writeFile(m_tmp, dispatchScript(p), 0755)) { r.err = "cannot write the dispatch script"; return r; }
	if (rename(m_tmp.c_str(), p.minarchPath().c_str()) != 0) {
		r.err = "cannot replace minarch.elf"; unlink(m_tmp.c_str()); return r;
	}

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
	const std::string m = p.minarchPath(), s = p.stockPath();
	if (isOurDispatch(m)) {
		if (!exists(s)) {
			r.err = "minarch-stock.elf is missing; cannot restore automatically";
			return r;
		}
		// Copy to a temp name and rename over the script: never leave the device
		// without a minarch.elf, even if the copy fails half-way.
		const std::string m_tmp = p.minarchPath() + ".new";
		if (!copyFile(s, m_tmp)) { r.err = "cannot restore minarch.elf (SD card full?)"; return r; }
		if (rename(m_tmp.c_str(), m.c_str()) != 0) {
			r.err = "cannot restore minarch.elf"; unlink(m_tmp.c_str()); return r;
		}
		unlink(s.c_str());
	}
	unlink(p.glPath().c_str());
	mkdirs(p.stateDir());
	writeFile(p.statePath(), "stock\n", 0644);
	unlink(p.hookPath().c_str());
	r.disabled = disableMarkedPaks(p);
	sync();
	r.ok = true;
	return r;
}

} // namespace mswitch
