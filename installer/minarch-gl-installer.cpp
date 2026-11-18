// minarch-gl-installer -- install, update and remove MinarchGL and its core paks.
//
// The tool has no payload of its own: everything it installs comes from this
// project's releases (see .docs/HANDOVER-20261118.md §5).  Its screens are
// therefore decided by two facts it computes on the spot -- what is on the card
// and what the latest release offers:
//
//   install page  nothing installed yet, or the card needs a repair
//   main page     installed: enable/disable, update, status, uninstall
//   status page   what the card has, and one row per core
//   core page     install/update/uninstall one core, and its ROM folders
//   done page     after an uninstall; only B leaves it, so a repeating Y cannot
//                 land on the "install" row that appears underneath
//
// Navigation is deliberately split: A only navigates (and never changes the
// card), Y performs every action that writes to it, B goes back -- or, on a
// root screen, leaves the tool.
//
// All file-level decisions live in installer-core.hpp, which has no SDL
// dependency and is covered by tests/installer-core-test.cpp on the build host.
// This file only chooses when to call it, and shows the result.
//
// CLI (no UI; for automation, device smoke tests and debugging):
//   --status                  print the derived state and the manifest source
//   --fetch                   fetch the manifest now (network) and print it
//   --install                 install/update the frontend from the release
//   --enable | --disable      switch .system/bin/minarch.elf
//   --uninstall               remove the frontend (restores stock when enabled)
//   --core-install <TAG>      install or update one core pak
//   --core-remove <TAG>       remove one core pak
//   --post-update             the no-UI update the .pakz post_install.sh runs
//   --page <name>             open a screen directly
//                             (install|main|status|core|done|message)
//                             for screenshots and layout checks on the device

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

extern "C" {
#include <msettings.h>
#include "defines.h"
#include "api.h"
}

#include "installer-core.hpp"

// List geometry, from the menu framework (settings/menu.hpp): rows are
// BUTTON_SIZE tall and the label sits OPTION_PADDING inside the row.  Repeating
// the two numbers here is the price of not compiling that framework, and they
// are what makes these rows line up with every other NextUI list.
#ifndef OPTION_PADDING
#define OPTION_PADDING 8
#endif

///////////////////////////////////////////////////////////////////////////////
// paths

static mgl::Paths makePaths() {
	mgl::Paths p;
	p.sdcard = SDCARD_PATH;
	p.platform = PLATFORM;
	return p;
}

///////////////////////////////////////////////////////////////////////////////
// plain text output

///////////////////////////////////////////////////////////////////////////////
// CLI

// The pieces the CLI needs that the UI also uses.  Kept here so both entry
// points run exactly the same code.
static int cliInstall(const mgl::Paths &p, const mgl::Env &env) {
	const mgl::Manifest m = mgl::loadManifest(p, env, true);
	if (!m.ok) { fprintf(stderr, "%s\n", m.err.c_str()); return 1; }
	if (m.latestGl.empty()) { fprintf(stderr, "the release does not publish %s\n", p.frontendAsset().c_str()); return 1; }
	mgl::Asset asset;
	std::string err;
	if (!mgl::acquireAsset(p, env, p.frontendAsset(), m.latestGl, 0, mgl::Progress(), "MinarchGL", asset, err)) {
		fprintf(stderr, "%s\n", err.c_str());
		return 1;
	}
	const mgl::Result r = mgl::installFrontend(p, asset.path, m.latestGl, mgl::Progress());
	if (!r.ok) { fprintf(stderr, "%s\n", r.err.c_str()); return 1; }
	printf("frontend: installed %s from %s\n", mgl::shortHash(m.latestGl).c_str(),
		mgl::manifestSourceName(m.source));
	return 0;
}

static int cliCoreInstall(const mgl::Paths &p, const mgl::Env &env, const std::string &tag) {
	const mgl::Manifest m = mgl::loadManifest(p, env, true);
	if (!m.ok) { fprintf(stderr, "%s\n", m.err.c_str()); return 1; }
	const mgl::CoreEntry *e = m.list.find(tag);
	if (!e) { fprintf(stderr, "no %s in the release\n", tag.c_str()); return 1; }
	const mgl::Result r = mgl::installCore(p, env, *e, mgl::Progress());
	if (!r.ok) { fprintf(stderr, "%s\n", r.err.c_str()); return 1; }
	printf("core: installed %s %s\n", e->name.c_str(), e->pin.c_str());
	return 0;
}

