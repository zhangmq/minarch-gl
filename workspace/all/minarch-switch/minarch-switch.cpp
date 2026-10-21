// minarch-switch -- install/restore the minarch-gl frontend and its cores.
//
// The UI is NextUI's own menu framework (workspace/all/settings/menu.cpp), the
// same code behind Settings, so fonts, pills, hints and colours are the system
// ones rather than a lookalike.
//
// All file-level decisions live in switch-core.hpp, which has no SDL dependency
// and is covered by tests/switch-core-test.cpp on the build host.  This file
// only chooses when to call it and shows the result.
//
// CLI (no UI, for automation/debugging):
//   minarch-switch.elf --status     print the frontend state and core-pak count
//   minarch-switch.elf --install    enable minarch-gl
//   minarch-switch.elf --restore    restore stock minarch

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
#include "switch-core.hpp"

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

static mswitch::Paths makePaths() {
	mswitch::Paths p;
	p.sdcard = SDCARD_PATH;
	p.platform = PLATFORM;
	p.pakDir = selfDir();
	return p;
}

///////////////////////////////////////////////////////////////////////////////
// CLI

static int cli(const mswitch::Paths &paths, const char *what) {
	using namespace mswitch;
	if (!strcmp(what, "--status")) {
		const State st = currentState(paths);
		printf("frontend: %s\n", stateName(st));
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
static mswitch::Paths g_paths;

static std::string runEnable() {
	const mswitch::InstallResult r = mswitch::installGl(g_paths);
	if (!r.ok) return "Enable failed:\n" + r.err;
	std::string msg = "minarch-gl enabled.\nEvery pak now runs on the GL frontend.";
	if (r.installed) msg += "\nInstalled " + std::to_string(r.installed) + " core pak(s).";
	if (r.reenabled) msg += "\nRe-enabled " + std::to_string(r.reenabled) + " core pak(s).";
	if (r.foreign) msg += "\nSkipped " + std::to_string(r.foreign) + " foreign pak(s) with the same tag.";
	return msg;
}

static std::string runRestore() {
	const mswitch::RestoreResult r = mswitch::restoreStock(g_paths);
	if (!r.ok) return "Restore failed:\n" + r.err;
	std::string msg = "Stock minarch restored.";
	if (r.disabled) msg += "\nHid " + std::to_string(r.disabled) + " GL-only core pak(s).";
	return msg;
}

static std::vector<AbstractMenuItem *> buildItems() {
	using namespace mswitch;
	std::vector<AbstractMenuItem *> items;
	const State st = currentState(g_paths);
	const int cores = countInstalledCores(g_paths);
	const int hidden = countHiddenCores(g_paths);

	std::string coreLabel;
	if (cores) coreLabel = std::to_string(cores) + " pak(s) visible";
	else if (hidden) coreLabel = std::to_string(hidden) + " pak(s) hidden";
	else coreLabel = "not installed";

	items.push_back(new MenuItem{ListItemType::Generic, "Frontend", "Which minarch the launcher runs.",
		std::vector<std::any>{std::string("stock")},
		std::vector<std::string>{stateName(st)}});
	items.push_back(new MenuItem{ListItemType::Generic, "Core paks", "Dreamcast/Naomi, N64 and Saturn paks in Emus/h700.",
		std::vector<std::any>{std::string("stock")},
		std::vector<std::string>{coreLabel}});

	if (st == State::Stock) {
		items.push_back(new MenuItem{ListItemType::Button, "Enable minarch-gl",
			"Back up the stock frontend, install the GL frontend and its core paks.",
			[](AbstractMenuItem &) -> InputReactionHint {
				pendingMessage = runEnable();
				return InputReactionHint::NoOp;
			}});
	} else if (st == State::Enabled) {
		items.push_back(new MenuItem{ListItemType::Button, "Reinstall / update",
			"Refresh the installed frontend and core paks from this pak.",
			[](AbstractMenuItem &) -> InputReactionHint {
				pendingMessage = runEnable();
				return InputReactionHint::NoOp;
			}});
		items.push_back(new MenuItem{ListItemType::Button, "Restore stock minarch",
			"Put the stock frontend back and hide the GL-only core paks.",
			[](AbstractMenuItem &) -> InputReactionHint {
				pendingMessage = runRestore();
				return InputReactionHint::NoOp;
			}});
	} else if (st == State::NeedsReinstall) {
		items.push_back(new MenuItem{ListItemType::Button, "Reinstall minarch-gl",
			"The system folder was replaced (NextUI update?). Re-apply minarch-gl.",
			[](AbstractMenuItem &) -> InputReactionHint {
				pendingMessage = runEnable();
				return InputReactionHint::NoOp;
			}});
		items.push_back(new MenuItem{ListItemType::Button, "Restore stock minarch",
			"Give up on GL and go back to the stock frontend.",
			[](AbstractMenuItem &) -> InputReactionHint {
				pendingMessage = runRestore();
				return InputReactionHint::NoOp;
			}});
	}
	return items;
}

static void rebuildMenu(Context &ctx) {
	delete ctx.menu;
	ctx.menu = new MenuList(MenuItemType::List, "Minarch GL", buildItems());
	// MenuList::draw() asserts layout_called; a rebuilt list has to be laid out
	// again or the tool aborts right after the first state change.
	ctx.menu->performLayout(ctx.listRect);
	ctx.dirty = 1;
}

int main(int argc, char *argv[]) {
	g_paths = makePaths();
	if (argc > 1) return cli(g_paths, argv[1]);

	PWR_setCPUSpeed(CPU_SPEED_AUTO);
	Context ctx;
	ctx.screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();

	mkdir(g_paths.stateDir().c_str(), 0755);

	ctx.listRect = {SCALE1(PADDING), SCALE1(PADDING),
	                ctx.screen->w - SCALE1(PADDING * 2), ctx.screen->h - SCALE1(PADDING * 2)};
	ctx.listRect = dy(ctx.listRect, SCALE1(PILL_SIZE));
	ctx.listRect.h -= SCALE1(BUTTON_SIZE);
	rebuildMenu(ctx);

	while (!g_quit) {
		GFX_startFrame();
		PAD_poll();

		const mswitch::State before = mswitch::currentState(g_paths);
		ctx.menu->handleInput(ctx.dirty, g_quit);
		if (mswitch::currentState(g_paths) != before) rebuildMenu(ctx);

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
			GFX_blitButtonGroup(hints, 1, ctx.screen, 1);

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
