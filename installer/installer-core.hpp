// installer-core.hpp -- everything the installer knows about the card.
//
// Kept free of SDL and of every NextUI header so it can be compiled and tested
// on the build host (tests/installer-core-test.cpp).  The UI in
// minarch-gl-installer.cpp only decides *when* to call these functions; every
// decision about the device's files lives here.
//
// On-card layout (all under a Paths root, so tests can point it at a temp dir):
//
//   .system/<plat>/bin/minarch.elf                  the frontend that actually runs
//   .userdata/<plat>/minarch-gl/minarch-stock.elf   copy of the stock frontend
//   .userdata/<plat>/minarch-gl/minarch-gl.elf      copy of our frontend
//   .userdata/<plat>/minarch-gl/core-list.json      cached release manifest
//   .userdata/<plat>/minarch-gl/SHA256SUMS          cached checksums
//   .userdata/<plat>/minarch-gl/dl/                 downloads (verified files are reused)
//   .userdata/<plat>/minarch-gl/bundled/            offline copies; used first, never written
//   Emus/<plat>/<TAG>.pak/                          installed core paks (they carry a
//                                                   `version` file: "<name> <pin>")
//
// Two rules keep the state model small enough to be obvious:
//
//   1. There is no state file and no marker file.  What is installed is the set
//      of files that exist; what is latest is what the release manifest says.
//      Everything else is derived on the spot (5.4.2 in .docs/HANDOVER-20261118.md).
//   2. Ownership is by name: our core paks are the tags listed in the manifest
//      (`LIBRETRO_*`), the frontend is the two copies under .userdata plus the
//      bytes in .system/bin.  Nothing outside those names is ever touched.
//
// The two backups live under .userdata on purpose: a NextUI update deletes
// .system/<plat>/bin wholesale before extracting a release, so a backup kept
// next to minarch.elf would not survive an update.
#pragma once

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/evp.h>

namespace mgl {

///////////////////////////////////////////////////////////////////////////////
// release location

// All components of one release sit next to each other, so a single directory
// URL is enough: no API call, no rate limit, and `releases/latest/download/`
// always redirects to the newest release.
static const char *kDefaultBaseUrl =
	"https://github.com/zhangmq/minarch-gl/releases/latest/download";

// Testing a private build before the repository is public: point this at a
// directory holding the same files (e.g. `python3 -m http.server` on the build
// host).  It only changes where bytes come from; the sha256 checks stay.
static const char *kBaseUrlEnv = "MINARCHGL_BASE_URL";

// Shape of core-list.json.  An unknown schema means a newer release format:
// stop and tell the user to update the installer rather than guessing.
static const int kSchema = 1;

static const char *kCoreListName = "core-list.json";
static const char *kSumsName = "SHA256SUMS";

///////////////////////////////////////////////////////////////////////////////
// paths

struct Paths {
	std::string sdcard;    // /mnt/sdcard
	std::string platform;  // h700

	std::string systemDir() const { return sdcard + "/.system/" + platform; }
	std::string binDir() const { return systemDir() + "/bin"; }
	std::string minarchPath() const { return binDir() + "/minarch.elf"; }
	std::string userDataDir() const { return sdcard + "/.userdata/" + platform; }
	std::string stateDir() const { return userDataDir() + "/minarch-gl"; }
	std::string stockPath() const { return stateDir() + "/minarch-stock.elf"; }
	std::string glPath() const { return stateDir() + "/minarch-gl.elf"; }
	std::string coreListPath() const { return stateDir() + "/" + kCoreListName; }
	std::string sumsPath() const { return stateDir() + "/" + kSumsName; }
	std::string dlDir() const { return stateDir() + "/dl"; }
	std::string bundledDir() const { return stateDir() + "/bundled"; }
	std::string logsDir() const { return userDataDir() + "/logs"; }
	// One key per line; see onlineUpdatesEnabled()/setOnlineUpdates().
	std::string prefsPath() const { return stateDir() + "/prefs"; }
	std::string emusDir() const { return sdcard + "/Emus/" + platform; }
	std::string corePakDir(const std::string &tag) const { return emusDir() + "/" + tag + ".pak"; }
	std::string coreTmpDir(const std::string &tag) const { return emusDir() + "/.tmp-" + tag; }
	std::string coreBakDir(const std::string &tag) const { return emusDir() + "/.bak-" + tag; }
	std::string romsDir() const { return sdcard + "/Roms"; }
	// minarch hands <card>/Bios/<TAG> to the core as its libretro system
	// directory (ma_core.c), so BIOS is keyed by tag and is not per-platform.
	std::string biosDir(const std::string &tag) const { return sdcard + "/Bios/" + tag; }
	// The per-platform frontend asset: the platform name is the only thing that
	// distinguishes one build from another, there is no version in the name.
	std::string frontendAsset() const { return "minarch-gl_" + platform + ".elf"; }
};

// A row of the UI's busy overlay ("Downloading flycast...").  The file-level
// functions call it before each blocking step so the caller can repaint; tests
// pass an empty function.
using Progress = std::function<void(const std::string &stage, const std::string &subject)>;

inline void report(const Progress &p, const char *stage, const std::string &subject) {
	if (p) p(stage, subject);
}

///////////////////////////////////////////////////////////////////////////////
// file helpers

inline bool fileExists(const std::string &p) { return access(p.c_str(), F_OK) == 0; }

inline bool dirExists(const std::string &p) {
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Whole file as a string, but never more than `limit` bytes: the manifest and
// the checksum table are small, and a wrong path must not read a 26 MB core
// into memory.
inline std::string readFile(const std::string &p, size_t limit = 4u << 20) {
	std::string out;
	FILE *f = fopen(p.c_str(), "rb");
	if (!f) return out;
	out.resize(limit);
	size_t got = fread(&out[0], 1, limit, f);
	fclose(f);
	out.resize(got);
	return out;
}

inline std::string trim(const std::string &s) {
	size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
	return s.substr(b, e - b);
}

// First line of a small text file, trimmed; "" when unreadable.
inline std::string readLine(const std::string &p) {
	const std::string s = readFile(p, 512);
	const size_t nl = s.find('\n');
	return trim(nl == std::string::npos ? s : s.substr(0, nl));
}

// Write via a temporary name and rename, so a failure never leaves a half
// written file where a complete one used to be.
inline bool writeFileAtomic(const std::string &p, const std::string &data, mode_t mode) {
	const std::string tmp = p + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) return false;
	bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	if (fclose(f) != 0) ok = false;
	if (ok && chmod(tmp.c_str(), mode) != 0) {
		// FAT/exFAT have no permission bits: chmod may fail and the mount options
		// decide the mode.  Accept that only when the file already has the mode we
		// want, so a real failure on a permission-carrying filesystem still aborts.
		struct stat st;
		if (stat(tmp.c_str(), &st) != 0 || (st.st_mode & 0777) != (mode & 0777)) ok = false;
	}
	if (ok && rename(tmp.c_str(), p.c_str()) != 0) ok = false;
	if (!ok) unlink(tmp.c_str());
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

// copyFile + chmod + rename: the destination is either the old file or the
// complete new one, never a truncated mix.
inline bool copyFileAtomic(const std::string &from, const std::string &to, mode_t mode) {
	const std::string tmp = to + ".tmp";
	if (!copyFile(from, tmp)) { unlink(tmp.c_str()); return false; }
	if (chmod(tmp.c_str(), mode) != 0) {
		struct stat st;
		if (stat(tmp.c_str(), &st) != 0 || (st.st_mode & 0777) != (mode & 0777)) {
			unlink(tmp.c_str());
			return false;
		}
	}
	if (rename(tmp.c_str(), to.c_str()) != 0) { unlink(tmp.c_str()); return false; }
	return true;
}

inline bool removeTree(const std::string &p) {
	struct stat st;
	if (lstat(p.c_str(), &st) != 0) return true;
	if (!S_ISDIR(st.st_mode)) return unlink(p.c_str()) == 0;
	DIR *d = opendir(p.c_str());
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) != NULL) {
			const std::string n = e->d_name;
			if (n == "." || n == "..") continue;
			removeTree(p + "/" + n);
		}
		closedir(d);
	}
	return rmdir(p.c_str()) == 0;
}

