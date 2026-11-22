// Host-side test for installer-core.hpp.
//
// The installer cannot be run on the build host (aarch64 binary, SDL/GLES UI),
// so the part that actually reads the release and mutates the SD card is
// isolated in a header with no SDL dependency and exercised here against a
// temporary directory tree.
//
//   make test
//
// Everything that would touch the network goes through Env, so the tests drive
// the real parsing/verification/install paths against a local directory of
// assets -- the same bytes a release would serve.
//
// Covered:
//   * the manifest: parsing, schema, per-platform sections, hostile names
//   * SHA256SUMS lookup and the "bundled > network > cache" order
//   * every frontend state of 5.4.3 and the actions they allow
//   * core paks: install, update, pin comparison, uninstall, ROM folders
//   * assets: reuse from bundled/, reuse from dl/, checksum rejection

#include "installer-core.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

#define CHECK_EQ_STR(a, b)                                                     \
	do {                                                                       \
		std::string _a = (a), _b = (b);                                         \
		if (_a != _b) {                                                         \
			fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, \
			        _a.c_str(), _b.c_str());                                    \
			failures++;                                                         \
		}                                                                       \
	} while (0)

///////////////////////////////////////////////////////////////////////////////
// helpers

static const char *STOCK_ELF = "\x7f" "ELF stock-binary\n";
static const char *STOCK2_ELF = "\x7f" "ELF stock-v2\n";
static const char *GL_ELF = "\x7f" "ELF gl-binary\n";
static const char *GL2_ELF = "\x7f" "ELF gl-binary-v2\n";

static std::string readAll(const std::string &p) { return readFile(p); }

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

static bool run(const std::string &cmd) { return system(cmd.c_str()) == 0; }

// A release directory on the host: assets, manifest and checksums, exactly the
// shape a GitHub release has.
struct Release {
	std::string dir;
	std::vector<std::string> assets;
	Release(const std::string &d) : dir(d) { mkdirs(d); }

	// <TAG>_h700.pak.tar.gz with the pak's files at the top level
	std::string corePak(const std::string &tag, const std::string &name, const std::string &pin,
	                    const std::string &coreFile) {
		const std::string src = dir + "/.src-" + tag;
		removeTree(src);
		mkdirs(src);
		writeStr(src + "/launch.sh", "#!/bin/sh\nexec minarch.elf \"$@\"\n");
		chmod((src + "/launch.sh").c_str(), 0755);
		writeStr(src + "/version", name + " " + pin + "\n");
		writeStr(src + "/default.cfg", "minarch_frame_pacing = Audio\n");
		writeStr(src + "/" + coreFile, std::string(8192, 'c'));
		const std::string out = tag + "_h700.pak.tar.gz";
		if (!run("tar -czf '" + dir + "/" + out + "' -C '" + src + "' .")) {
			fprintf(stderr, "cannot build %s\n", out.c_str());
			exit(2);
		}
		removeTree(src);
		assets.push_back(out);
		return out;
	}
};

// One core entry as JSON, with the real size/checksum of a file in `assets`.
static std::string entryJson(const Release &rel, const std::string &tag, const std::string &name,
                             const std::string &pin, const std::string &file,
                             const std::vector<std::string> &folders) {
	struct stat st;
	const std::string path = rel.dir + "/" + file;
	if (stat(path.c_str(), &st) != 0) { fprintf(stderr, "missing %s\n", path.c_str()); exit(2); }
	std::string foldersJson;
	for (size_t i = 0; i < folders.size(); i++) {
		if (i) foldersJson += ", ";
		foldersJson += "\"" + folders[i] + "\"";
	}
	return "{\"tag\": \"" + tag + "\", \"name\": \"" + name + "\", \"pin\": \"" + pin +
		"\", \"file\": \"" + file + "\", \"sha256\": \"" + sha256Hex(path) +
		"\", \"size\": " + std::to_string((long long)st.st_size) +
		", \"folders\": [" + foldersJson + "]}";
}

static void writeManifest(Release &rel, const std::string &platform, const std::string &coresJson,
                          const std::string &elfName = "minarch-gl_h700.elf") {
	std::string json = "{\n  \"schema\": 1,\n  \"platforms\": {\n";
	if (!platform.empty())
		json += "    \"" + platform + "\": {\"cores\": [" + coresJson + "]}\n";
	json += "  }\n}\n";
	writeStr(rel.dir + "/core-list.json", json);
	rel.assets.push_back("core-list.json");

	std::string sums;
	std::vector<std::string> names = rel.assets;
	if (!elfName.empty()) {
		const bool have = std::find(names.begin(), names.end(), elfName) != names.end();
		if (!have) names.push_back(elfName);
	}
	for (const std::string &n : names) {
		const std::string path = rel.dir + "/" + n;
		if (!fileExists(path)) continue;
		sums += sha256Hex(path) + "  " + n + "\n";
	}
	writeStr(rel.dir + "/SHA256SUMS", sums);
}

// An Env that serves the release directory and really extracts with tar.  It
// enforces `online` the same way defaultEnv() does, so an offline manifest test
// cannot silently reach the fake network.
static Env fakeEnv(const std::string &assetDir, bool online = true) {
	Env e;
	e.baseUrl = "https://example.invalid/release";
	e.online = online;
	e.fetch = [assetDir, online](const std::string &url, const std::string &dest, int, std::string &err) {
		if (!online && isRemoteUrl(url)) { err = "online updates are off"; return false; }
		const std::string name = url.substr(url.rfind('/') + 1);
		if (!fileExists(assetDir + "/" + name)) { err = "404 " + name; return false; }
		if (!copyFile(assetDir + "/" + name, dest)) { err = "cannot copy " + name; return false; }
		return true;
	};
	e.unpack = [](const std::string &a, const std::string &d, const std::string &w, std::string &err) {
		return unpackArchive(a, d, w, err);
	};
	return e;
}

///////////////////////////////////////////////////////////////////////////////
// JSON

