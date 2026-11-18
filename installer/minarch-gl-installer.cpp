// minarch-gl-installer -- install/restore the MinarchGL frontend and its cores.
//
// The tool has one purpose and therefore one screen: a single toggle
// (Enable MinarchGL / Disable MinarchGL, depending on what is installed) and a
// Status view.  Everything else is a consequence of that toggle.
//
// The UI is NextUI's own menu framework (workspace/all/settings/menu.cpp), the
// same code behind Settings, so fonts, pills, hints and colours are the system
// ones rather than a lookalike.
//
// All file-level decisions live in installer-core.hpp, which has no SDL dependency
// and is covered by tests/installer-core-test.cpp on the build host.  This file
// only chooses when to call it and shows the result.
//
// CLI (no UI, for automation/debugging):
//   minarch-gl-installer.elf --status     print the frontend state and core-pak count
//   minarch-gl-installer.elf --install    enable minarch-gl
//   minarch-gl-installer.elf --restore    restore stock minarch
//   minarch-gl-installer.elf --page P     open the UI on screen P (main|status|ask)
//                                   without going through the menu; for
//                                   screenshots and layout checks on the device

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

extern "C" {
#include <msettings.h>
#include "defines.h"
#include "api.h"
#include "utils.h"
}

#include "menu.hpp"
#include "installer-core.hpp"

///////////////////////////////////////////////////////////////////////////////
// paths

static std::string selfDir() {
	char buf[1024];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		char *slash = strrchr(buf, '/');
		if (slash) { *slash = '\0'; return buf; }
	}
	return ".";
}

static mgl::Paths makePaths() {
	mgl::Paths p;
	p.sdcard = SDCARD_PATH;
	p.platform = PLATFORM;
	p.pakDir = selfDir();
	return p;
}

///////////////////////////////////////////////////////////////////////////////
// CLI

static int cli(const mgl::Paths &paths, const char *what) {
	using namespace mgl;
	if (!strcmp(what, "--status")) {
		// Same text the Status screen shows, so a device check can be scripted.
		printf("%s\n", statusLines(paths).c_str());
		printf("cores: %d installed, %d in payload\n",
			countInstalledCores(paths), countPayloadCores(paths));
		printf("state file: %s\n", paths.statePath().c_str());
		return 0;
	}
	if (!strcmp(what, "--install")) {
		const InstallResult r = installGl(paths);
		if (!r.ok) { fprintf(stderr, "install failed: %s\n", r.err.c_str()); return 1; }
		printf("installed: %d core pak(s), re-enabled %d, skipped %d foreign\n",
			r.installed, r.reenabled, r.foreign);
		return 0;
	}
	if (!strcmp(what, "--restore")) {
		const RestoreResult r = restoreStock(paths);
		if (!r.ok) { fprintf(stderr, "restore failed: %s\n", r.err.c_str()); return 1; }
		printf("restored: hid %d core pak(s)\n", r.disabled);
		return 0;
	}
	fprintf(stderr, "unknown option: %s\n", what);
	return 2;
}

///////////////////////////////////////////////////////////////////////////////
// UI

// Which menu is on screen.
enum class Screen { Main, AskRomFolders, Status, Dirs };

struct Context {
	MenuList *menu = nullptr;
	SDL_Surface *screen = nullptr;
	SDL_Rect listRect = {0, 0, 0, 0};
	SDL_Rect titleRect = {0, 0, 0, 0};
	int dirty = 1;
	int show_setting = 0;
};

static int g_quit = 0;
static std::string pendingMessage;
static mgl::Paths g_paths;

static std::string runEnable() {
	const mgl::InstallResult r = mgl::installGl(g_paths);
	if (!r.ok) return "Enable failed:\n" + r.err;
	if (r.foreign) {
		// Another pak owns one of our tags: say so instead of silently skipping.
		return "minarch-gl enabled, but " + std::to_string(r.foreign) +
			" core pak(s) were left alone: a foreign pak already uses that tag.";
	}
	return "";  // silence: the menu rebuild shows the new state, or asks below
}