// True for an argument this tool handles without a screen.  Anything else --
// including --from-core, which is reserved for the core paks' launch check --
// falls through to the UI, so an unknown flag can never stop the tool from
// starting (5.12).
static bool isCliOption(const char *arg) {
	static const char *const known[] = {
		"--status", "--fetch", "--install", "--enable", "--disable", "--uninstall",
		"--core-install", "--core-remove", "--post-update",
	};
	for (const char *k : known)
		if (!strcmp(arg, k)) return true;
	return false;
}

static int cli(const mgl::Paths &p, const mgl::Env &env, int argc, char *argv[]) {
	const std::string what = argv[1];
	if (what == "--status") {
		const mgl::Manifest m = mgl::loadManifest(p, env, false);
		printf("%s", mgl::statusText(p, m).c_str());
		return 0;
	}
	if (what == "--fetch") {
		const mgl::Manifest m = mgl::loadManifest(p, env, true);
		if (!m.ok) { fprintf(stderr, "%s\n", m.err.c_str()); return 1; }
		printf("manifest: %s\n", mgl::manifestSourceName(m.source));
		printf("%s", mgl::statusText(p, m).c_str());
		return 0;
	}
	if (what == "--install") return cliInstall(p, env);
	if (what == "--enable") {
		const mgl::Result r = mgl::enableFrontend(p);
		if (!r.ok) { fprintf(stderr, "%s\n", r.err.c_str()); return 1; }
		printf("enabled\n");
		return 0;
	}
	if (what == "--disable") {
		const mgl::Result r = mgl::disableFrontend(p);
		if (!r.ok) { fprintf(stderr, "%s\n", r.err.c_str()); return 1; }
		printf("disabled\n");
		return 0;
	}
	if (what == "--uninstall") {
		const mgl::Result r = mgl::uninstallFrontend(p);
		if (!r.ok) { fprintf(stderr, "%s\n", r.err.c_str()); return 1; }
		printf("uninstalled\n");
		return 0;
	}
	if (what == "--post-update") return cliInstall(p, env);
	if (what == "--core-install" && argc > 2) return cliCoreInstall(p, env, argv[2]);
	if (what == "--core-remove" && argc > 2) {
		const mgl::Result r = mgl::removeCore(p, argv[2]);
		if (!r.ok) { fprintf(stderr, "%s\n", r.err.c_str()); return 1; }
		printf("core removed: %s\n", argv[2]);
		return 0;
	}
	fprintf(stderr, "unknown option: %s\n", what.c_str());
	return 2;
}

///////////////////////////////////////////////////////////////////////////////
// UI model

enum class Screen { Install, Main, Status, Core, Done };

// One row of a list screen.
//
// `action` runs on Y (it may write to the card), `open` runs on A (navigation
// only).  A row with neither is plain text and is skipped by the selection, so
// A can be pressed repeatedly while browsing without ever triggering a write.
struct Row {
	std::string label;
	std::string value;
	std::function<void()> action;
	std::string actionHint;   // shown as "Y <hint>" while the row is selected
	std::function<void()> open;
	std::string openHint;     // shown as "A <hint>"
	bool plain = false;
};

inline bool selectable(const Row &r) { return (bool)r.action || (bool)r.open; }

struct App {
	mgl::Paths paths;
	mgl::Env env;
	mgl::Manifest manifest;
	mgl::FrontendInfo frontend;
	int coreIndex = 0;          // core page target
	int statusRow = -1;         // status row the core page was opened from

	Screen screen = Screen::Install;
	std::vector<Row> rows;
	int sel = -1;
	int top = 0;

	bool busy = false;
	std::string busyText;
	bool message = false;
	std::string messageText;
	bool doneFromCore = false;

	int dirty = 1;
	int show_setting = 0;
	int quit = 0;
	SDL_Surface *surf = nullptr;
	// Where the rows go: the framework's own list area (settings.cpp), i.e. the
	// screen minus PADDING, minus the band the hardware group uses at the top and
	// one BUTTON_SIZE at the bottom for the hint pill.  Computed once from the
	// real surface size, so 640x480, 720x480 and 1280x720 all lay out the same way.
	SDL_Rect listRect = {0, 0, 0, 0};
};

///////////////////////////////////////////////////////////////////////////////
// drawing

static void fillBackground(App &a) {
	const uint32_t bgc = CFG_getColor(COLOR_BACKGROUND);
	SDL_FillRect(a.surf, NULL, SDL_MapRGBA(a.surf->format,
		(bgc >> 24) & 0xFF, (bgc >> 16) & 0xFF, (bgc >> 8) & 0xFF, bgc & 0xFF));
}