// mkdir -p: the state directories live below paths that may not exist yet
// (fresh card, or after the user wiped .userdata).
inline void mkdirs(const std::string &p) {
	for (size_t i = 1; i <= p.size(); i++) {
		if (i == p.size() || p[i] == '/') {
			const std::string sub = p.substr(0, i);
			if (!sub.empty()) mkdir(sub.c_str(), 0755);
		}
	}
}

inline void forEachDir(const std::string &dir, const std::function<void(const std::string &, const std::string &)> &fn) {
	DIR *d = opendir(dir.c_str());
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		const std::string name = e->d_name;
		if (name == "." || name == "..") continue;
		fn(dir + "/" + name, name);
	}
	closedir(d);
}

inline void syncNow() { sync(); }

// The tool's own preferences: a small text file under .userdata, one key per
// line.  Only "online" today, and a missing file means on -- a card that never
// saw a choice keeps updating.  All file logic lives here so the tests cover it
// and the UI only decides when to ask.
inline bool onlineUpdatesEnabled(const Paths &p) {
	const std::string s = readFile(p.prefsPath(), 4 * 1024);
	size_t pos = 0;
	while (pos < s.size()) {
		const size_t nl = s.find('\n', pos);
		const std::string line = trim(s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
		if (line.rfind("online=", 0) == 0) {
			const std::string v = trim(line.substr(7));
			if (v == "0" || v == "off" || v == "false") return false;
			if (v == "1" || v == "on" || v == "true") return true;
		}
		if (nl == std::string::npos) break;
		pos = nl + 1;
	}
	return true;
}

// Rewrite only the online= line: a prefs file from a newer build keeps whatever
// else it carries.
inline bool setOnlineUpdates(const Paths &p, bool on) {
	const std::string old = readFile(p.prefsPath(), 4 * 1024);
	std::string kept;
	size_t pos = 0;
	while (pos < old.size()) {
		const size_t nl = old.find('\n', pos);
		const std::string line = old.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		if (line.rfind("online=", 0) != 0 && !trim(line).empty()) kept += line + "\n";
		if (nl == std::string::npos) break;
		pos = nl + 1;
	}
	mkdirs(p.stateDir());
	const std::string data = kept + "online=" + (on ? "1" : "0") + "\n";
	if (!writeFileAtomic(p.prefsPath(), data, 0644)) return false;
	syncNow();
	return true;
}

// Whether a URL leaves the card.  Offline mode refuses these, but keeps file://
// working: the Full Pack installs through file:// and so does the dev override.
inline bool isRemoteUrl(const std::string &url) {
	return url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
}
inline bool isLocalUrl(const std::string &url) { return url.compare(0, 7, "file://") == 0; }

// Bytes available to an unprivileged writer; -1 when the filesystem cannot say.
inline long long freeBytes(const std::string &path) {
	struct statvfs st;
	if (statvfs(path.c_str(), &st) != 0) return -1;
	return (long long)st.f_bavail * (long long)st.f_frsize;
}

///////////////////////////////////////////////////////////////////////////////
// hashing

// Lowercase sha256 of a file's contents, or "" when it cannot be read.
//
// sha256 and not md5: this value is compared against the release checksums, so
// the same number has to serve as "which build is this" and "is it intact".
// One file is ~660 KB, so hashing it while entering a screen is cheap.
inline std::string sha256Hex(const std::string &path) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return std::string();
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (!ctx) { fclose(f); return std::string(); }
	bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1;
	unsigned char buf[64 * 1024];
	size_t n;
	while (ok && (n = fread(buf, 1, sizeof buf, f)) > 0) ok = EVP_DigestUpdate(ctx, buf, n) == 1;
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int mdlen = 0;
	ok = ok && ferror(f) == 0 && EVP_DigestFinal_ex(ctx, md, &mdlen) == 1 && mdlen == 32;
	EVP_MD_CTX_free(ctx);
	fclose(f);
	if (!ok) return std::string();
	static const char *hex = "0123456789abcdef";
	std::string out(64, '0');
	for (unsigned int i = 0; i < 32; i++) {
		out[i * 2] = hex[md[i] >> 4];
		out[i * 2 + 1] = hex[md[i] & 0xf];
	}
	return out;
}

// First 8 hex digits, for the Status screen.  Two different builds colliding on
// 8 digits is possible in theory and harmless here: it is only a label, every
// comparison uses the full hash.
inline std::string shortHash(const std::string &h) {
	return h.size() >= 8 ? h.substr(0, 8) : h;
}