static void testJson() {
	Json v;
	std::string err;
	CHECK(jsonParse("{\"a\": [1, 2.5, true, null, \"x\\u0041\\n\"]}", v, err));
	CHECK(v.isObject());
	const Json *a = v.find("a");
	CHECK(a && a->isArray() && a->items.size() == 5);
	CHECK(a->items[0].isNumber() && a->items[0].number == 1);
	CHECK(a->items[1].number == 2.5);
	CHECK(a->items[2].type == Json::Type::Bool && a->items[2].boolean);
	CHECK(a->items[3].type == Json::Type::Null);
	CHECK_EQ_STR(a->items[4].text, "xA\n");

	CHECK(!jsonParse("{", v, err));      // truncated
	CHECK(!jsonParse("{} x", v, err));   // trailing garbage
	CHECK(!jsonParse("{\"a\":}", v, err));
	CHECK(!jsonParse("", v, err));
	CHECK(!jsonParse("{\"a\": \"\\q\"}", v, err));
	// A missing key is not an error for a lookup, it is simply absent.
	Json o;
	CHECK(jsonParse("{}", o, err));
	CHECK(o.find("nope") == NULL);
}

static void testManifestParsing() {
	CoreList list;
	std::string err;
	const std::string good =
		"{\"schema\": 1, \"platforms\": {\"h700\": {\"cores\": ["
		"{\"tag\": \"LIBRETRO_FLYCAST\", \"name\": \"flycast\", \"pin\": \"aa97a6d6\", "
		"\"file\": \"LIBRETRO_FLYCAST_h700.pak.tar.gz\", \"sha256\": \"" + std::string(64, 'a') +
		"\", \"size\": 12, \"folders\": [\"Sega Dreamcast\", \"Sega Naomi\"]}]}}}";
	CHECK(parseCoreList(good, "h700", list, err));
	CHECK(list.hasPlatform && list.cores.size() == 1);
	CHECK_EQ_STR(list.cores[0].tag, "LIBRETRO_FLYCAST");
	CHECK_EQ_STR(list.cores[0].pin, "aa97a6d6");
	CHECK(list.cores[0].size == 12);
	CHECK(list.cores[0].folders.size() == 2);
	CHECK(list.find("LIBRETRO_FLYCAST") != NULL);

	// Another platform is a valid release with nothing for us.
	CoreList other;
	CHECK(parseCoreList(good, "tg5040", other, err));
	CHECK(!other.hasPlatform && other.cores.empty());

	// Unknown schema: stop, the release format is newer than the installer.
	CoreList newer;
	CHECK(!parseCoreList("{\"schema\": 2, \"platforms\": {}}", "h700", newer, err));
	CHECK(err.find("schema") != std::string::npos);

	// Hostile or simply wrong entries are rejected instead of being trusted.
	CoreList r;
	err.clear();
	CHECK(!parseCoreList("{\"schema\": 1, \"platforms\": {\"h700\": {\"cores\": [{\"tag\": \"EVIL\", "
	                    "\"name\": \"x\", \"pin\": \"p\", \"file\": \"f\", \"sha256\": \"" + std::string(64, 'a') +
	                    "\"}]}}}", "h700", r, err));
	err.clear();
	CHECK(!parseCoreList("{\"schema\": 1, \"platforms\": {\"h700\": {\"cores\": [{\"tag\": \"LIBRETRO_A\", "
	                    "\"name\": \"x\", \"pin\": \"p\", \"file\": \"../../evil\", \"sha256\": \"" + std::string(64, 'a') +
	                    "\"}]}}}", "h700", r, err));
	err.clear();
	CHECK(!parseCoreList("{\"schema\": 1, \"platforms\": {\"h700\": {\"cores\": [{\"tag\": \"LIBRETRO_A\", "
	                    "\"name\": \"x\", \"pin\": \"p\", \"file\": \"a.tgz\", \"sha256\": \"short\"}]}}}",
	                     "h700", r, err));
	err.clear();
	CHECK(!parseCoreList("{\"schema\": 1, \"platforms\": {\"h700\": {\"cores\": [{\"tag\": \"LIBRETRO_A\", "
	                    "\"name\": \"x\", \"pin\": \"p\", \"file\": \"a.tgz\", \"sha256\": \"" + std::string(64, 'a') +
	                    "\"}, {\"tag\": \"LIBRETRO_A\", \"name\": \"y\", \"pin\": \"p\", \"file\": \"b.tgz\", "
	                    "\"sha256\": \"" + std::string(64, 'a') + "\"}]}}}", "h700", r, err));
	CHECK(err.find("duplicate") != std::string::npos);

	// Asset/tag/folder name rules.
	CHECK(safeAssetName("LIBRETRO_FLYCAST_h700.pak.tar.gz"));
	CHECK(!safeAssetName("../x"));
	CHECK(!safeAssetName("a/b"));
	CHECK(!safeAssetName(""));
	CHECK(safeTag("LIBRETRO_FLYCAST"));
	CHECK(!safeTag("LIBRETRO_"));
	CHECK(!safeTag("libretro_flycast"));
	CHECK(!safeTag("LIBRETRO_A/B"));
	CHECK(safeFolderName("Sega Dreamcast"));
	CHECK(!safeFolderName("../x"));
	CHECK(!safeFolderName("a/b"));

	// Archive member names never escape the destination.
	CHECK(safeArchiveName("./launch.sh"));
	CHECK(safeArchiveName("sub/dir/file"));
	CHECK(!safeArchiveName("/etc/passwd"));
	CHECK(!safeArchiveName("../x"));
	CHECK(!safeArchiveName("a/../../x"));
	CHECK(!safeArchiveName(""));
}

static void testSums() {
	const std::string text =
		std::string(64, 'a') + "  minarch-gl_h700.elf\n"
		+ std::string(64, 'b') + " *LIBRETRO_FLYCAST_h700.pak.tar.gz\n"
		+ "not a checksum line\n"
		+ std::string(64, 'c') + "  core-list.json\r\n";
	const std::vector<std::pair<std::string, std::string>> sums = parseSums(text);
	CHECK(sums.size() == 3);
	CHECK_EQ_STR(sumFor(sums, "minarch-gl_h700.elf"), std::string(64, 'a'));
	CHECK_EQ_STR(sumFor(sums, "LIBRETRO_FLYCAST_h700.pak.tar.gz"), std::string(64, 'b'));
	CHECK_EQ_STR(sumFor(sums, "core-list.json"), std::string(64, 'c'));
	CHECK_EQ_STR(sumFor(sums, "absent"), "");
}