static std::string runDisable() {
	const mgl::RestoreResult r = mgl::restoreStock(g_paths);
	if (!r.ok) return "Disable failed:\n" + r.err;
	std::string msg = "Stock minarch restored.";
	if (r.disabled) msg += "\nHid " + std::to_string(r.disabled) + " MinarchGL core pak(s).";
	return msg;
}

// The tool's screens: the main menu, the ROM-folder question that follows a
// successful enable, and the read-only status pages (one per core, listing the
// directories carrying its tag).
static Screen g_screen = Screen::Main;
static std::vector<std::string> g_askFolders;
static int g_needRebuild = 0;

static void rebuildMenu(Context &ctx);

// One row of a status page.  A directory row is a single left-aligned path; a
// key row has a label on the left and a value on the right.  Both are drawn in
// font.tiny: this page uses one size, not the framework's 24/20 pair.
struct UiRow {
	std::string label;
	std::string value;
	bool dir = false;
	int core = -1;   // >= 0: A opens that core's directory page
};

static std::vector<UiRow> g_rows;
static int g_sel = 0;
static int g_top = 0;
static int g_dirsCore = 0;
static int g_enterStatus = 0;
static SDL_Surface *g_surface = nullptr;

// Installing copies ~15 MB of cores onto the card; the menu callback runs on
// the same thread as the frame loop, so say what is happening before the copy
// instead of leaving the previous frame frozen on screen.
static void showBusy(const char *msg) {
	if (!g_surface) return;
	const uint32_t bgc = CFG_getColor(COLOR_BACKGROUND);
	SDL_FillRect(g_surface, NULL, SDL_MapRGBA(g_surface->format,
		(bgc >> 24) & 0xFF, (bgc >> 16) & 0xFF, (bgc >> 8) & 0xFF, bgc & 0xFF));
	char line[64];
	snprintf(line, sizeof line, "%s", msg);
	GFX_blitMessage(font.large, line, g_surface, NULL);
	GFX_flip(g_surface);
}

static void buildStatusRows() {
	using namespace mgl;
	g_rows.clear();
	const StatusInfo si = statusInfo(g_paths);
	g_rows.push_back({"Activated", si.activated});
	g_rows.push_back({"Activated Version", si.activatedHash});
	g_rows.push_back({"Backup", si.backup});
	g_rows.push_back({"Backup Version", si.backupHash});
	for (int i = 0; i < (int)si.cores.size(); i++)
		g_rows.push_back({si.cores[i].name, si.cores[i].hash, false, i});
}

static void buildDirsRows(int core) {
	using namespace mgl;
	g_rows.clear();
	const StatusInfo si = statusInfo(g_paths);
	if (core < 0 || core >= (int)si.cores.size()) return;
	for (const std::string &d : si.cores[core].dirs) {
		UiRow r;
		r.value = d;
		r.dir = true;
		g_rows.push_back(r);
	}
	if (g_rows.empty()) {
		UiRow r;
		r.value = "no folder tagged " + std::string(kCores[core].tag);
		r.dir = true;
		g_rows.push_back(r);
	}
}

static void blitLine(SDL_Surface *dst, const std::string &text, int x, int y, SDL_Color color) {
	if (text.empty()) return;
	SDL_Surface *t = TTF_RenderUTF8_Blended(font.tiny, text.c_str(), color);
	if (!t) return;
	SDL_Rect at = {x, y, 0, 0};
	SDL_BlitSurface(t, NULL, dst, &at);
	SDL_FreeSurface(t);
}