inline bool looksLikeSha256(const std::string &s) {
	if (s.size() != 64) return false;
	for (char c : s) {
		const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
		if (!hex) return false;
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// processes (curl, tar) and the Env that carries them

// Run a program without a shell; stdout/stderr go to the given files (or
// /dev/null).  Returns the exit status, or -1 when it could not be run.
inline int runProcess(const std::vector<std::string> &args, const std::string &outPath, const std::string &errPath) {
	if (args.empty()) return -1;
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		const int fd = open(outPath.empty() ? "/dev/null" : outPath.c_str(),
			O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { dup2(fd, 1); if (fd > 2) close(fd); }
		const int fe = open(errPath.empty() ? "/dev/null" : errPath.c_str(),
			O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fe >= 0) { dup2(fe, 2); if (fe > 2) close(fe); }
		std::vector<char *> argv;
		argv.reserve(args.size() + 1);
		for (const std::string &a : args) argv.push_back(const_cast<char *>(a.c_str()));
		argv.push_back(NULL);
		execvp(argv[0], &argv[0]);
		_exit(127);
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
	if (!WIFEXITED(status)) return -1;
	return WEXITSTATUS(status);
}

// Last non-empty line of a small text file, for error messages.
inline std::string lastLine(const std::string &path) {
	const std::string s = readFile(path, 64 * 1024);
	size_t end = s.size();
	while (end > 0 && (s[end - 1] == '\n' || s[end - 1] == '\r' || s[end - 1] == ' ')) end--;
	const size_t nl = s.rfind('\n', end == 0 ? 0 : end - 1);
	std::string line = s.substr(nl == std::string::npos ? 0 : nl + 1, end - (nl == std::string::npos ? 0 : nl + 1));
	return trim(line);
}

inline std::string baseUrl() {
	const char *v = getenv(kBaseUrlEnv);
	if (v && *v) {
		std::string s = v;
		while (!s.empty() && s.back() == '/') s.pop_back();
		return s;
	}
	return kDefaultBaseUrl;
}

// curl as a subprocess: the device ships curl, and a subprocess is the only way
// to get redirects + TLS without adding a dependency to this tool.
inline bool curlFetch(const std::string &url, const std::string &dest, int timeoutSec, std::string &err) {
	const std::string errFile = dest + ".curl-err";
	const std::string connect = std::to_string(std::min(timeoutSec, 10));
	const std::vector<std::string> args = {
		"curl", "-fsSL",
		"--connect-timeout", connect,
		"--max-time", std::to_string(timeoutSec),
		"-o", dest, url,
	};
	const int rc = runProcess(args, "", errFile);
	if (rc != 0) {
		const std::string why = lastLine(errFile);
		err = "cannot fetch " + url + (why.empty() ? std::string() : " (" + why + ")");
		unlink(dest.c_str());
		unlink(errFile.c_str());
		return false;
	}
	unlink(errFile.c_str());
	return true;
}

// A tar member is only accepted when it stays inside the destination: no
// absolute paths, no "..".  Our archives are built by our own release script,
// but extraction runs as root and the rule costs nothing.
inline bool safeArchiveName(const std::string &name) {
	if (name.empty() || name[0] == '/') return false;
	size_t i = 0;
	while (i <= name.size()) {
		const size_t slash = name.find('/', i);
		const std::string part = name.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
		if (part == "..") return false;
		if (slash == std::string::npos) break;
		i = slash + 1;
	}
	return true;
}

// True when the tree contains a symlink or a special file.  tar would happily
// create one, and a symlink planted in a core pak could point anywhere on the
// card; the release never contains one, so their presence means bad input.
inline bool treeHasLinks(const std::string &dir) {
	bool found = false;
	forEachDir(dir, [&](const std::string &path, const std::string &) {
		if (found) return;
		struct stat st;
		if (lstat(path.c_str(), &st) != 0) return;
		if (S_ISLNK(st.st_mode)) { found = true; return; }
		if (S_ISDIR(st.st_mode)) found = treeHasLinks(path);
		else if (!S_ISREG(st.st_mode)) found = true;
	});
	return found;
}

inline bool unpackArchive(const std::string &archive, const std::string &destDir,
                          const std::string &workDir, std::string &err) {
	mkdirs(workDir);
	const std::string listPath = workDir + "/.tar-list";
	const std::string errPath = workDir + "/.tar-err";

	if (runProcess({"tar", "-tzf", archive}, listPath, errPath) != 0) {
		const std::string why = lastLine(errPath);
		err = "cannot read " + archive + (why.empty() ? std::string() : " (" + why + ")");
		return false;
	}
	const std::string listing = readFile(listPath, 8u << 20);
	size_t pos = 0;
	while (pos < listing.size()) {
		const size_t nl = listing.find('\n', pos);
		const std::string name = trim(listing.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
		if (!name.empty() && !safeArchiveName(name)) {
			err = "refusing to extract " + name;
			return false;
		}
		if (nl == std::string::npos) break;
		pos = nl + 1;
	}

	mkdirs(destDir);
	if (runProcess({"tar", "-xzf", archive, "-C", destDir}, "", errPath) != 0) {
		const std::string why = lastLine(errPath);
		err = "cannot extract " + archive + (why.empty() ? std::string() : " (" + why + ")");
		removeTree(destDir);
		return false;
	}
	unlink(listPath.c_str());
	unlink(errPath.c_str());
	if (treeHasLinks(destDir)) {
		err = "the archive contains a link; refusing to install it";
		removeTree(destDir);
		return false;
	}
	return true;
}

// Everything this tool does to the network or to archives goes through here, so
// the tests can replace both with plain local copies and stay offline.
struct Env {
	std::string baseUrl;
	// false = online updates are off: the fetch below refuses http(s) and still
	// accepts file://, so the Full Pack and the dev override keep working.
	bool online = true;
	std::function<bool(const std::string &url, const std::string &dest, int timeoutSec, std::string &err)> fetch;
	std::function<bool(const std::string &archive, const std::string &destDir,
	                   const std::string &workDir, std::string &err)> unpack;

	std::string urlFor(const std::string &asset) const { return baseUrl + "/" + asset; }
};

inline Env defaultEnv(bool online = true) {
	Env e;
	e.baseUrl = baseUrl();
	e.online = online;
	e.fetch = [online](const std::string &url, const std::string &dest, int timeoutSec, std::string &err) {
		if (!online && isRemoteUrl(url)) {
			err = "online updates are off";
			return false;
		}
		return curlFetch(url, dest, timeoutSec, err);
	};
	e.unpack = [](const std::string &archive, const std::string &destDir,
	              const std::string &workDir, std::string &err) {
		return unpackArchive(archive, destDir, workDir, err);
	};
	return e;
}

///////////////////////////////////////////////////////////////////////////////
// JSON (only what a release manifest needs)

struct Json {
	enum class Type { Null, Bool, Number, String, Array, Object };
	Type type = Type::Null;
	bool boolean = false;
	double number = 0;
	std::string text;
	std::vector<Json> items;                          // array
	std::vector<std::pair<std::string, Json>> members; // object

	bool isString() const { return type == Type::String; }
	bool isNumber() const { return type == Type::Number; }
	bool isArray() const { return type == Type::Array; }
	bool isObject() const { return type == Type::Object; }
	const Json *find(const std::string &key) const {
		if (type != Type::Object) return NULL;
		for (const auto &m : members)
			if (m.first == key) return &m.second;
		return NULL;
	}
};

class JsonParser {
public:
	explicit JsonParser(const std::string &s) : s_(s), i_(0) {}

	bool parse(Json &out, std::string &err) {
		skipSpace();
		if (!value(out, 0)) { err = err_.empty() ? "malformed JSON" : err_; return false; }
		skipSpace();
		if (i_ != s_.size()) { err = at("trailing characters"); return false; }
		return true;
	}

private:
	const std::string &s_;
	size_t i_;
	std::string err_;

	std::string at(const char *what) const {
		return std::string(what) + " at offset " + std::to_string((unsigned long)i_);
	}
	void skipSpace() {
		while (i_ < s_.size()) {
			const char c = s_[i_];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') i_++;
			else break;
		}
	}
	bool literal(const char *word) {
		const size_t n = strlen(word);
		if (s_.compare(i_, n, word) != 0) { err_ = at("expected literal"); return false; }
		i_ += n;
		return true;
	}
	bool string(std::string &out) {
		if (i_ >= s_.size() || s_[i_] != '"') { err_ = at("expected string"); return false; }
		i_++;
		out.clear();
		while (i_ < s_.size()) {
			const char c = s_[i_++];
			if (c == '"') return true;
			if (c != '\\') { out.push_back(c); continue; }
			if (i_ >= s_.size()) break;
			const char e = s_[i_++];
			switch (e) {
			case '"': out.push_back('"'); break;
			case '\\': out.push_back('\\'); break;
			case '/': out.push_back('/'); break;
			case 'b': out.push_back('\b'); break;
			case 'f': out.push_back('\f'); break;
			case 'n': out.push_back('\n'); break;
			case 'r': out.push_back('\r'); break;
			case 't': out.push_back('\t'); break;
			case 'u': {
				// Only the ASCII range matters for a manifest; anything else is
				// kept as a placeholder instead of guessing an encoding.
				if (i_ + 4 > s_.size()) { err_ = at("truncated \\u escape"); return false; }
				unsigned int v = 0;
				for (int k = 0; k < 4; k++) {
					const char h = s_[i_ + k];
					v <<= 4;
					if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
					else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
					else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
					else { err_ = at("bad \\u escape"); return false; }
				}
				i_ += 4;
				out.push_back(v < 0x80 ? (char)v : '?');
				break;
			}
			default: err_ = at("bad escape"); return false;
			}
		}
		err_ = at("unterminated string");
		return false;
	}
	bool value(Json &out, int depth) {
		if (depth > 32) { err_ = at("nesting too deep"); return false; }
		skipSpace();
		if (i_ >= s_.size()) { err_ = at("unexpected end"); return false; }
		const char c = s_[i_];
		if (c == '{') return object(out, depth);
		if (c == '[') return array(out, depth);
		if (c == '"') { out.type = Json::Type::String; return string(out.text); }
		if (c == 't') { out.type = Json::Type::Bool; out.boolean = true; return literal("true"); }
		if (c == 'f') { out.type = Json::Type::Bool; out.boolean = false; return literal("false"); }
		if (c == 'n') { out.type = Json::Type::Null; return literal("null"); }
		return number(out);
	}
	bool number(Json &out) {
		const size_t start = i_;
		if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) i_++;
		bool digits = false;
		while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') { i_++; digits = true; }
		if (i_ < s_.size() && s_[i_] == '.') {
			i_++;
			while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') { i_++; digits = true; }
		}
		if (!digits) { err_ = at("expected value"); return false; }
		if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
			i_++;
			if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) i_++;
			while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') i_++;
		}
		out.type = Json::Type::Number;
		out.number = strtod(s_.substr(start, i_ - start).c_str(), NULL);
		return true;
	}
	bool array(Json &out, int depth) {
		out.type = Json::Type::Array;
		i_++; // [
		skipSpace();
		if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
		for (;;) {
			Json v;
			if (!value(v, depth + 1)) return false;
			out.items.push_back(std::move(v));
			skipSpace();
			if (i_ >= s_.size()) { err_ = at("unterminated array"); return false; }
			if (s_[i_] == ',') { i_++; continue; }
			if (s_[i_] == ']') { i_++; return true; }
			err_ = at("expected , or ]");
			return false;
		}
	}
	bool object(Json &out, int depth) {
		out.type = Json::Type::Object;
		i_++; // {
		skipSpace();
		if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
		for (;;) {
			skipSpace();
			std::string key;
			if (!string(key)) return false;
			skipSpace();
			if (i_ >= s_.size() || s_[i_] != ':') { err_ = at("expected :"); return false; }
			i_++;
			Json v;
			if (!value(v, depth + 1)) return false;
			out.members.push_back(std::make_pair(std::move(key), std::move(v)));
			skipSpace();
			if (i_ >= s_.size()) { err_ = at("unterminated object"); return false; }
			if (s_[i_] == ',') { i_++; continue; }
			if (s_[i_] == '}') { i_++; return true; }
			err_ = at("expected , or }");
			return false;
		}
	}
};

inline bool jsonParse(const std::string &text, Json &out, std::string &err) {
	JsonParser p(text);
	return p.parse(out, err);
}

///////////////////////////////////////////////////////////////////////////////
// release manifest

struct CoreEntry {
	std::string tag;                  // LIBRETRO_FLYCAST (also the pak directory name)
	std::string name;                 // flycast (the core name inside the pak)
	std::string pin;                  // aa97a6d6: the commit the core was built from
	std::string file;                 // LIBRETRO_FLYCAST_h700.pak.tar.gz (release asset)
	std::string sha256;               // of that asset
	long long size = 0;               // of that asset
	std::vector<std::string> folders; // default ROM folders ("Sega Dreamcast", ...)
};

struct CoreList {
	int schema = 0;
	bool hasPlatform = false;   // false: this release has nothing for our platform
	std::vector<CoreEntry> cores;

	const CoreEntry *find(const std::string &tag) const {
		for (const CoreEntry &e : cores)
			if (e.tag == tag) return &e;
		return NULL;
	}
};

// Asset names become both a URL path and a local file name; keep them to one
// flat path component so a manifest cannot redirect a write anywhere else.
inline bool safeAssetName(const std::string &n) {
	if (n.empty() || n.size() > 128) return false;
	if (n == "." || n == "..") return false;
	for (char c : n) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
		if (!ok) return false;
	}
	return true;
}