///////////////////////////////////////////////////////////////////////////////
// manifest acquisition

static void testManifestSources(const std::string &root) {
	Paths p;
	p.sdcard = root + "/card";
	p.platform = "h700";

	Release rel(root + "/release");
	rel.corePak("LIBRETRO_FLYCAST", "flycast", "aa97a6d6", "flycast_libretro.so");
	writeStr(rel.dir + "/minarch-gl_h700.elf", GL_ELF);
	rel.assets.push_back("minarch-gl_h700.elf");
	writeManifest(rel, "h700", entryJson(rel, "LIBRETRO_FLYCAST", "flycast", "aa97a6d6",
	                                    "LIBRETRO_FLYCAST_h700.pak.tar.gz", {"Sega Dreamcast"}));
	const Env env = fakeEnv(rel.dir);

	// Nothing anywhere: the UI gets an explanation, not a crash.
	Manifest none = loadManifest(p, env, false);
	CHECK(!none.ok);
	CHECK_EQ_STR(none.err, "No network and no local copy.");

	// Online: fetched, verified, cached for the next offline start.
	Manifest online = loadManifest(p, env, true);
	CHECK(online.ok);
	CHECK(online.source == ManifestSource::Online);
	CHECK_EQ_STR(online.latestGl, sha256Hex(rel.dir + "/minarch-gl_h700.elf"));
	CHECK(online.list.cores.size() == 1);
	CHECK(fileExists(p.coreListPath()));
	CHECK(fileExists(p.sumsPath()));

	// Offline with a cache: still shows what is installed and what is missing.
	Manifest cached = loadManifest(p, env, false);
	CHECK(cached.ok);
	CHECK(cached.source == ManifestSource::Cached);
	CHECK_EQ_STR(cached.latestGl, online.latestGl);

	// A cached manifest that no longer matches its checksums is not used.
	writeStr(p.coreListPath(), "{\"schema\": 1, \"platforms\": {}}");
	Manifest stale = loadManifest(p, env, false);
	CHECK(!stale.ok);

	// With the network on, the fresh copy wins over bundled/ (it is the only one
	// that can be newer); bundled/ is not overwritten by that check.
	mkdirs(p.bundledDir());
	writeStr(p.bundledDir() + "/core-list.json", "{\"schema\": 1, \"platforms\": {\"h700\": {\"cores\": []}}}");
	writeStr(p.bundledDir() + "/SHA256SUMS", sha256Hex(p.bundledDir() + "/core-list.json") + "  core-list.json\n");
	Manifest fresh = loadManifest(p, env, true);
	CHECK(fresh.ok);
	CHECK(fresh.source == ManifestSource::Online);
	CHECK(fresh.list.cores.size() == 1);

	// With online updates off the network is refused instantly and bundled/ is
	// what the tool works from.
	Manifest bundled = loadManifest(p, fakeEnv(rel.dir, false), true);
	CHECK(bundled.ok);
	CHECK(bundled.source == ManifestSource::Bundled);
	CHECK(bundled.list.cores.empty());

	// bundled/ without a checksum table is not a usable release; the last good
	// download cached under .userdata is used instead.
	unlink((p.bundledDir() + "/SHA256SUMS").c_str());
	Manifest noSums = loadManifest(p, env, false);
	CHECK(noSums.ok);
	CHECK(noSums.source == ManifestSource::Cached);

	// A downloaded manifest whose bytes do not match the sums is rejected and
	// must not replace the cache.
	Paths q;
	q.sdcard = root + "/card2";
	q.platform = "h700";
	Release broken(root + "/release-broken");
	writeStr(broken.dir + "/minarch-gl_h700.elf", GL_ELF);
	writeManifest(broken, "h700", "", "minarch-gl_h700.elf");
	writeStr(broken.dir + "/core-list.json", "{\"schema\": 1, \"platforms\": {}}"); // after the sums
	Env brokenEnv = fakeEnv(broken.dir);
	Manifest bad = loadManifest(q, brokenEnv, true);
	CHECK(!bad.ok);

	// A release that publishes nothing (GitHub 404): say that, instead of
	// claiming there is no network.
	Paths nf;
	nf.sdcard = root + "/card-404";
	nf.platform = "h700";
	Release empty(root + "/release-empty");
	Manifest notFound = loadManifest(nf, fakeEnv(empty.dir), true);
	CHECK(!notFound.ok);
	CHECK_EQ_STR(notFound.err, "No release has been published yet.");

	// Unknown platform section: a valid release, no cores for this card.
	Paths z;
	z.sdcard = root + "/card3";
	z.platform = "tg5040";
	Release other(root + "/release-other");
	writeManifest(other, "h700", "");
	Manifest unknownPlatform = loadManifest(z, fakeEnv(other.dir), true);
	CHECK(unknownPlatform.ok);
	CHECK(unknownPlatform.list.hasPlatform == false);
}

///////////////////////////////////////////////////////////////////////////////
// assets

