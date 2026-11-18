// manifest-check -- verify a release directory the way the installer sees it.
//
//   build/host/manifest-check <platform> <core-list.json> <asset dir>
//
// Reads the manifest with the installer's own parser and checks that every core
// entry points at a file that exists, has the published size and matches the
// published sha256.  tools/release.sh runs it right after writing the manifest,
// so a release whose table and files disagree is caught before it is published.
//
// Part of `make test`: the host test suite builds it, and tools/test.sh runs it
// against dist/core-list.json when a release has been packaged.

#include "installer-core.hpp"

#include <cstdio>
#include <string>

int main(int argc, char **argv) {
	if (argc != 4) {
		fprintf(stderr, "usage: %s <platform> <core-list.json> <asset dir>\n", argv[0]);
		return 2;
	}
	const std::string platform = argv[1];
	const std::string manifestPath = argv[2];
	const std::string assetDir = argv[3];

	std::string err;
	mgl::CoreList list;
	if (!mgl::parseCoreList(mgl::readFile(manifestPath), platform, list, err)) {
		fprintf(stderr, "manifest-check: %s: %s\n", manifestPath.c_str(), err.c_str());
		return 1;
	}
	if (!list.hasPlatform) {
		fprintf(stderr, "manifest-check: %s has no section for %s\n", manifestPath.c_str(), platform.c_str());
		return 1;
	}

	int bad = 0;
	printf("manifest-check: schema %d, %d core(s) for %s\n",
		list.schema, (int)list.cores.size(), platform.c_str());
	for (const mgl::CoreEntry &e : list.cores) {
		const std::string path = assetDir + "/" + e.file;
		struct stat st;
		if (stat(path.c_str(), &st) != 0) {
			fprintf(stderr, "  %-22s MISSING %s\n", e.name.c_str(), path.c_str());
			bad++;
			continue;
		}
		const std::string sum = mgl::sha256Hex(path);
		if (sum != e.sha256) {
			fprintf(stderr, "  %-22s checksum mismatch\n", e.name.c_str());
			bad++;
		} else if (e.size > 0 && (long long)st.st_size != e.size) {
			fprintf(stderr, "  %-22s size %lld != %lld\n", e.name.c_str(),
				(long long)st.st_size, e.size);
			bad++;
		} else {
			printf("  %-22s pin %-8s %8lld B  %s\n", e.name.c_str(), e.pin.c_str(),
				(long long)st.st_size, sum.substr(0, 12).c_str());
		}
	}
	if (bad) {
		fprintf(stderr, "manifest-check: %d problem(s)\n", bad);
		return 1;
	}
	return 0;
}