inline bool safeTag(const std::string &t) {
	if (t.size() < 10 || t.size() > 64) return false;
	if (t.compare(0, 9, "LIBRETRO_") != 0) return false;
	for (char c : t) {
		const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
		if (!ok) return false;
	}
	return true;
}

// A folder name is a single path component under Roms/.
inline bool safeFolderName(const std::string &f) {
	if (f.empty() || f.size() > 96) return false;
	if (f == "." || f == "..") return false;
	return f.find('/') == std::string::npos && f.find('\\') == std::string::npos;
}

inline bool parseCoreEntry(const Json &o, CoreEntry &e, std::string &err) {
	if (!o.isObject()) { err = "core entry is not an object"; return false; }
	const Json *tag = o.find("tag");
	const Json *name = o.find("name");
	const Json *pin = o.find("pin");
	const Json *file = o.find("file");
	const Json *sha = o.find("sha256");
	if (!tag || !tag->isString() || !safeTag(tag->text)) { err = "bad core tag"; return false; }
	if (!name || !name->isString() || name->text.empty()) { err = "bad core name for " + tag->text; return false; }
	if (!pin || !pin->isString() || pin->text.empty()) { err = "bad pin for " + tag->text; return false; }
	if (!file || !file->isString() || !safeAssetName(file->text)) { err = "bad file for " + tag->text; return false; }
	if (!sha || !sha->isString() || !looksLikeSha256(sha->text)) { err = "bad sha256 for " + tag->text; return false; }
	e.tag = tag->text;
	e.name = name->text;
	e.pin = pin->text;
	e.file = file->text;
	e.sha256 = sha->text;
	e.size = 0;
	if (const Json *size = o.find("size")) {
		if (!size->isNumber() || size->number < 0) { err = "bad size for " + e.tag; return false; }
		e.size = (long long)size->number;
	}
	if (const Json *folders = o.find("folders")) {
		if (!folders->isArray()) { err = "bad folders for " + e.tag; return false; }
		for (const Json &f : folders->items) {
			if (!f.isString() || !safeFolderName(f.text)) { err = "bad folder name for " + e.tag; return false; }
			e.folders.push_back(f.text);
		}
	}
	return true;
}