static void blitText(SDL_Surface *dst, TTF_Font *font, const std::string &text, int x, int y, SDL_Color color) {
	if (text.empty()) return;
	SDL_Surface *t = TTF_RenderUTF8_Blended(font, text.c_str(), color);
	if (!t) return;
	SDL_Rect at = {x, y, 0, 0};
	SDL_BlitSurface(t, NULL, dst, &at);
	SDL_FreeSurface(t);
}

static int textWidth(TTF_Font *font, const std::string &text) {
	int w = 0;
	TTF_SizeUTF8(font, text.c_str(), &w, NULL);
	return w;
}

static void drawHints(App &a, const char *key, const std::string &hint, bool withBack = true) {
	auto pairs = std::vector<std::pair<std::string, std::string>>();
	if (withBack) pairs.push_back({"B", "BACK"});
	if (key && *key && !hint.empty()) pairs.push_back({key, hint});
	char *arr[5] = {NULL, NULL, NULL, NULL, NULL};
	for (size_t i = 0; i < pairs.size() && i < 2; i++) {
		arr[i * 2] = (char *)pairs[i].first.c_str();
		arr[i * 2 + 1] = (char *)pairs[i].second.c_str();
	}
	// align right; the action hint (not BACK) survives on a narrow screen
	GFX_blitButtonGroup(arr, pairs.size() > 1 ? 1 : 0, a.surf, 1);
}

// The list screens, drawn the way NextUI draws its own lists.
//
// Two styles, and which one applies is a property of the page, not of the row:
//
//   pages with a value column (Status)  the framework's "fixed" rows: the
//       selected row is a full-width accent bar, its label sits on a dark pill,
//       and the value column carries the state.  font.tiny.
//   pages without values (Main, Core, the install page)  the framework's "list"
//       rows, as on NextUI's own menu and Settings root: the selected row is
//       just a dark pill behind its label -- no accent bar, no value column.
//       font.small, which is what drawListItem uses.
//
// A plain row (a caption such as the reason on the install page) stays in the
// small caption font on the left edge and is never selected.
static void drawRows(App &a) {
	const SDL_Rect area = a.listRect;
	const bool fixed = a.screen == Screen::Status;
	TTF_Font *item_font = fixed ? font.tiny : font.small;
	const int x = area.x;
	const int w = area.w;
	const int row_h = SCALE1(BUTTON_SIZE);
	const int top = area.y;
	const int bottom = area.y + area.h;
	const int visible = (bottom - top) / row_h;
	const int label_x = x + SCALE1(OPTION_PADDING);
	const int value_right = x + w - SCALE1(OPTION_PADDING);

	if (a.sel < a.top) a.top = a.sel;
	if (a.sel >= a.top + visible) a.top = a.sel - visible + 1;
	if (a.top < 0) a.top = 0;

	const SDL_Color text_color = uintToColour(CFG_getColor(COLOR_LIST_TEXT));
	const SDL_Color sel_color = uintToColour(CFG_getColor(COLOR_LIST_TEXT_SELECTED));

	for (int i = a.top; i < a.top + visible && i < (int)a.rows.size(); i++) {
		const int y = top + (i - a.top) * row_h;
		const Row &r = a.rows[i];
		const bool sel = i == a.sel && selectable(r);
		const bool caption = r.plain && !selectable(r);
		TTF_Font *f = caption ? font.tiny : item_font;
		const int ty = y + (row_h - TTF_FontHeight(f)) / 2;

		if (sel) {
			if (fixed) {
				SDL_Rect bar = {x, y, w, row_h};
				GFX_blitPillLight(ASSET_BUTTON, a.surf, &bar);
			}
			const int tw = textWidth(f, r.label);
			SDL_Rect pill = {x, y, tw + SCALE1(OPTION_PADDING * 2), row_h};
			GFX_blitPillDark(ASSET_BUTTON, a.surf, &pill);
		}
		blitText(a.surf, f, r.label, label_x, ty, sel ? sel_color : text_color);
		if (!r.value.empty()) {
			const int vw = textWidth(font.tiny, r.value);
			const int vy = y + (row_h - TTF_FontHeight(font.tiny)) / 2;
			blitText(a.surf, font.tiny, r.value, value_right - vw, vy, text_color);
		}
	}

	if ((int)a.rows.size() > visible) {
		if (a.top > 0) {
			SDL_Rect up = {a.surf->w / 2 - SCALE1(12), top - SCALE1(PADDING), SCALE1(24), SCALE1(6)};
			GFX_blitAsset(ASSET_SCROLL_UP, NULL, a.surf, &up);
		}
		if (a.top + visible < (int)a.rows.size()) {
			SDL_Rect down = {a.surf->w / 2 - SCALE1(12), bottom - SCALE1(12), SCALE1(24), SCALE1(6)};
			GFX_blitAsset(ASSET_SCROLL_DOWN, NULL, a.surf, &down);
		}
	}
}