// The status pages use the framework's assets and colours -- selected row is a
// light pill, its label sits on a dark pill -- but draw their own rows so every
// line can be font.tiny.
static void drawRowsPage(Context &ctx, char **hints) {
	const int x = SCALE1(PADDING);
	const int w = ctx.screen->w - SCALE1(PADDING * 2);
	const int row_h = SCALE1(BUTTON_SIZE);
	const int top = SCALE1(PADDING + PILL_SIZE);
	const int bottom = ctx.screen->h - SCALE1(BUTTON_SIZE);
	const int visible = (bottom - top) / row_h;
	const int label_x = x + SCALE1(OPTION_PADDING);
	const int value_right = x + w - SCALE1(OPTION_PADDING);

	if (g_sel < g_top) g_top = g_sel;
	if (g_sel >= g_top + visible) g_top = g_sel - visible + 1;
	if (g_top < 0) g_top = 0;

	GFX_clear(ctx.screen);
	const uint32_t bgc = CFG_getColor(COLOR_BACKGROUND);
	SDL_FillRect(ctx.screen, NULL, SDL_MapRGBA(ctx.screen->format,
		(bgc >> 24) & 0xFF, (bgc >> 16) & 0xFF, (bgc >> 8) & 0xFF, bgc & 0xFF));
	GFX_blitHardwareGroup(ctx.screen, ctx.show_setting);
	GFX_blitButtonGroup(hints, 1, ctx.screen, 1);

	const SDL_Color text_color = uintToColour(CFG_getColor(COLOR_LIST_TEXT));
	const SDL_Color sel_color = uintToColour(CFG_getColor(COLOR_LIST_TEXT_SELECTED));

	for (int i = g_top; i < g_top + visible && i < (int)g_rows.size(); i++) {
		const int y = top + (i - g_top) * row_h;
		const bool sel = i == g_sel;
		const UiRow &r = g_rows[i];

		if (sel) {
			SDL_Rect bar = {x, y, w, row_h};
			GFX_blitPillLight(ASSET_BUTTON, ctx.screen, &bar);
		}
		const int ty = y + (row_h - TTF_FontHeight(font.tiny)) / 2;

		if (r.dir) {
			blitLine(ctx.screen, r.value, label_x, ty, text_color);
			continue;
		}
		if (sel && !r.label.empty()) {
			int tw = 0;
			TTF_SizeUTF8(font.tiny, r.label.c_str(), &tw, NULL);
			SDL_Rect pill = {x, y, tw + SCALE1(OPTION_PADDING * 2), row_h};
			GFX_blitPillDark(ASSET_BUTTON, ctx.screen, &pill);
		}
		blitLine(ctx.screen, r.label, label_x, ty, sel ? sel_color : text_color);
		if (!r.value.empty()) {
			int vw = 0;
			TTF_SizeUTF8(font.tiny, r.value.c_str(), &vw, NULL);
			blitLine(ctx.screen, r.value, value_right - vw, ty, text_color);
		}
	}

	if ((int)g_rows.size() > visible) {
		if (g_top > 0) {
			SDL_Rect up = {ctx.screen->w / 2 - SCALE1(12), top - SCALE1(PADDING), SCALE1(24), SCALE1(6)};
			GFX_blitAsset(ASSET_SCROLL_UP, NULL, ctx.screen, &up);
		}
		if (g_top + visible < (int)g_rows.size()) {
			SDL_Rect down = {ctx.screen->w / 2 - SCALE1(12), bottom - SCALE1(12), SCALE1(24), SCALE1(6)};
			GFX_blitAsset(ASSET_SCROLL_DOWN, NULL, ctx.screen, &down);
		}
	}

	GFX_flip(ctx.screen);
}

static void statusInput(Context &ctx) {
	const int n = (int)g_rows.size();
	if (n > 0) {
		if (PAD_justRepeated(BTN_UP)) { g_sel = (g_sel + n - 1) % n; ctx.dirty = 1; }
		else if (PAD_justRepeated(BTN_DOWN)) { g_sel = (g_sel + 1) % n; ctx.dirty = 1; }
	}

	if (g_screen == Screen::Dirs) {
		if (PAD_justPressed(BTN_B)) {
			g_screen = Screen::Status;
			buildStatusRows();
			g_sel = 4 + g_dirsCore;
			ctx.dirty = 1;
		}
		return;
	}
	if (PAD_justPressed(BTN_B)) {
		g_screen = Screen::Main;
		rebuildMenu(ctx);
		return;
	}
	if (PAD_justPressed(BTN_A) && n > 0) {
		const UiRow r = g_rows[g_sel];
		if (r.core >= 0) {
			g_dirsCore = r.core;
			buildDirsRows(r.core);
			g_sel = 0;
			g_top = 0;
			g_screen = Screen::Dirs;
			ctx.dirty = 1;
		}
	}
}