inline bool parseCoreList(const std::string &text, const std::string &platform, CoreList &out, std::string &err) {
	Json root;
	if (!jsonParse(text, root, err)) return false;
	if (!root.isObject()) { err = "manifest is not an object"; return false; }
	const Json *schema = root.find("schema");
	if (!schema || !schema->isNumber()) { err = "manifest has no schema"; return false; }
	out.schema = (int)schema->number;
	if (out.schema != kSchema) {
		err = "unsupported manifest schema " + std::to_string(out.schema);
		return false;
	}
	const Json *platforms = root.find("platforms");
	if (!platforms || !platforms->isObject()) { err = "manifest has no platforms"; return false; }
	const Json *plat = platforms->find(platform);
	if (!plat) {
		// A valid release that does not cover this platform is not an error, it
		// is an empty list: the UI says so and everything local still works.
		out.hasPlatform = false;
		return true;
	}
	if (!plat->isObject()) { err = "platform entry is not an object"; return false; }
	out.hasPlatform = true;
	const Json *cores = plat->find("cores");
	if (!cores || !cores->isArray()) { err = "platform has no cores array"; return false; }
	for (const Json &o : cores->items) {
		CoreEntry e;
		if (!parseCoreEntry(o, e, err)) return false;
		if (out.find(e.tag)) { err = "duplicate core " + e.tag; return false; }
		out.cores.push_back(e);
	}
	return true;
}

// SHA256SUMS is the standard "sha256␠␠name" format; `*name` (binary marker) is
// accepted too, and so is any amount of whitespace between the two.
inline std::vector<std::pair<std::string, std::string>> parseSums(const std::string &text) {
	std::vector<std::pair<std::string, std::string>> out;
	size_t pos = 0;
	while (pos < text.size()) {
		const size_t nl = text.find('\n', pos);
		const std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
		if (line.size() > 66) {
			const std::string sum = line.substr(0, 64);
			std::string rest = trim(line.substr(64));
			if (!rest.empty() && rest[0] == '*') rest.erase(0, 1);
			if (looksLikeSha256(sum) && safeAssetName(rest)) out.push_back(std::make_pair(rest, sum));
		}
		if (nl == std::string::npos) break;
		pos = nl + 1;
	}
	return out;
}

inline std::string sumFor(const std::vector<std::pair<std::string, std::string>> &sums, const std::string &name) {
	for (const auto &s : sums)
		if (s.first == name) return s.second;
	return std::string();
}

// Where the manifest in hand came from.  Only used to explain the state to the
// user ("online"/"cached"/"local"); it never changes what the actions do.
enum class ManifestSource { None, Bundled, Online, Cached };

inline const char *manifestSourceName(ManifestSource s) {
	switch (s) {
	case ManifestSource::Bundled: return "local";
	case ManifestSource::Online: return "online";
	case ManifestSource::Cached: return "cached";
	default: return "none";
	}
}

struct Manifest {
	bool ok = false;
	std::string err;                  // why it is unavailable, for the UI
	ManifestSource source = ManifestSource::None;
	CoreList list;
	std::string latestGl;             // sha256 of minarch-gl_<plat>.elf, "" if not published
	std::vector<std::pair<std::string, std::string>> sums;
};

// Read a manifest that is already on the card.  Both files must be present: a
// checksum table is what makes an install verifiable, and the bundled/offline
// set and the online download always ship the two together.
inline bool loadLocalManifest(const std::string &dir, const std::string &platform,
                              ManifestSource source, Manifest &out, std::string &err) {
	const std::string listPath = dir + "/" + kCoreListName;
	const std::string sumsPath = dir + "/" + kSumsName;
	if (!fileExists(listPath) || !fileExists(sumsPath)) {
		err = "no " + std::string(kCoreListName) + " in " + dir;
		return false;
	}
	out.sums = parseSums(readFile(sumsPath, 256 * 1024));
	if (out.sums.empty()) { err = "malformed " + std::string(kSumsName) + " in " + dir; return false; }
	const std::string want = sumFor(out.sums, kCoreListName);
	if (!want.empty() && sha256Hex(listPath) != want) {
		err = std::string(kCoreListName) + " does not match " + kSumsName;
		return false;
	}
	if (!parseCoreList(readFile(listPath, 1u << 20), platform, out.list, err)) return false;
	out.ok = true;
	out.source = source;
	out.latestGl = sumFor(out.sums, "minarch-gl_" + platform + ".elf");
	return true;
}

// The manifest the installer should work from.
//
// Order: the network first (it is the only source that can be newer), then a
// local copy (bundled/, from the Full Pack or put there by hand), then the last
// download cached under .userdata.  With online updates off the fetch refuses
// http(s) immediately, so the local copies win without any network wait; a
// file:// base URL (the Full Pack's post_install, the dev override) still works.
inline Manifest loadManifest(const Paths &p, const Env &env, bool allowNetwork,
                             const Progress &progress = nullptr) {
	Manifest m;
	std::string err;
	bool notFound = false;

	if (allowNetwork && env.fetch) {
		mkdirs(p.dlDir());
		const std::string tmpList = p.dlDir() + "/.core-list.json.part";
		const std::string tmpSums = p.dlDir() + "/.SHA256SUMS.part";
		report(progress, env.online ? "Checking" : "Reading", env.online ? "for updates" : "local data");
		std::string e1, e2;
		// Two requests, each with a generous budget: a proxied path (or a slow
		// link) can spend several seconds on connect + TLS + the release
		// redirect, while the screen already shows the busy state.  The old
		// 3 s / 2 s split let SHA256SUMS time out on a cold proxy connection
		// and surface as the generic "No network" message.
		const bool gotList = env.fetch(env.urlFor(kCoreListName), tmpList, 8, e1);
		const bool gotSums = env.fetch(env.urlFor(kSumsName), tmpSums, 8, e2);
		// A failed fetch used to disappear into that generic message; the log is
		// the only place a user can see which request failed and why.
		if (!gotList) fprintf(stderr, "manifest: %s: %s\n", kCoreListName, e1.empty() ? "fetch failed" : e1.c_str());
		if (!gotSums) fprintf(stderr, "manifest: %s: %s\n", kSumsName, e2.empty() ? "fetch failed" : e2.c_str());
		// GitHub answers 404 when the repository has no release yet (or the
		// latest one carries no manifest).  That is not a network failure and
		// should not be reported as one.
		notFound = e1.find("404") != std::string::npos || e2.find("404") != std::string::npos;
		if (gotList && gotSums) {
			Manifest fresh;
			const std::vector<std::pair<std::string, std::string>> sums = parseSums(readFile(tmpSums, 256 * 1024));
			const std::string want = sumFor(sums, kCoreListName);
			if (!sums.empty() && (want.empty() || sha256Hex(tmpList) == want) &&
			    parseCoreList(readFile(tmpList, 1u << 20), p.platform, fresh.list, err)) {
				// Only a manifest that parses is cached: a broken download must not
				// replace the copy that worked last time.
				mkdirs(p.stateDir());
				rename(tmpSums.c_str(), p.sumsPath().c_str());
				rename(tmpList.c_str(), p.coreListPath().c_str());
				fresh.ok = true;
				fresh.source = ManifestSource::Online;
				fresh.sums = sums;
				fresh.latestGl = sumFor(sums, "minarch-gl_" + p.platform + ".elf");
				syncNow();
				return fresh;
			}
		}
		unlink(tmpList.c_str());
		unlink(tmpSums.c_str());
	}
	m = Manifest();

	if (loadLocalManifest(p.bundledDir(), p.platform, ManifestSource::Bundled, m, err)) return m;
	m = Manifest();

	Manifest cached;
	if (loadLocalManifest(p.stateDir(), p.platform, ManifestSource::Cached, cached, err)) return cached;

	m = Manifest();
	m.ok = false;
	m.err = notFound ? "No release has been published yet."
	                 : (env.online ? "No network and no local copy."
	                               : "Online updates are off and no local copy.");
	return m;
}