// Wrap text to `maxw` by words, breaking a word that cannot fit at all (a URL,
// typically).  SDL_ttf's own wrapping is not used: it lays out a wrapped surface
// by the font's line skip, which does not match the way every NextUI screen
// draws text, and an unbreakable word simply overflows.
static std::vector<std::string> wrapText(TTF_Font *font, const std::string &text, int maxw) {
	std::vector<std::string> lines;
	std::string line;
	size_t pos = 0;
	while (pos <= text.size()) {
		const size_t nl = text.find('\n', pos);
		const std::string para = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		size_t at = 0;
		while (at < para.size()) {
			size_t sp = para.find(' ', at);
			std::string word = para.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
			at = (sp == std::string::npos) ? para.size() : sp + 1;
			while (!word.empty() && textWidth(font, word) > maxw) {
				size_t cut = word.size();
				while (cut > 1 && textWidth(font, word.substr(0, cut)) > maxw) cut--;
				if (!line.empty()) { lines.push_back(line); line.clear(); }
				lines.push_back(word.substr(0, cut));
				word = word.substr(cut);
			}
			if (word.empty()) continue;
			const std::string candidate = line.empty() ? word : line + " " + word;
			if (textWidth(font, candidate) <= maxw) {
				line = candidate;
			} else {
				if (!line.empty()) lines.push_back(line);
				line = word;
			}
		}
		if (!line.empty()) { lines.push_back(line); line.clear(); }
		if (nl == std::string::npos) break;
		pos = nl + 1;
	}
	return lines;
}

// One centered block of text: the busy line and every error/result message.
static void drawMessageText(App &a, const std::string &text, TTF_Font *font, SDL_Color color) {
	const int maxw = a.surf->w - SCALE1(PADDING * 4);
	const std::vector<std::string> lines = wrapText(font, text, maxw);
	const int line_h = TTF_FontHeight(font) + SCALE1(2);
	int y = (a.surf->h - (int)lines.size() * line_h) / 2;
	for (const std::string &l : lines) {
		blitText(a.surf, font, l, (a.surf->w - textWidth(font, l)) / 2, y, color);
		y += line_h;
	}
}

static void drawCenterMessage(App &a, const std::string &line, TTF_Font *font, int y, SDL_Color color) {
	blitText(a.surf, font, line, (a.surf->w - textWidth(font, line)) / 2, y, color);
}

// The done page after an uninstall: nothing to press but B.
static void drawDone(App &a) {
	const int cy = a.surf->h / 2;
	drawCenterMessage(a, a.messageText, font.large, cy - SCALE1(PILL_SIZE), COLOR_WHITE);
	drawCenterMessage(a, "Press B to continue.", font.tiny, cy + SCALE1(PILL_SIZE),
		uintToColour(CFG_getColor(COLOR_LIST_TEXT)));
}

// While an action runs the screen is replaced by one line of text: the action
// blocks the frame loop, so this is the only thing the user sees until it ends.
static void drawBusy(App &a) {
	fillBackground(a);
	drawMessageText(a, a.busyText, font.medium, COLOR_WHITE);
}

static void drawMessage(App &a) {
	fillBackground(a);
	drawMessageText(a, a.messageText, font.medium, COLOR_WHITE);
	// Only A does anything here, so only A is offered.
	drawHints(a, "A", "OKAY", false);
}

///////////////////////////////////////////////////////////////////////////////
// screen contents

static void selectFirst(App &a) {
	a.sel = -1;
	a.top = 0;
	for (int i = 0; i < (int)a.rows.size(); i++) {
		if (selectable(a.rows[i])) { a.sel = i; return; }
	}
}

static void moveSelection(App &a, int delta) {
	const int n = (int)a.rows.size();
	if (n == 0) return;
	int i = a.sel;
	if (i < 0 || i >= n) i = delta > 0 ? -1 : 0;
	for (int k = 0; k < n; k++) {
		i = (i + delta + n) % n;
		if (selectable(a.rows[i])) { a.sel = i; a.dirty = 1; return; }
	}
}

static void rebuildRows(App &a);
static void render(App &a);
static mgl::Progress progressOf(App &a);

// Changing screens starts the selection over: the rows are a different set, and
// carrying the index across would leave the cursor on whatever happened to sit
// at that position (row 2 of the install page is "Install", row 2 of the main
// page is "Uninstall").
static void resetSelection(App &a) {
	a.sel = -1;
	a.top = 0;
}