static void testAssets(const std::string &root) {
	Paths p;
	p.sdcard = root + "/card-assets";
	p.platform = "h700";
	mkdirs(p.stateDir());

	Release rel(root + "/release-assets");
	const std::string file = rel.corePak("LIBRETRO_FLYCAST", "flycast", "aa97a6d6", "flycast_libretro.so");
	const std::string sum = sha256Hex(rel.dir + "/" + file);
	struct stat st;
	stat((rel.dir + "/" + file).c_str(), &st);
	const long long size = (long long)st.st_size;
	const Env env = fakeEnv(rel.dir);

	Asset a;
	std::string err;
	CHECK(acquireAsset(p, env, file, sum, size, Progress(), "flycast", a, err));
	CHECK(a.downloaded);
	CHECK_EQ_STR(a.path, p.dlDir() + "/" + file);

	// Second time it comes from dl/ without touching the network.
	Env noNetwork;
	noNetwork.baseUrl = env.baseUrl;
	Asset again;
	CHECK(acquireAsset(p, noNetwork, file, sum, size, Progress(), "flycast", again, err));
	CHECK(!again.downloaded);

	// bundled/ is preferred over the download.
	mkdirs(p.bundledDir());
	CHECK(copyFile(a.path, p.bundledDir() + "/" + file));
	Asset fromBundle;
	CHECK(acquireAsset(p, noNetwork, file, sum, size, Progress(), "flycast", fromBundle, err));
	CHECK_EQ_STR(fromBundle.path, p.bundledDir() + "/" + file);

	// A wrong checksum is refused, and nothing is left behind.
	Asset nope;
	CHECK(!acquireAsset(p, env, file, std::string(64, 'b'), size, Progress(), "flycast", nope, err));
	CHECK(err.find("checksum") != std::string::npos);
	CHECK(!fileExists(p.dlDir() + "/." + file + ".part"));

	// A truncated download is refused instead of being installed.  (A clean card:
	// the copies above would otherwise satisfy the request.)
	Paths fresh;
	fresh.sdcard = root + "/card-assets-fresh";
	fresh.platform = "h700";
	Release truncated(root + "/release-truncated");
	mkdirs(truncated.dir);
	writeStr(truncated.dir + "/" + file, "short");
	Asset bad;
	CHECK(!acquireAsset(fresh, fakeEnv(truncated.dir), file, sum, size, Progress(), "flycast", bad, err));
	CHECK(!fileExists(fresh.dlDir() + "/." + file + ".part"));
	CHECK(!fileExists(fresh.dlDir() + "/" + file));
}

///////////////////////////////////////////////////////////////////////////////
// frontend states and actions

static void testFrontendStates(const std::string &root) {
	Paths p;
	p.sdcard = root + "/card-frontend";
	p.platform = "h700";
	mkdirs(p.binDir());

	FrontendInfo f = frontendInfo(p, "");
	CHECK(f.state == FrontendState::NeedsRepair);   // no frontend at all
	CHECK_EQ_STR(f.reason, "The NextUI frontend is missing.");

	writeStr(p.minarchPath(), STOCK_ELF);
	f = frontendInfo(p, "");
	CHECK(f.state == FrontendState::NotInstalled);
	CHECK_EQ_STR(f.reason, "MinarchGL is not set up.");

	// Install: the running stock binary is saved, our copy is added, and the
	// running binary stays stock (Install does not enable).
	mkdirs(p.stateDir());
	writeStr(p.stateDir() + "/frontend.elf", GL_ELF);
	Result r = installFrontend(p, p.stateDir() + "/frontend.elf", "", Progress());
	CHECK(r.ok);
	CHECK_EQ_STR(readAll(p.stockPath()), STOCK_ELF);
	CHECK_EQ_STR(readAll(p.glPath()), GL_ELF);
	CHECK_EQ_STR(readAll(p.minarchPath()), STOCK_ELF);
	f = frontendInfo(p, "");
	CHECK(f.state == FrontendState::Disabled);
	CHECK_EQ_STR(f.stock, sha256Hex(p.stockPath()));
	CHECK_EQ_STR(f.gl, sha256Hex(p.glPath()));

	// Enable / disable are byte copies of the two backups.
	CHECK(enableFrontend(p).ok);
	CHECK_EQ_STR(readAll(p.minarchPath()), GL_ELF);
	CHECK(frontendInfo(p, "").state == FrontendState::Enabled);
	CHECK(disableFrontend(p).ok);
	CHECK_EQ_STR(readAll(p.minarchPath()), STOCK_ELF);
	CHECK(frontendInfo(p, "").state == FrontendState::Disabled);

	// Update while enabled replaces both copies, and keeps the stock backup.
	CHECK(enableFrontend(p).ok);
	writeStr(p.stateDir() + "/frontend2.elf", GL2_ELF);
	const std::string latest = sha256Hex(p.stateDir() + "/frontend2.elf");
	r = installFrontend(p, p.stateDir() + "/frontend2.elf", latest, Progress());
	CHECK(r.ok);
	CHECK_EQ_STR(readAll(p.glPath()), GL2_ELF);
	CHECK_EQ_STR(readAll(p.minarchPath()), GL2_ELF);
	CHECK_EQ_STR(readAll(p.stockPath()), STOCK_ELF);
	f = frontendInfo(p, latest);
	CHECK(f.state == FrontendState::Enabled);

	// Re-running the same update is harmless.
	r = installFrontend(p, p.stateDir() + "/frontend2.elf", latest, Progress());
	CHECK(r.ok);
	CHECK_EQ_STR(readAll(p.stockPath()), STOCK_ELF);

	// NextUI update: .system is replaced under us.  The new stock binary must be
	// what gets saved on the next repair, not an older one.
	writeStr(p.minarchPath(), STOCK2_ELF);
	f = frontendInfo(p, latest);
	CHECK(f.state == FrontendState::NeedsRepair);
	CHECK_EQ_STR(f.reason, "The frontend was replaced by a NextUI update.");
	r = installFrontend(p, p.stateDir() + "/frontend2.elf", latest, Progress());
	CHECK(r.ok);
	CHECK_EQ_STR(readAll(p.stockPath()), STOCK2_ELF);
	CHECK_EQ_STR(readAll(p.minarchPath()), STOCK2_ELF); // repair does not enable
	CHECK(frontendInfo(p, latest).state == FrontendState::Disabled);

	// Our build running while the stock copy is gone: nothing to save, and
	// recording our own binary as "stock" would be wrong.
	CHECK(enableFrontend(p).ok);
	unlink(p.stockPath().c_str());
	f = frontendInfo(p, latest);
	CHECK(f.state == FrontendState::StockLost);
	CHECK_EQ_STR(f.reason, "The stock frontend is gone - reinstall NextUI.");
	r = installFrontend(p, p.stateDir() + "/frontend2.elf", latest, Progress());
	CHECK(!r.ok);
	CHECK_EQ_STR(readAll(p.minarchPath()), GL2_ELF);      // still runnable
	CHECK(!fileExists(p.stockPath()));                    // and still not lied about

	// Same case with our copy deleted as well: only the published hash can tell
	// that the running binary is ours.
	removeTree(p.stateDir());
	f = frontendInfo(p, latest);
	CHECK(f.state == FrontendState::StockLost);
	// Without the published hash it cannot be told apart and reads as stock,
	// which is why a frontend action is only offered when SHA256SUMS is at hand.
	f = frontendInfo(p, "");
	CHECK(f.state == FrontendState::NotInstalled);

	// Uninstall with our build running restores stock first.
	writeStr(p.minarchPath(), STOCK2_ELF);
	mkdirs(p.stateDir());
	writeStr(p.stateDir() + "/f.elf", GL_ELF);
	r = installFrontend(p, p.stateDir() + "/f.elf", "", Progress());
	CHECK(r.ok);
	CHECK(enableFrontend(p).ok);
	r = uninstallFrontend(p);
	CHECK(r.ok);
	CHECK_EQ_STR(readAll(p.minarchPath()), STOCK2_ELF);
	CHECK(!dirExists(p.stateDir()));

	// Uninstall refuses when the stock copy is gone (nothing to restore).
	writeStr(p.minarchPath(), STOCK2_ELF);
	mkdirs(p.stateDir());
	writeStr(p.glPath(), GL_ELF);
	CHECK(enableFrontend(p).ok);
	r = uninstallFrontend(p);
	CHECK(!r.ok);
	CHECK_EQ_STR(readAll(p.minarchPath()), GL_ELF);

	// A missing frontend with a saved stock copy reads as "not installed" (there
	// is no copy of our build anywhere), and Install puts both back: the stock
	// binary into .system/bin, ours next to it.
	unlink(p.minarchPath().c_str());
	writeStr(p.stockPath(), STOCK_ELF);
	unlink(p.glPath().c_str());
	f = frontendInfo(p, "");
	CHECK(f.state == FrontendState::NotInstalled);
	writeStr(p.stateDir() + "/f2.elf", GL_ELF);
	r = installFrontend(p, p.stateDir() + "/f2.elf", "", Progress());
	CHECK(r.ok);
	CHECK_EQ_STR(readAll(p.minarchPath()), STOCK_ELF);
	CHECK(frontendInfo(p, "").state == FrontendState::Disabled);
}