///////////////////////////////////////////////////////////////////////////////
// downloading a verified asset

struct Asset {
	std::string path;        // verified local file
	bool downloaded = false;
};

// Find or fetch one release asset and make sure its bytes are the ones the
// manifest announced.  A file that is already on the card wins: offline copies
// first (bundled/), then the previous download (dl/), then the network.
inline bool acquireAsset(const Paths &p, const Env &env, const std::string &name,
                         const std::string &sha256, long long size,
                         const Progress &progress, const std::string &subject,
                         Asset &out, std::string &err) {
	if (!safeAssetName(name) || !looksLikeSha256(sha256)) { err = "bad asset entry"; return false; }

	const std::string bundled = p.bundledDir() + "/" + name;
	if (fileExists(bundled) && sha256Hex(bundled) == sha256) {
		out.path = bundled;
		out.downloaded = false;
		return true;
	}
	const std::string cached = p.dlDir() + "/" + name;
	if (fileExists(cached) && sha256Hex(cached) == sha256) {
		out.path = cached;
		out.downloaded = false;
		return true;
	}
	if (!env.fetch) { err = "no network"; return false; }

	mkdirs(p.dlDir());
	if (size > 0) {
		// The download needs its own space; the caller checks the space the
		// extraction needs on top of this.
		const long long free = freeBytes(p.dlDir());
		if (free >= 0 && free < size + (2 << 20)) { err = "not enough space on the card to download " + name; return false; }
	}
	const std::string part = p.dlDir() + "/." + name + ".part";
	unlink(part.c_str());
	report(progress, "Downloading", subject);
	if (!env.fetch(env.urlFor(name), part, 600, err)) { unlink(part.c_str()); return false; }
	report(progress, "Verifying", subject);
	if (sha256Hex(part) != sha256) {
		unlink(part.c_str());
		err = name + " is damaged (checksum mismatch)";
		return false;
	}
	if (size > 0) {
		struct stat st;
		if (stat(part.c_str(), &st) != 0 || (long long)st.st_size != size) {
			unlink(part.c_str());
			err = name + " has the wrong size";
			return false;
		}
	}
	unlink(cached.c_str());
	if (rename(part.c_str(), cached.c_str()) != 0) {
		unlink(part.c_str());
		err = "cannot store " + name;
		return false;
	}
	out.path = cached;
	out.downloaded = true;
	return true;
}

// Whether an action that needs this asset can succeed right now.  Online, any
// published asset qualifies (the download will fetch it).  Offline, only one
// that is already on the card -- or a file:// base URL, which is local.  The UI
// hides rows this says no to, so Y never lands on a button that cannot work.
inline bool assetReachable(const Env &env, const Paths &p, const std::string &name,
                           const std::string &sha256) {
	if (env.online) return true;
	if (isLocalUrl(env.baseUrl)) return true;
	const std::string bundled = p.bundledDir() + "/" + name;
	if (fileExists(bundled) && sha256Hex(bundled) == sha256) return true;
	const std::string cached = p.dlDir() + "/" + name;
	if (fileExists(cached) && sha256Hex(cached) == sha256) return true;
	return false;
}

///////////////////////////////////////////////////////////////////////////////
// frontend state

enum class FrontendState {
	NotInstalled,   // no copy of our build: the card is on stock, or was never set up
	StockLost,      // our build is (or may be) running and the stock copy is gone
	NeedsRepair,    // indeterminate: NextUI replaced the frontend, or a copy is missing
	Disabled,       // stock is running, our build is installed
	Enabled,        // our build is running
};

inline const char *frontendStateName(FrontendState s) {
	switch (s) {
	case FrontendState::NotInstalled: return "not installed";
	case FrontendState::StockLost: return "stock frontend lost";
	case FrontendState::NeedsRepair: return "needs repair";
	case FrontendState::Disabled: return "disabled";
	case FrontendState::Enabled: return "enabled";
	}
	return "unknown";
}

struct FrontendInfo {
	FrontendState state = FrontendState::NotInstalled;
	std::string reason;     // one sentence for the install page
	std::string active;     // sha256 of .system/bin/minarch.elf, "" when unreadable
	std::string stock;      // sha256 of minarch-stock.elf
	std::string gl;         // sha256 of minarch-gl.elf
};

// Which frontend is running, decided only by comparing the three copies.
//
// `latestGl` is the published hash of our build ("" when the manifest has no
// SHA256SUMS entry for it).  It is used for one thing only: recognising that the
// binary running now *is* our build when our own copy was deleted, which would
// otherwise look like a stock frontend and be recorded as the stock backup.
//
// The order matters: `stock` is checked before `gl`, so a card whose stock and
// gl copies are byte-identical is reported as disabled (the safe side).
inline FrontendInfo frontendInfo(const Paths &p, const std::string &latestGl) {
	FrontendInfo f;
	f.active = sha256Hex(p.minarchPath());
	f.stock = sha256Hex(p.stockPath());
	f.gl = sha256Hex(p.glPath());
	const bool hasActive = !f.active.empty();
	const bool hasStock = !f.stock.empty();
	const bool hasGl = !f.gl.empty();
	const bool activeIsGl = hasActive && hasGl && f.active == f.gl;
	const bool activeIsLatest = hasActive && !latestGl.empty() && f.active == latestGl;

	// Running our build with nothing to fall back to.  Repair cannot help: there
	// is no stock binary anywhere on the card.
	if (hasGl && !hasStock && activeIsGl) {
		f.state = FrontendState::StockLost;
		f.reason = "The stock frontend is gone - reinstall NextUI.";
		return f;
	}
	// Same, but our copy is gone too: the running binary is our build only
	// according to the release checksum.
	if (!hasGl && activeIsLatest) {
		f.state = FrontendState::StockLost;
		f.reason = "The stock frontend is gone - reinstall NextUI.";
		return f;
	}
	if (!hasGl && !hasActive && !hasStock) {
		f.state = FrontendState::NeedsRepair;
		f.reason = "The NextUI frontend is missing.";
		return f;
	}
	if (!hasGl) {
		f.state = FrontendState::NotInstalled;
		f.reason = "MinarchGL is not set up.";
		return f;
	}
	if (!hasStock) {
		f.state = FrontendState::NeedsRepair;
		f.reason = "Some files are missing.";
		return f;
	}
	if (!hasActive) {
		f.state = FrontendState::NeedsRepair;
		f.reason = "The NextUI frontend is missing.";
		return f;
	}
	if (f.active == f.stock) {
		f.state = FrontendState::Disabled;
		return f;
	}
	if (f.active == f.gl) {
		f.state = FrontendState::Enabled;
		return f;
	}
	f.state = FrontendState::NeedsRepair;
	f.reason = "The frontend was replaced by a NextUI update.";
	return f;
}

// True when the frontend action on this card is a repair rather than a first
// install.  Only the wording differs; both run installFrontend().
inline bool installedBefore(const FrontendInfo &f) {
	return f.state == FrontendState::NeedsRepair || f.state == FrontendState::StockLost;
}

///////////////////////////////////////////////////////////////////////////////
// results

struct Result {
	bool ok = false;
	std::string err;
};

///////////////////////////////////////////////////////////////////////////////
// frontend actions