static void go(App &a, Screen s) {
	a.screen = s;
	resetSelection(a);
	rebuildRows(a);
	a.dirty = 1;
}

static void showFailure(App &a, const std::string &err) {
	a.message = true;
	a.messageText = err.empty() ? "Failed." : err;
	a.dirty = 1;
}

static void showResult(App &a, const std::string &text, Screen next) {
	a.screen = next;
	resetSelection(a);
	rebuildRows(a);
	a.message = true;
	a.messageText = text;
	a.dirty = 1;
}

static std::string titleOf(const mgl::FrontendInfo &f) {
	switch (f.state) {
	case mgl::FrontendState::Enabled: return "enabled";
	case mgl::FrontendState::Disabled: return "disabled";
	default: return mgl::frontendStateName(f.state);
	}
}

// --- actions ---------------------------------------------------------------

static void finishFrontendAction(App &a, const std::string &okText) {
	a.busy = false;
	a.frontend = mgl::frontendInfo(a.paths, a.manifest.latestGl);
	const bool installed = a.frontend.state == mgl::FrontendState::Disabled ||
		a.frontend.state == mgl::FrontendState::Enabled;
	if (!installed) {
		// The card is in a state Repair cannot leave (no stock copy to fall back
		// on): stay on the install page and let it explain itself.
		go(a, Screen::Install);
		showFailure(a, "MinarchGL was installed, but the frontend state is still unknown.");
		return;
	}
	showResult(a, okText, Screen::Main);
}

static void doFrontendInstall(App &a, const char *okText) {
	a.busy = true;
	a.busyText = "Working...";
	render(a);
	std::string err;
	mgl::Asset asset;
	if (!mgl::acquireAsset(a.paths, a.env, a.paths.frontendAsset(), a.manifest.latestGl, 0,
	                       progressOf(a), "MinarchGL", asset, err)) {
		a.busy = false;
		showFailure(a, err);
		return;
	}
	const mgl::Result r = mgl::installFrontend(a.paths, asset.path, a.manifest.latestGl, progressOf(a));
	a.busy = false;
	if (!r.ok) { showFailure(a, r.err); return; }
	finishFrontendAction(a, okText);
}

static void doEnable(App &a) {
	a.busy = true;
	a.busyText = "Working...";
	render(a);
	const mgl::Result r = mgl::enableFrontend(a.paths);
	a.busy = false;
	if (!r.ok) { showFailure(a, r.err); return; }
	a.frontend = mgl::frontendInfo(a.paths, a.manifest.latestGl);
	go(a, Screen::Main);
}

static void doDisable(App &a) {
	a.busy = true;
	a.busyText = "Working...";
	render(a);
	const mgl::Result r = mgl::disableFrontend(a.paths);
	a.busy = false;
	if (!r.ok) { showFailure(a, r.err); return; }
	a.frontend = mgl::frontendInfo(a.paths, a.manifest.latestGl);
	go(a, Screen::Main);
}

static void doUninstallFrontend(App &a) {
	a.busy = true;
	a.busyText = "Working...";
	render(a);
	const mgl::Result r = mgl::uninstallFrontend(a.paths);
	a.busy = false;
	if (!r.ok) { showFailure(a, r.err); return; }
	a.frontend = mgl::frontendInfo(a.paths, a.manifest.latestGl);
	a.doneFromCore = false;
	a.messageText = "MinarchGL removed.";
	go(a, Screen::Done);
}

static const mgl::CoreEntry &currentCore(App &a) {
	static mgl::CoreEntry empty;
	if (a.coreIndex < 0 || a.coreIndex >= (int)a.manifest.list.cores.size()) return empty;
	return a.manifest.list.cores[a.coreIndex];
}

static void doCoreInstall(App &a, const char *okText) {
	const mgl::CoreEntry &e = currentCore(a);
	a.busy = true;
	a.busyText = "Working...";
	render(a);
	const mgl::Result r = mgl::installCore(a.paths, a.env, e, progressOf(a));
	a.busy = false;
	if (!r.ok) { showFailure(a, r.err); return; }
	showResult(a, okText, Screen::Core);
}

static void doCoreRemove(App &a) {
	const mgl::CoreEntry &e = currentCore(a);
	a.busy = true;
	a.busyText = "Removing " + e.name + "...";
	render(a);
	const mgl::Result r = mgl::removeCore(a.paths, e.tag);
	a.busy = false;
	if (!r.ok) { showFailure(a, r.err); return; }
	a.doneFromCore = true;
	a.messageText = e.name + " removed.";
	go(a, Screen::Done);
}