///////////////////////////////////////////////////////////////////////////////
// core paks

static void testCores(const std::string &root) {
	Paths p;
	p.sdcard = root + "/card-cores";
	p.platform = "h700";

	Release rel(root + "/release-cores");
	const std::string file = rel.corePak("LIBRETRO_FLYCAST", "flycast", "aa97a6d6", "flycast_libretro.so");
	const Env env = fakeEnv(rel.dir);

	CoreEntry e;
	e.tag = "LIBRETRO_FLYCAST";
	e.name = "flycast";
	e.pin = "aa97a6d6";
	e.file = file;
	e.sha256 = sha256Hex(rel.dir + "/" + file);
	struct stat st;
	stat((rel.dir + "/" + file).c_str(), &st);
	e.size = (long long)st.st_size;
	e.folders = {"Sega Dreamcast", "Sega Naomi"};

	CHECK(coreStatus(p, e).state == CoreState::NotInstalled);

	Result r = installCore(p, env, e, Progress());
	CHECK(r.ok);
	CHECK(dirExists(p.corePakDir(e.tag)));
	CHECK(fileExists(p.corePakDir(e.tag) + "/launch.sh"));
	CHECK(fileExists(p.corePakDir(e.tag) + "/flycast_libretro.so"));
	CHECK(!dirExists(p.coreTmpDir(e.tag)));
	CHECK(!dirExists(p.coreBakDir(e.tag)));
	CHECK_EQ_STR(coreInstalledPin(p, e.tag), "aa97a6d6");
	CoreStatus s = coreStatus(p, e);
	CHECK(s.state == CoreState::Current);
	CHECK_EQ_STR(s.pin, "aa97a6d6");

	// A pak whose version file says something else needs an update; so does one
	// with no version file at all (an early or hand-made pak).
	e.pin = "deadbeef";
	CHECK(coreStatus(p, e).state == CoreState::NotLatest);
	unlink((p.corePakDir("LIBRETRO_FLYCAST") + "/version").c_str());
	CHECK(coreStatus(p, e).state == CoreState::NotLatest);

	// Update replaces the whole pak.
	Release rel2(root + "/release-cores2");
	const std::string file2 = rel2.corePak("LIBRETRO_FLYCAST", "flycast", "deadbeef", "flycast_libretro.so");
	writeStr(rel2.dir + "/extra.txt", "x");
	CoreEntry e2 = e;
	e2.file = file2;
	e2.sha256 = sha256Hex(rel2.dir + "/" + file2);
	stat((rel2.dir + "/" + file2).c_str(), &st);
	e2.size = (long long)st.st_size;
	r = installCore(p, fakeEnv(rel2.dir), e2, Progress());
	CHECK(r.ok);
	CHECK_EQ_STR(coreInstalledPin(p, e2.tag), "deadbeef");
	CHECK(coreStatus(p, e2).state == CoreState::Current);

	// A failed download leaves the working pak alone.
	CoreEntry broken = e2;
	broken.sha256 = std::string(64, 'f');
	CHECK(!installCore(p, env, broken, Progress()).ok);
	CHECK_EQ_STR(coreInstalledPin(p, e2.tag), "deadbeef");
	CHECK(fileExists(p.corePakDir(e2.tag) + "/flycast_libretro.so"));

	// Uninstall only accepts our tag namespace and is idempotent.
	CHECK(!removeCore(p, "Sega Dreamcast").ok);
	CHECK(!removeCore(p, "LIBRETRO__evil").ok);
	CHECK(removeCore(p, e2.tag).ok);
	CHECK(!dirExists(p.corePakDir(e2.tag)));
	CHECK(removeCore(p, e2.tag).ok);

	// Ownership is by name: another pak in Emus/ is never touched.
	mkdirs(p.emusDir() + "/DC.pak");
	writeStr(p.emusDir() + "/DC.pak/launch.sh", "#!/bin/sh\n");
	CHECK(removeCore(p, e2.tag).ok);
	CHECK(fileExists(p.emusDir() + "/DC.pak/launch.sh"));

	// Default folders: the BIOS directory (and its subdirs) plus the ROM folders,
	// whose names carry the tag NextUI reads.
	mkdirs(p.romsDir());
	CHECK(romFoldersForTag(p, e.tag).empty());
	CHECK(!dirExists(p.biosDir(e.tag)));
	CHECK(createCoreFolders(p, e).ok);
	CHECK(dirExists(p.biosDir(e.tag)));
	CHECK_EQ_STR(p.biosDir(e.tag), p.sdcard + "/Bios/" + e.tag);  // not per platform
	CHECK(dirExists(p.romsDir() + "/Sega Dreamcast (LIBRETRO_FLYCAST)"));
	CHECK(dirExists(p.romsDir() + "/Sega Naomi (LIBRETRO_FLYCAST)"));
	CHECK(romFoldersForTag(p, e.tag).size() == 2);
	CHECK_EQ_STR(romFoldersForTag(p, e.tag)[0], "Sega Dreamcast (LIBRETRO_FLYCAST)");
	CHECK(createCoreFolders(p, e).ok);   // idempotent
	CHECK_EQ_STR(folderTag("Dreamcast (LIBRETRO_FLYCAST)"), "LIBRETRO_FLYCAST");
	CHECK_EQ_STR(folderTag("Dreamcast"), "");
	mkdirs(p.romsDir() + "/Sega Saturn (SATURN)");   // someone else's folder
	CHECK(romFoldersForTag(p, e.tag).size() == 2);
	CHECK(romFoldersForTag(p, "LIBRETRO_YABASANSHIRO").empty());

	// BIOS and ROM folders are created separately now (each has its own row on
	// the core page), so a deleted BIOS directory can be restored on its own.
	CHECK(deleteBiosFolder(p, e.tag).ok);
	CHECK(!dirExists(p.biosDir(e.tag)));
	CHECK(createBiosFolder(p, e.tag).ok);
	CHECK(dirExists(p.biosDir(e.tag)));
	CHECK(createBiosFolder(p, e.tag).ok);        // idempotent
	CHECK(createRomFolders(p, e).ok);            // idempotent too

	// Deleting a ROM folder takes its contents with it, and only for this tag.
	const std::string dc = p.romsDir() + "/Sega Dreamcast (LIBRETRO_FLYCAST)";
	writeStr(dc + "/game.gdi", "disc\n");
	mkdirs(dc + "/sub");
	writeStr(dc + "/sub/track01.bin", "data\n");
	CHECK(!deleteRomFolder(p, "LIBRETRO_YABASANSHIRO", "Sega Dreamcast (LIBRETRO_FLYCAST)").ok);
	CHECK(!deleteRomFolder(p, e.tag, "../h700").ok);
	CHECK(!deleteRomFolder(p, e.tag, "Sega Dreamcast").ok);   // no tag in the name
	CHECK(deleteRomFolder(p, e.tag, "Sega Dreamcast (LIBRETRO_FLYCAST)").ok);
	CHECK(!dirExists(dc));
	CHECK(!fileExists(dc + "/sub/track01.bin"));
	CHECK(romFoldersForTag(p, e.tag).size() == 1);
	CHECK(!deleteRomFolder(p, e.tag, "Sega Dreamcast (LIBRETRO_FLYCAST)").ok);  // already gone

	// A BIOS directory with files in it goes the same way.
	writeStr(p.biosDir(e.tag) + "/dc_boot.bin", "bios\n");
	CHECK(!deleteBiosFolder(p, "LIBRETRO__evil").ok);
	CHECK(deleteBiosFolder(p, e.tag).ok);
	CHECK(!dirExists(p.biosDir(e.tag)));
	CHECK(!fileExists(p.biosDir(e.tag) + "/dc_boot.bin"));

	// An archive that does not hold a pak is refused.
	Release junk(root + "/release-junk");
	mkdirs(junk.dir + "/.src");
	writeStr(junk.dir + "/.src/readme.txt", "not a pak\n");
	CHECK(run("tar -czf '" + junk.dir + "/junk.pak.tar.gz' -C '" + junk.dir + "/.src' ."));
	CoreEntry je;
	je.tag = "LIBRETRO_YABASANSHIRO";
	je.name = "yabasanshiro";
	je.pin = "x";
	je.file = "junk.pak.tar.gz";
	je.sha256 = sha256Hex(junk.dir + "/junk.pak.tar.gz");
	r = installCore(p, fakeEnv(junk.dir), je, Progress());
	CHECK(!r.ok);
	CHECK(!dirExists(p.corePakDir(je.tag)));
	CHECK(!dirExists(p.coreTmpDir(je.tag)));
}