static std::vector<AbstractMenuItem *> buildItems() {
	using namespace mgl;
	std::vector<AbstractMenuItem *> items;

	if (g_screen == Screen::AskRomFolders) {
		for (const std::string &f : g_askFolders) {
			items.push_back(new MenuItem{ListItemType::Generic, f, "",
				std::vector<std::any>{std::string("create")},
				std::vector<std::string>{"create"}});
		}
		items.push_back(new MenuItem{ListItemType::Button,
			"Create " + std::to_string(g_askFolders.size()) + " folder(s)", "",
			[](AbstractMenuItem &) -> InputReactionHint {
				const int made = mgl::createRomFolders(g_paths, g_askFolders);
				pendingMessage = "Created " + std::to_string(made) + " ROM folder(s).";
				g_askFolders.clear();
				g_screen = Screen::Main;
				g_needRebuild = 1;
				return InputReactionHint::NoOp;
			}});
		items.push_back(new MenuItem{ListItemType::Button, "Skip", "",
			[](AbstractMenuItem &) -> InputReactionHint {
				g_askFolders.clear();
				g_screen = Screen::Main;
				g_needRebuild = 1;
				return InputReactionHint::NoOp;
			}});
		return items;
	}

	const bool enabled = currentState(g_paths) == State::Enabled;
	items.push_back(new MenuItem{ListItemType::Button,
		enabled ? "Disable MinarchGL" : "Enable MinarchGL",
		"",
		[](AbstractMenuItem &) -> InputReactionHint {
			if (mgl::currentState(g_paths) == mgl::State::Enabled) {
				showBusy("Restoring stock minarch...");
				pendingMessage = runDisable();
			} else {
				showBusy("Enabling MinarchGL...");
				pendingMessage = runEnable();
				// Nothing to report: either ask about ROM folders or say nothing
				// (the rebuilt menu shows the new state).
				if (pendingMessage.empty()) {
					g_askFolders = mgl::missingDefaultRomFolders(g_paths);
					if (!g_askFolders.empty()) g_screen = Screen::AskRomFolders;
				}
			}
			return InputReactionHint::NoOp;
		}});
	items.push_back(new MenuItem{ListItemType::Button, "Status", "",
		[](AbstractMenuItem &) -> InputReactionHint {
			g_enterStatus = 1;
			return InputReactionHint::NoOp;
		}});
	return items;
}

static void rebuildMenu(Context &ctx) {
	delete ctx.menu;
	const char *title = (g_screen == Screen::AskRomFolders) ? "Create ROM folders?" : "MinarchGL Installer";
	ctx.menu = new MenuList(MenuItemType::List, title, buildItems());
	// MenuList::draw() asserts layout_called; a rebuilt list has to be laid out
	// again or the tool aborts right after the first state change.
	ctx.menu->performLayout(ctx.listRect);
	ctx.dirty = 1;
}