static void doCreateFolders(App &a) {
	const mgl::CoreEntry &e = currentCore(a);
	const mgl::Result r = mgl::createCoreFolders(a.paths, e);
	if (!r.ok) { showFailure(a, r.err); return; }
	showResult(a, "Folders created.", Screen::Core);
}

// --- rows ------------------------------------------------------------------

static void buildInstallRows(App &a) {
	Row head;
	head.plain = true;
	head.label = "Needs setup";
	a.rows.push_back(head);

	Row why;
	why.plain = true;
	why.label = a.frontend.reason.empty() ? "MinarchGL is not installed." : a.frontend.reason;
	a.rows.push_back(why);

	if (a.frontend.state == mgl::FrontendState::StockLost) return;   // nothing we can do

	if (!a.manifest.ok) {
		Row note;
		note.plain = true;
		note.label = a.manifest.err;
		a.rows.push_back(note);
		return;
	}
	if (a.manifest.latestGl.empty()) {
		Row note;
		note.plain = true;
		note.label = "The release does not publish the frontend.";
		a.rows.push_back(note);
		return;
	}

	const bool repair = mgl::installedBefore(a.frontend);
	Row act;
	act.label = repair ? "Repair MinarchGL" : "Install MinarchGL";
	act.actionHint = repair ? "REPAIR" : "INSTALL";
	act.action = [&a, repair]() { doFrontendInstall(a, repair ? "Repaired." : "Installed."); };
	a.rows.push_back(act);
}

static void buildMainRows(App &a) {
	const bool enabled = a.frontend.state == mgl::FrontendState::Enabled;
	Row toggle;
	toggle.label = enabled ? "Disable MinarchGL" : "Enable MinarchGL";
	toggle.actionHint = enabled ? "DISABLE" : "ENABLE";
	toggle.action = [&a, enabled]() { enabled ? doDisable(a) : doEnable(a); };
	a.rows.push_back(toggle);

	// Update is offered only when the release actually publishes a frontend and
	// it differs from the installed copy.  Same action as Repair.
	if (a.manifest.ok && !a.manifest.latestGl.empty() && !a.frontend.gl.empty() &&
	    a.frontend.gl != a.manifest.latestGl) {
		Row up;
		up.label = "Update MinarchGL";
		up.actionHint = "UPDATE";
		up.action = [&a]() { doFrontendInstall(a, "Updated."); };
		a.rows.push_back(up);
	}

	Row status;
	status.label = "Status";
	status.openHint = "OPEN";
	status.open = [&a]() { go(a, Screen::Status); };
	a.rows.push_back(status);

	Row un;
	un.label = "Uninstall MinarchGL";
	un.actionHint = "UNINSTALL";
	un.action = [&a]() { doUninstallFrontend(a); };
	a.rows.push_back(un);
}

static void buildStatusRows(App &a) {
	Row state;
	state.plain = true;
	state.label = "State";
	state.value = titleOf(a.frontend);
	a.rows.push_back(state);

	Row gl;
	gl.plain = true;
	gl.label = "MinarchGL";
	if (a.frontend.gl.empty()) gl.value = "missing";
	else {
		gl.value = mgl::shortHash(a.frontend.gl);
		if (a.manifest.latestGl.empty()) gl.value += "  unknown";
		else if (a.frontend.gl == a.manifest.latestGl) gl.value += "  current";
		else gl.value += "  update available";
	}
	a.rows.push_back(gl);

	Row stock;
	stock.plain = true;
	stock.label = "Stock";
	stock.value = a.frontend.stock.empty() ? "missing" : mgl::shortHash(a.frontend.stock);
	a.rows.push_back(stock);

	Row list;
	list.plain = true;
	list.label = "Core list";
	if (!a.manifest.ok) list.value = "unavailable";
	else if (!a.manifest.list.hasPlatform) list.value = "no release for " + a.paths.platform;
	else {
		list.value = std::to_string(a.manifest.list.cores.size()) + " cores   " +
			mgl::manifestSourceName(a.manifest.source);
	}
	a.rows.push_back(list);

	if (a.manifest.ok) {
		for (int i = 0; i < (int)a.manifest.list.cores.size(); i++) {
			const mgl::CoreEntry &e = a.manifest.list.cores[i];
			const mgl::CoreStatus s = mgl::coreStatus(a.paths, e);
			Row r;
			r.label = e.name;
			r.value = mgl::coreStateName(s.state);
			r.openHint = "OPEN";
			r.open = [&a, i]() { a.coreIndex = i; a.statusRow = (int)a.rows.size() - a.manifest.list.cores.size() + i; go(a, Screen::Core); };
			a.rows.push_back(r);
		}
	}
}