// Install / update / repair our frontend from a file whose sha256 has already
// been checked (see acquireAsset).  Order matters (5.7):
//
//   1. make sure a stock copy exists, taking it from whatever runs now --
//      unless what runs now *is* our build, in which case there is nothing to
//      save and the old stock copy is the only way back;
//   2. replace minarch-gl.elf;
//   3. if our build is what is running, replace the running binary too: the
//      user pressed update/repair, so finishing half way would be a bug.
inline Result installFrontend(const Paths &p, const std::string &srcElf,
                              const std::string &latestGl, const Progress &progress) {
	Result r;
	if (!fileExists(srcElf)) { r.err = "the downloaded frontend is missing"; return r; }
	mkdirs(p.stateDir());

	const std::string active = p.minarchPath();
	const std::string activeHash = sha256Hex(active);
	const std::string stockHash = sha256Hex(p.stockPath());
	const std::string glHash = sha256Hex(p.glPath());
	const bool activeIsGl = !activeHash.empty() && !glHash.empty() && activeHash == glHash;
	const bool activeIsLatest = !activeHash.empty() && !latestGl.empty() && activeHash == latestGl;

	report(progress, "Installing", "MinarchGL");

	if (activeIsGl || activeIsLatest) {
		// Nothing to save: the running binary is ours.
		if (stockHash.empty()) {
			r.err = "the stock frontend is missing; reinstall NextUI first";
			return r;
		}
	} else if (!activeHash.empty()) {
		// Whatever NextUI put there is the stock frontend from now on.
		if (!copyFileAtomic(active, p.stockPath(), 0644)) {
			r.err = "cannot save the stock frontend (SD card full?)";
			return r;
		}
	} else {
		// No frontend at all: put the stock copy back, then install ours.  The
		// card would otherwise be left without something to run.
		if (stockHash.empty()) { r.err = "the NextUI frontend is missing and no copy was saved"; return r; }
		if (!copyFileAtomic(p.stockPath(), active, 0755)) {
			r.err = "cannot restore the stock frontend (SD card full?)";
			return r;
		}
	}

	if (!copyFileAtomic(srcElf, p.glPath(), 0644)) {
		r.err = "cannot install minarch-gl.elf (SD card full?)";
		return r;
	}
	if (activeIsGl || activeIsLatest) {
		if (!copyFileAtomic(p.glPath(), active, 0755)) {
			r.err = "cannot replace the running minarch.elf";
			return r;
		}
	}
	syncNow();
	r.ok = true;
	return r;
}

inline Result enableFrontend(const Paths &p) {
	Result r;
	if (!fileExists(p.glPath())) { r.err = "MinarchGL is not installed"; return r; }
	mkdirs(p.binDir());
	if (!copyFileAtomic(p.glPath(), p.minarchPath(), 0755)) {
		r.err = "cannot write .system/bin/minarch.elf (SD card full?)";
		return r;
	}
	syncNow();
	r.ok = true;
	return r;
}

inline Result disableFrontend(const Paths &p) {
	Result r;
	if (!fileExists(p.stockPath())) { r.err = "the stock frontend copy is missing"; return r; }
	if (!copyFileAtomic(p.stockPath(), p.minarchPath(), 0755)) {
		r.err = "cannot write .system/bin/minarch.elf (SD card full?)";
		return r;
	}
	syncNow();
	r.ok = true;
	return r;
}

// Remove MinarchGL from the card: put stock back if ours is running, then drop
// the copies, the cache and the downloads.  Core paks and ROM folders are not
// touched -- they are separate Uninstall rows on purpose.
inline Result uninstallFrontend(const Paths &p) {
	Result r;
	const std::string activeHash = sha256Hex(p.minarchPath());
	const std::string glHash = sha256Hex(p.glPath());
	const bool activeIsGl = !activeHash.empty() && !glHash.empty() && activeHash == glHash;
	if (activeIsGl) {
		if (!fileExists(p.stockPath())) {
			r.err = "the stock frontend copy is missing; reinstall NextUI instead";
			return r;
		}
		if (!copyFileAtomic(p.stockPath(), p.minarchPath(), 0755)) {
			r.err = "cannot restore the stock frontend";
			return r;
		}
	}
	// The online-updates preference is the user's, not part of what a frontend
	// uninstall removes: keep that one file while the backups, cache and
	// downloads go.
	const std::string prefs = readFile(p.prefsPath(), 4 * 1024);
	removeTree(p.stateDir());
	if (!prefs.empty()) {
		mkdirs(p.stateDir());
		writeFileAtomic(p.prefsPath(), prefs, 0644);
	}
	syncNow();
	r.ok = true;
	return r;
}

///////////////////////////////////////////////////////////////////////////////
// core paks

enum class CoreState {
	NotInstalled,
	NotLatest,   // installed, but not the build the manifest points at
	Current,
};

inline const char *coreStateName(CoreState s) {
	switch (s) {
	case CoreState::NotInstalled: return "not installed";
	case CoreState::NotLatest: return "need update";
	default: return "current";
	}
}

struct CoreStatus {
	std::string tag;
	std::string name;
	CoreState state = CoreState::NotInstalled;
	std::string pin;   // installed pin, "" when the version file is unreadable
};

// The pin recorded inside an installed pak: `version` holds "<name> <pin>".
inline std::string coreInstalledPin(const Paths &p, const std::string &tag) {
	const std::string line = readLine(p.corePakDir(tag) + "/version");
	const size_t sp = line.find(' ');
	if (sp == std::string::npos) return std::string();
	return trim(line.substr(sp + 1));
}

// Ownership is by name: a directory named like a manifest tag is ours, whether
// we put it there or the user did.  Nothing else in Emus/ is ever touched, and
// there is no marker file to get out of sync.
inline CoreStatus coreStatus(const Paths &p, const CoreEntry &e) {
	CoreStatus s;
	s.tag = e.tag;
	s.name = e.name;
	if (!dirExists(p.corePakDir(e.tag))) return s;
	s.pin = coreInstalledPin(p, e.tag);
	s.state = (!e.pin.empty() && s.pin == e.pin) ? CoreState::Current : CoreState::NotLatest;
	return s;
}

// Install or update a core pak from a verified archive.
//
// The archive holds the pak's files at its top level, so it extracts straight
// into a temporary sibling directory and is renamed into place: a failed
// download, checksum or extraction never disturbs the working pak.
inline Result installCore(const Paths &p, const Env &env, const CoreEntry &e,
                          const Progress &progress) {
	Result r;
	Asset asset;
	if (!acquireAsset(p, env, e.file, e.sha256, e.size, progress, e.name, asset, r.err)) return r;

	// Extraction needs the unpacked tree next to the old pak until the rename,
	// so budget for the archive, the new tree and the old one.
	if (e.size > 0) {
		const long long need = e.size * 7 + (4 << 20);
		const long long free = freeBytes(p.emusDir());
		if (free >= 0 && free < need) { r.err = "not enough space on the card for " + e.name; return r; }
	}

	mkdirs(p.emusDir());
	removeTree(p.coreTmpDir(e.tag));
	report(progress, "Extracting", e.name);
	std::string err;
	if (!env.unpack || !env.unpack(asset.path, p.coreTmpDir(e.tag), p.dlDir(), err)) {
		removeTree(p.coreTmpDir(e.tag));
		r.err = err.empty() ? "cannot unpack " + e.file : err;
		return r;
	}
	if (!fileExists(p.coreTmpDir(e.tag) + "/launch.sh")) {
		removeTree(p.coreTmpDir(e.tag));
		r.err = e.file + " does not contain a pak";
		return r;
	}
	chmod((p.coreTmpDir(e.tag) + "/launch.sh").c_str(), 0755);

	report(progress, "Installing", e.name);
	removeTree(p.coreBakDir(e.tag));
	const bool hadOld = dirExists(p.corePakDir(e.tag));
	if (hadOld && rename(p.corePakDir(e.tag).c_str(), p.coreBakDir(e.tag).c_str()) != 0) {
		removeTree(p.coreTmpDir(e.tag));
		r.err = "cannot replace the installed " + e.tag + ".pak";
		return r;
	}
	if (rename(p.coreTmpDir(e.tag).c_str(), p.corePakDir(e.tag).c_str()) != 0) {
		// Put the working pak back before reporting the failure.
		if (hadOld) rename(p.coreBakDir(e.tag).c_str(), p.corePakDir(e.tag).c_str());
		removeTree(p.coreTmpDir(e.tag));
		r.err = "cannot install " + e.tag + ".pak";
		return r;
	}
	removeTree(p.coreBakDir(e.tag));
	syncNow();
	r.ok = true;
	return r;
}