int main(int argc, char *argv[]) {
	g_paths = makePaths();
	const char *page = nullptr;
	if (argc > 1) {
		if (!strcmp(argv[1], "--page") && argc > 2) page = argv[2];
		else return cli(g_paths, argv[1]);
	}
	if (page && strcmp(page, "main") && strcmp(page, "status") && strcmp(page, "ask")) {
		fprintf(stderr, "unknown page: %s\n", page);
		return 2;
	}

	PWR_setCPUSpeed(CPU_SPEED_AUTO);
	Context ctx;
	ctx.screen = GFX_init(MODE_MAIN);
	g_surface = ctx.screen;
	PAD_init();
	PWR_init();
	InitSettings();

	mkdir(g_paths.stateDir().c_str(), 0755);

	if (page) {
		// Debug shortcut: the screen the argument names, without navigating.
		// Nothing below this depends on the menu having been used.
		if (!strcmp(page, "status")) g_enterStatus = 1;
		else if (!strcmp(page, "ask")) {
			g_askFolders = mgl::missingDefaultRomFolders(g_paths);
			g_screen = Screen::AskRomFolders;
		}
		fprintf(stderr, "minarch-gl-installer: page=%s\n", page);
	}

	ctx.listRect = {SCALE1(PADDING), SCALE1(PADDING),
	                ctx.screen->w - SCALE1(PADDING * 2), ctx.screen->h - SCALE1(PADDING * 2)};
	ctx.listRect = dy(ctx.listRect, SCALE1(PILL_SIZE));
	ctx.listRect.h -= SCALE1(BUTTON_SIZE);
	rebuildMenu(ctx);

	while (!g_quit) {
		GFX_startFrame();
		PAD_poll();

		if (g_enterStatus) {
			g_enterStatus = 0;
			g_screen = Screen::Status;
			buildStatusRows();
			g_sel = 0;
			g_top = 0;
			ctx.dirty = 1;
		}
		if (g_screen == Screen::Status || g_screen == Screen::Dirs) {
			char *back[] = {(char *)("B"), (char *)("BACK"), NULL};
			char *open[] = {(char *)("B"), (char *)("BACK"), (char *)("A"), (char *)("OPEN"), NULL};
			statusInput(ctx);
			PWR_update(&ctx.dirty, &ctx.show_setting, NULL, NULL);
			if (ctx.dirty) {
				drawRowsPage(ctx, g_screen == Screen::Status ? open : back);
				ctx.dirty = 0;
			} else {
				GFX_sync();
			}
			continue;
		}

		// B leaves the ROM-folder question instead of closing the tool: the
		// question is a follow-up, not a second level the user can get stuck in.
		if (g_screen == Screen::AskRomFolders && PAD_justPressed(BTN_B)) {
			g_askFolders.clear();
			g_screen = Screen::Main;
			rebuildMenu(ctx);
			// The rebuilt menu would see the same B as "quit" this frame.
			continue;
		}

		const mgl::State before = mgl::currentState(g_paths);
		ctx.menu->handleInput(ctx.dirty, g_quit);
		if (mgl::currentState(g_paths) != before) rebuildMenu(ctx);
		if (g_needRebuild) { g_needRebuild = 0; rebuildMenu(ctx); }

		PWR_update(&ctx.dirty, &ctx.show_setting, NULL, NULL);

		if (!pendingMessage.empty()) {
			MenuList::showOverlay(pendingMessage, OverlayDismissMode::DismissOnA);
			pendingMessage.clear();
			ctx.dirty = 1;
		}

		if (ctx.dirty) {
			GFX_clear(ctx.screen);
			uint32_t bgc = CFG_getColor(COLOR_BACKGROUND);
			SDL_FillRect(ctx.screen, NULL, SDL_MapRGBA(ctx.screen->format,
				(bgc >> 24) & 0xFF, (bgc >> 16) & 0xFF, (bgc >> 8) & 0xFF, bgc & 0xFF));

			int ow = GFX_blitHardwareGroup(ctx.screen, ctx.show_setting);
			int max_width = ctx.screen->w - SCALE1(PADDING * 2) - ow;
			ctx.titleRect = {SCALE1(PADDING), SCALE1(PADDING), max_width, SCALE1(PILL_SIZE)};

			char *hints[] = {(char *)("B"), (char *)("BACK"), (char *)("A"), (char *)("OKAY"), NULL};
			if (g_screen == Screen::AskRomFolders) {
				char *choose[] = {(char *)("B"), (char *)("SKIP"), (char *)("A"), (char *)("SELECT"), NULL};
				GFX_blitButtonGroup(choose, 1, ctx.screen, 1);
			}
			else GFX_blitButtonGroup(hints, 1, ctx.screen, 1);

			ctx.menu->draw(ctx.screen, ctx.listRect, ctx.titleRect);
			GFX_flip(ctx.screen);
			ctx.dirty = 0;
		} else {
			GFX_sync();
		}
	}

	delete ctx.menu;
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