static void buildCoreRows(App &a) {
	const mgl::CoreEntry &e = currentCore(a);
	if (e.tag.empty()) {
		Row note;
		note.plain = true;
		note.label = "No core in the release list.";
		a.rows.push_back(note);
		return;
	}
	const mgl::CoreStatus s = mgl::coreStatus(a.paths, e);

	if (s.state == mgl::CoreState::NotInstalled) {
		Row r;
		r.label = "Install";
		r.actionHint = "INSTALL";
		r.action = [&a]() { doCoreInstall(a, "Installed."); };
		a.rows.push_back(r);
	}
	if (s.state == mgl::CoreState::NotLatest) {
		Row r;
		r.label = "Update";
		r.actionHint = "UPDATE";
		r.action = [&a]() { doCoreInstall(a, "Updated."); };
		a.rows.push_back(r);
	}
	if (s.state != mgl::CoreState::NotInstalled) {
		Row r;
		r.label = "Uninstall";
		r.actionHint = "UNINSTALL";
		r.action = [&a]() { doCoreRemove(a); };
		a.rows.push_back(r);
	}

	const std::vector<std::string> dirs = mgl::romFoldersForTag(a.paths, e.tag);
	for (const std::string &d : dirs) {
		Row r;
		r.plain = true;
		r.label = "Roms/" + d;
		a.rows.push_back(r);
	}

	if (dirs.empty()) {
		Row r;
		r.label = "Create default folder";
		r.actionHint = "CREATE";
		r.action = [&a]() { doCreateFolders(a); };
		a.rows.push_back(r);
	}
}

static void rebuildRows(App &a) {
	a.rows.clear();
	switch (a.screen) {
	case Screen::Install: buildInstallRows(a); break;
	case Screen::Main: buildMainRows(a); break;
	case Screen::Status: buildStatusRows(a); break;
	case Screen::Core: buildCoreRows(a); break;
	case Screen::Done: break;
	}
	if (a.sel < 0 || a.sel >= (int)a.rows.size() || !selectable(a.rows[a.sel])) selectFirst(a);
}

///////////////////////////////////////////////////////////////////////////////
// frame

static void render(App &a) {
	GFX_clear(a.surf);

	if (a.busy) {
		drawBusy(a);
		GFX_flip(a.surf);
		return;
	}

	fillBackground(a);
	if (a.message) {
		drawMessage(a);
		GFX_flip(a.surf);
		return;
	}

	// No page title: the rows say what the screen is.  The band at the top is
	// the hardware group's (battery, wifi, clock), which is why the list starts
	// below it -- the same split minarch's own menu uses.
	GFX_blitHardwareGroup(a.surf, a.show_setting);
	if (a.screen == Screen::Done) {
		drawDone(a);
		drawHints(a, "B", "CONTINUE", false);
	} else {
		const Row *sel = (a.sel >= 0 && a.sel < (int)a.rows.size()) ? &a.rows[a.sel] : nullptr;
		if (sel && sel->action) drawHints(a, "Y", sel->actionHint);
		else if (sel && sel->open) drawHints(a, "A", sel->openHint);
		else drawHints(a, NULL, "");
		drawRows(a);
	}
	GFX_flip(a.surf);
	a.dirty = 0;
}

static mgl::Progress progressOf(App &a) {
	return [&a](const std::string &stage, const std::string &subject) {
		a.busy = true;
		a.busyText = stage + " " + subject + "...";
		render(a);
	};
}

///////////////////////////////////////////////////////////////////////////////
// input