/////////////////////////////////////////////////////////////////////////////////
// preferences and the offline policy

// The .pakz is a zip, and the device's unzip is busybox (no -Z, different -l
// format), so the member names are read out of the central directory here rather
// than parsed from a listing.  What has to hold: the names come back intact, and
// something that is not a zip is refused instead of half-parsed.
static bool makeZip(const std::string &dir, const std::string &out, const std::string &members) {
	const std::string cmd = "cd '" + dir + "' && { command -v zip >/dev/null 2>&1 && "
		"zip -qr '" + out + "' " + members + " || bsdtar --format=zip -cf '" + out + "' " + members + "; }";
	return run(cmd);
}

static void testZipMembers(const std::string &root) {
	const std::string dir = root + "/zip-test";
	mkdirs(dir + "/Tools/h700/X.pak");
	writeStr(dir + "/Tools/h700/X.pak/launch.sh", "#!/bin/sh\n");
	writeStr(dir + "/Tools/h700/X.pak/pak.json", "{\"version\": \"0.1.8\"}\n");
	writeStr(dir + "/post_install.sh", "#!/bin/sh\nexit 0\n");
	CHECK(makeZip(dir, "t.pakz", "Tools post_install.sh"));

	std::vector<std::string> names;
	std::string err;
	CHECK(zipMembers(dir + "/t.pakz", names, err));
	bool launch = false, pak = false, post = false;
	for (const std::string &n : names) {
		if (n == "Tools/h700/X.pak/launch.sh") launch = true;
		if (n == "Tools/h700/X.pak/pak.json") pak = true;
		if (n == "post_install.sh") post = true;
	}
	CHECK(launch);
	CHECK(pak);
	CHECK(post);

	// Not a zip: rejected with an error, and the name list is left alone.
	writeStr(dir + "/notzip", "hello");
	std::vector<std::string> none;
	CHECK(!zipMembers(dir + "/notzip", none, err));
	CHECK(!err.empty());
	CHECK(none.empty());
}