inline Result removeCore(const Paths &p, const std::string &tag) {
	Result r;
	if (!safeTag(tag)) { r.err = "refusing to remove " + tag; return r; }
	removeTree(p.corePakDir(tag));
	syncNow();
	r.ok = true;
	return r;
}

///////////////////////////////////////////////////////////////////////////////
// ROM folders

inline std::string romFolderName(const std::string &system, const std::string &tag) {
	return system + " (" + tag + ")";
}

// The tag NextUI would read from a folder name, or "" when there is none: the
// last '(' up to the following ')', exactly like getEmuName() in common/utils.c.
inline std::string folderTag(const std::string &name) {
	const size_t open = name.rfind('(');
	if (open == std::string::npos) return std::string();
	const size_t close = name.find(')', open + 1);
	if (close == std::string::npos) return std::string();
	return name.substr(open + 1, close - open - 1);
}

// Every directory under Roms/ that belongs to a tag, so a folder the user made
// and named correctly counts as much as the default one.
inline std::vector<std::string> romFoldersForTag(const Paths &p, const std::string &tag) {
	std::vector<std::string> out;
	forEachDir(p.romsDir(), [&](const std::string &path, const std::string &name) {
		if (dirExists(path) && folderTag(name) == tag) out.push_back(name);
	});
	std::sort(out.begin(), out.end());
	return out;
}

// Create one core's BIOS directory, "Bios/<TAG>/".  Only the tag directory is
// created: how a core lays out the files inside it is the core's own business
// (see its libretro documentation), so nothing here guesses at subdirectories.
inline Result createBiosFolder(const Paths &p, const std::string &tag) {
	Result r;
	if (!safeTag(tag)) { r.err = "bad core tag " + tag; return r; }
	mkdirs(p.biosDir(tag));
	syncNow();
	r.ok = true;
	return r;
}

// Create one core's default ROM folders, e.g. "Roms/Sega Dreamcast
// (LIBRETRO_FLYCAST)" and "Roms/Sega Naomi (LIBRETRO_FLYCAST)".  Existing
// folders are skipped.
inline Result createRomFolders(const Paths &p, const CoreEntry &e) {
	Result r;
	if (!safeTag(e.tag)) { r.err = "bad core tag " + e.tag; return r; }
	mkdirs(p.romsDir());
	for (const std::string &f : e.folders) {
		if (!safeFolderName(f)) continue;
		const std::string name = romFolderName(f, e.tag);
		const std::string path = p.romsDir() + "/" + name;
		if (dirExists(path)) continue;
		if (mkdir(path.c_str(), 0755) != 0) { r.err = "cannot create Roms/" + name; return r; }
	}
	syncNow();
	r.ok = true;
	return r;
}

// Both at once: what the tool used to do as one button, kept for callers that
// want the whole set (and for the tests).
inline Result createCoreFolders(const Paths &p, const CoreEntry &e) {
	Result r = createBiosFolder(p, e.tag);
	if (!r.ok) return r;
	return createRomFolders(p, e);
}

// Delete one ROM folder of a tag, recursively.  This is the only destructive
// operation the tool offers, so the name must belong to the tag (folderTag) and
// must not escape Roms/: callers pass a name they got from romFoldersForTag.
inline Result deleteRomFolder(const Paths &p, const std::string &tag, const std::string &name) {
	Result r;
	if (!safeTag(tag)) { r.err = "bad core tag " + tag; return r; }
	if (!safeFolderName(name)) { r.err = "bad folder name " + name; return r; }
	if (folderTag(name) != tag) { r.err = "Roms/" + name + " does not belong to " + tag; return r; }
	const std::string path = p.romsDir() + "/" + name;
	struct stat st;
	if (lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
		r.err = "no such folder Roms/" + name;
		return r;
	}
	if (!removeTree(path)) { r.err = "cannot delete Roms/" + name; return r; }
	syncNow();
	r.ok = true;
	return r;
}

// Delete a core's BIOS directory, recursively.  Same contract: the path is the
// one minarch hands the core as its system directory, nothing else.
inline Result deleteBiosFolder(const Paths &p, const std::string &tag) {
	Result r;
	if (!safeTag(tag)) { r.err = "bad core tag " + tag; return r; }
	const std::string path = p.biosDir(tag);
	struct stat st;
	if (lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
		r.err = "no such folder Bios/" + tag;
		return r;
	}
	if (!removeTree(path)) { r.err = "cannot delete Bios/" + tag; return r; }
	syncNow();
	r.ok = true;
	return r;
}

///////////////////////////////////////////////////////////////////////////////
// plain-text status (--status, and the device smoke test)

inline std::string statusText(const Paths &p, const Manifest &m) {
	FrontendInfo f = frontendInfo(p, m.latestGl);
	std::string out;
	out += "platform   " + p.platform + "\n";
	out += "state      " + std::string(frontendStateName(f.state)) + "\n";
	out += "online     " + std::string(onlineUpdatesEnabled(p) ? "on" : "off") + "\n";
	out += "active     " + (f.active.empty() ? std::string("missing") : shortHash(f.active)) + "\n";
	out += "stock      " + (f.stock.empty() ? std::string("missing") : shortHash(f.stock)) + "\n";
	out += "gl         " + (f.gl.empty() ? std::string("missing") : shortHash(f.gl)) + "\n";
	out += "latest gl  " + (m.latestGl.empty() ? std::string("unknown") : shortHash(m.latestGl)) + "\n";
	out += std::string("manifest   ") + manifestSourceName(m.source);
	if (!m.ok) out += " (" + m.err + ")";
	out += "\n";
	if (m.ok) {
		out += "cores      " + std::to_string(m.list.cores.size()) + "\n";
		for (const CoreEntry &e : m.list.cores) {
			const CoreStatus s = coreStatus(p, e);
			out += "  " + e.name + "  " + coreStateName(s.state);
			out += "  latest " + e.pin;
			if (s.state != CoreState::NotInstalled) out += ", installed " + (s.pin.empty() ? std::string("unknown") : s.pin);
			out += "\n";
		}
	}
	return out;
}

} // namespace mgl