static void handleInput(App &a) {
	if (a.busy) return;   // input is dropped while an action runs

	if (a.message) {
		if (PAD_justPressed(BTN_A)) {
			a.message = false;
			a.dirty = 1;
		}
		return;
	}

	if (a.screen == Screen::Done) {
		if (PAD_justPressed(BTN_B)) {
			if (a.doneFromCore) go(a, Screen::Core);
			else go(a, Screen::Install);
		}
		return;
	}

	if (PAD_justRepeated(BTN_UP)) moveSelection(a, -1);
	else if (PAD_justRepeated(BTN_DOWN)) moveSelection(a, 1);

	if (PAD_justPressed(BTN_B)) {
		switch (a.screen) {
		case Screen::Status: go(a, Screen::Main); break;
		case Screen::Core: {
			// Back to the status list with the same core still selected.
			const int row = a.statusRow;
			go(a, Screen::Status);
			if (row >= 0 && row < (int)a.rows.size()) a.sel = row;
			break;
		}
		default: a.quit = 1; break;   // a root screen: leave the tool
		}
		return;
	}

	if (a.sel < 0 || a.sel >= (int)a.rows.size()) return;
	const Row &r = a.rows[a.sel];
	if (PAD_justPressed(BTN_Y) && r.action) r.action();
	else if (PAD_justPressed(BTN_A) && r.open) r.open();
}

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
	// The launch script redirects this to $LOGS_PATH; the first line is what the
	// tool was asked to do, which is the first thing to look at in a bug report.
	{
		std::string line = "minarch-gl-installer:";
		for (int i = 0; i < argc; i++) line += std::string(" ") + argv[i];
		printf("%s\n", line.c_str());
		fflush(stdout);
	}

	App app;
	app.paths = makePaths();
	app.env = mgl::defaultEnv();

	// No UI: run one action and leave.  Everything below the UI entry point is
	// the same code, so a CLI failure is a UI failure.
	if (argc > 1 && isCliOption(argv[1])) return cli(app.paths, app.env, argc, argv);
	if (argc > 1 && strcmp(argv[1], "--from-core") == 0) {
		// Reserved: the core paks do not run a launch check yet (5.11).  Recognised
		// and ignored so adding it later does not change this tool's interface.
		fprintf(stderr, "minarch-gl-installer: --from-core ignored (not wired yet)\n");
	}

	PWR_setCPUSpeed(CPU_SPEED_AUTO);
	app.surf = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	{
		// In the log: the layout depends on these, and they differ per device and
		// per HDMI state.  (The panel geometry is detected from $DEVICE, so a
		// launch without NextUI's environment gets the 640x480 default.)
		char geo[192];
		snprintf(geo, sizeof geo, "screen %dx%d (fixed %dx%d, panel %dx%d, scale %d, hdmi %d)",
			app.surf->w, app.surf->h, FIXED_WIDTH, FIXED_HEIGHT, panel_w, panel_h,
			FIXED_SCALE, hdmi_active);
		printf("%s\n", geo);
		fflush(stdout);
	}

	// Startup check: fetch the manifest (bounded, ~5 s worst case) while the
	// "Checking for updates..." line is on screen.  A failure is not fatal: the
	// local actions still work and the page says what is missing.
	app.busy = true;
	app.busyText = "Checking for updates...";
	render(app);
	app.manifest = mgl::loadManifest(app.paths, app.env, true, progressOf(app));
	app.busy = false;
	app.frontend = mgl::frontendInfo(app.paths, app.manifest.latestGl);

	app.screen = (app.frontend.state == mgl::FrontendState::Disabled ||
	              app.frontend.state == mgl::FrontendState::Enabled) ? Screen::Main : Screen::Install;

	if (argc > 2 && strcmp(argv[1], "--page") == 0) {
		const std::string page = argv[2];
		if (page == "main") app.screen = Screen::Main;
		else if (page == "status") app.screen = Screen::Status;
		else if (page == "core") { app.screen = Screen::Core; app.coreIndex = 0; }
		else if (page == "done") { app.screen = Screen::Done; app.doneFromCore = false; app.messageText = "MinarchGL removed."; }
		else if (page == "message") {
			// A deliberately long one: it has to wrap and stay centered, which is
			// what a curl error and a "card full" report look like.
			app.message = true;
			app.messageText = "cannot fetch https://github.com/zhangmq/minarch-gl/releases/"
				"latest/download/core-list.json (curl: (28) Connection timed out after "
				"3001 milliseconds)\nNo network and no local copy.";
		}
		else if (page != "install") { fprintf(stderr, "unknown page: %s\n", page.c_str()); return 2; }
		fprintf(stderr, "minarch-gl-installer: page=%s\n", page.c_str());
	}

	// The list area, exactly as the framework sets it up (settings.cpp): inset by
	// PADDING, then below the top band and one row shorter at the bottom.
	app.listRect = {SCALE1(PADDING), SCALE1(PADDING),
	                app.surf->w - SCALE1(PADDING * 2), app.surf->h - SCALE1(PADDING * 2)};
	app.listRect.y += SCALE1(PILL_SIZE);
	app.listRect.h -= SCALE1(PILL_SIZE) + SCALE1(BUTTON_SIZE);

	rebuildRows(app);
	app.dirty = 1;

	while (!app.quit) {
		GFX_startFrame();
		PAD_poll();
		handleInput(app);
		PWR_update(&app.dirty, &app.show_setting, NULL, NULL);
		if (app.dirty) render(app);
		else GFX_sync();
	}

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