// The tool updates itself inside its own pak (the directory must not move: it is
// the launch loop's working directory), so the thing to test is that a verified
// payload replaces the files and that nothing else -- a bad checksum, a tree
// without a pak, an interrupted unpack -- can damage the installed copy.
static void testInstallerUpdate(const std::string &root) {
	CHECK(compareVersions("0.1.7", "0.1.8") < 0);
	CHECK(compareVersions("0.1.10", "0.1.9") > 0);
	CHECK(compareVersions("0.2", "0.2.0") == 0);
	CHECK(compareVersions("0.1.7", "0.1.7") == 0);

	Paths p;
	p.sdcard = root + "/card-self";
	p.platform = "h700";
	p.selfPakDir = p.sdcard + "/Tools/h700/MinarchGL Installer.pak";
	mkdirs(p.selfPakDir);
	writeStr(p.selfPakDir + "/launch.sh", "#!/bin/sh\n");
	writeStr(p.selfPakDir + "/pak.json", "{\"version\": \"0.1.7\"}\n");
	writeStr(p.selfPakDir + "/minarch-gl-installer.elf", std::string("\x7f" "ELF old-installer\n"));
	writeStr(p.selfPakDir + "/LICENSE", "old licence\n");
	CHECK_EQ_STR(readPakVersion(p.selfPakDir), "0.1.7");

	// The release publishes the .pakz; its bytes only have to match the checksum
	// here, because the extractor is injected below.
	Release rel(root + "/release-self");
	writeStr(rel.dir + "/MinarchGL.Installer.pakz", std::string("pakz-bytes"));
	const std::string pakzSha = sha256Hex(rel.dir + "/MinarchGL.Installer.pakz");
	std::vector<std::pair<std::string, std::string>> sums = {
		{"MinarchGL.Installer.pakz", pakzSha},
		{"minarch-gl_h700.elf", std::string(64, 'a')},
	};

	const std::string stagedTree = root + "/staged-self";
	Env env = fakeEnv(rel.dir);
	env.unpackZip = [stagedTree](const std::string &, const std::string &dest,
	                             const std::string &, std::string &) {
		mkdirs(dest);
		const std::string pak = dest + "/Tools/h700/MinarchGL Installer.pak";
		mkdirs(pak);
		writeStr(pak + "/launch.sh", "#!/bin/sh\n# v2\n");
		writeStr(pak + "/pak.json", "{\"version\": \"0.1.8\"}\n");
		writeStr(pak + "/minarch-gl-installer.elf", std::string("\x7f" "ELF new-installer\n"));
		writeStr(pak + "/NOTICE", "notice v2\n");
		return true;
	};

	// Same version: nothing is written, and the release is not "upgraded" onto
	// itself.
	env.unpackZip = [stagedTree](const std::string &, const std::string &dest,
	                             const std::string &, std::string &) {
		mkdirs(dest + "/Tools/h700/MinarchGL Installer.pak");
		writeStr(dest + "/Tools/h700/MinarchGL Installer.pak/pak.json", "{\"version\": \"0.1.7\"}\n");
		return true;
	};
	InstallerUpdate u = updateInstaller(p, env, sums, Progress());
	CHECK(u.ok);
	CHECK(!u.changed);
	CHECK_EQ_STR(u.from, "0.1.7");
	CHECK(!dirExists(p.selfPakDir + "/.update"));

	// A checksum that does not match the file never reaches the pak.
	std::vector<std::pair<std::string, std::string>> badSums = sums;
	badSums[0].second = std::string(64, 'b');
	u = updateInstaller(p, env, badSums, Progress());
	CHECK(!u.ok);
	CHECK(!u.err.empty());

	// A tree without pak.json is refused.
	env.unpackZip = [](const std::string &, const std::string &dest, const std::string &, std::string &) {
		mkdirs(dest + "/Tools/h700/whatever");
		writeStr(dest + "/Tools/h700/whatever/launch.sh", "#!/bin/sh\n");
		return true;
	};
	u = updateInstaller(p, env, sums, Progress());
	CHECK(!u.ok);
	CHECK(!u.err.empty());

	// A release that does not publish the pakz is reported, not guessed at.
	std::vector<std::pair<std::string, std::string>> noPakz = {{"minarch-gl_h700.elf", std::string(64, 'a')}};
	u = updateInstaller(p, env, noPakz, Progress());
	CHECK(!u.ok);
	CHECK(!u.err.empty());

	CHECK_EQ_STR(readPakVersion(p.selfPakDir), "0.1.7");
	CHECK_EQ_STR(readFile(p.selfPakDir + "/minarch-gl-installer.elf", 4096),
	             std::string("\x7f" "ELF old-installer\n"));

	// A newer payload: every file is replaced, new files appear, and the elf
	// ends up being the new one.
	env = fakeEnv(rel.dir);
	env.unpackZip = [](const std::string &, const std::string &dest, const std::string &, std::string &) {
		mkdirs(dest);
		const std::string pak = dest + "/Tools/h700/MinarchGL Installer.pak";
		mkdirs(pak);
		writeStr(pak + "/launch.sh", "#!/bin/sh\n# v2\n");
		writeStr(pak + "/pak.json", "{\"version\": \"0.1.8\"}\n");
		writeStr(pak + "/minarch-gl-installer.elf", std::string("\x7f" "ELF new-installer\n"));
		writeStr(pak + "/NOTICE", "notice v2\n");
		return true;
	};
	u = updateInstaller(p, env, sums, Progress());
	CHECK(u.ok);
	CHECK(u.changed);
	CHECK_EQ_STR(u.from, "0.1.7");
	CHECK_EQ_STR(u.to, "0.1.8");
	CHECK_EQ_STR(readPakVersion(p.selfPakDir), "0.1.8");
	CHECK_EQ_STR(readFile(p.selfPakDir + "/minarch-gl-installer.elf", 4096),
	             std::string("\x7f" "ELF new-installer\n"));
	CHECK(fileExists(p.selfPakDir + "/NOTICE"));
	CHECK(fileExists(p.selfPakDir + "/LICENSE"));   // untouched files stay
	CHECK(!dirExists(p.selfPakDir + "/.update"));
	struct stat st;
	CHECK(stat((p.selfPakDir + "/minarch-gl-installer.elf").c_str(), &st) == 0 && (st.st_mode & 0111));

	// An elf that is not an ELF is refused before anything is unlinked.
	writeStr(p.selfPakDir + "/pak.json", "{\"version\": \"0.1.7\"}\n");
	env.unpackZip = [](const std::string &, const std::string &dest, const std::string &, std::string &) {
		mkdirs(dest + "/Tools/h700/MinarchGL Installer.pak");
		const std::string pak = dest + "/Tools/h700/MinarchGL Installer.pak";
		writeStr(pak + "/pak.json", "{\"version\": \"0.1.9\"}\n");
		writeStr(pak + "/minarch-gl-installer.elf", "not an elf\n");
		return true;
	};
	u = updateInstaller(p, env, sums, Progress());
	CHECK(!u.ok);
	CHECK_EQ_STR(readPakVersion(p.selfPakDir), "0.1.7");
	CHECK_EQ_STR(readFile(p.selfPakDir + "/minarch-gl-installer.elf", 4096),
	             std::string("\x7f" "ELF new-installer\n"));

	// The stale tree of a failed attempt is removed at the next start.
	mkdirs(p.selfPakDir + "/.update");
	writeStr(p.selfPakDir + "/.update/leftover", "x");
	cleanupInstallerStaging(p);
	CHECK(!dirExists(p.selfPakDir + "/.update"));
}

static void testPrefs(const std::string &root) {
	Paths p;
	p.sdcard = root + "/card-prefs";
	p.platform = "h700";

	// Never chosen: online, exactly like a card that predates the setting.
	CHECK(!fileExists(p.prefsPath()));
	CHECK(onlineUpdatesEnabled(p));

	// A choice is written atomically and read back.
	CHECK(setOnlineUpdates(p, false));
	CHECK(fileExists(p.prefsPath()));
	CHECK(!onlineUpdatesEnabled(p));
	CHECK(setOnlineUpdates(p, true));
	CHECK(onlineUpdatesEnabled(p));

	// Unknown keys survive a rewrite: a newer build's prefs are not destroyed.
	writeStr(p.prefsPath(), "theme=dark\nonline=0\nsomething=1\n");
	CHECK(!onlineUpdatesEnabled(p));
	CHECK(setOnlineUpdates(p, true));
	const std::string after = readAll(p.prefsPath());
	CHECK(after.find("theme=dark") != std::string::npos);
	CHECK(after.find("something=1") != std::string::npos);
	CHECK(onlineUpdatesEnabled(p));

	// Malformed or missing values fall back to on, not to a broken state.
	writeStr(p.prefsPath(), "online=banana\n");
	CHECK(onlineUpdatesEnabled(p));
	writeStr(p.prefsPath(), "");
	CHECK(onlineUpdatesEnabled(p));

	// A frontend uninstall drops the state directory but keeps the preference
	// (and nothing else): the setup page has to remember the choice.
	CHECK(setOnlineUpdates(p, false));
	mkdirs(p.stateDir());
	writeStr(p.stateDir() + "/junk", "x");
	CHECK(uninstallFrontend(p).ok);
	CHECK(!fileExists(p.stateDir() + "/junk"));
	CHECK(!onlineUpdatesEnabled(p));
}

static void testOfflinePolicy(const std::string &root) {
	// The scheme helpers decide what counts as leaving the card.
	CHECK(isRemoteUrl("https://github.com/x"));
	CHECK(isRemoteUrl("http://example.invalid/x"));
	CHECK(!isRemoteUrl("file:///mnt/SDCARD/x"));
	CHECK(isLocalUrl("file:///mnt/SDCARD/x"));
	CHECK(!isLocalUrl("https://github.com/x"));

	// defaultEnv(false) refuses remote URLs before curl runs (so this test makes
	// no network request at all), and the error says why.
	Env off = defaultEnv(false);
	std::string err;
	const std::string dest = root + "/prefs-dest";
	CHECK(!off.fetch("https://example.invalid/core-list.json", dest, 1, err));
	CHECK_EQ_STR(err, "online updates are off");
	CHECK(!fileExists(dest));
	// ... while the same call is allowed when online is on (fake by checking the
	// scheme gate only: the URL is never dialled in this test).

	// assetReachable: online accepts anything; offline only what is on the card
	// or behind a file:// base URL.
	Paths p;
	p.sdcard = root + "/card-assets-reach";
	p.platform = "h700";
	Env online = defaultEnv(true);
	CHECK(assetReachable(online, p, "minarch-gl_h700.elf", std::string(64, 'a')));
	Env offline = defaultEnv(false);
	CHECK(!assetReachable(offline, p, "minarch-gl_h700.elf", std::string(64, 'a')));
	Env local = defaultEnv(false);
	local.baseUrl = "file:///mnt/SDCARD/MinarchGL.Pack";
	CHECK(assetReachable(local, p, "minarch-gl_h700.elf", std::string(64, 'a')));
	mkdirs(p.dlDir());
	writeStr(p.dlDir() + "/minarch-gl_h700.elf", GL_ELF);
	const std::string sha = sha256Hex(p.dlDir() + "/minarch-gl_h700.elf");
	CHECK(assetReachable(offline, p, "minarch-gl_h700.elf", sha));
	CHECK(!assetReachable(offline, p, "minarch-gl_h700.elf", std::string(64, 'a')));
}

///////////////////////////////////////////////////////////////////////////////

int main() {
	const std::string root = mktempRoot();

	testJson();
	testManifestParsing();
	testSums();
	testManifestSources(root);
	testPrefs(root);
	testOfflinePolicy(root);
	testAssets(root);
	testFrontendStates(root);
	testCores(root);
	testZipMembers(root);
	testInstallerUpdate(root);

	removeTree(root);
	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("installer-core: all checks passed\n");
	return 0;
}
