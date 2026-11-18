#pragma once

#include <stdbool.h>

void Game_open(char* path);
void Game_close(void);

/* What one Game_changeDisc() call did, for the caller's single log line.  The
 * three step flags are what the tray/image calls answered; a step that was never
 * reached stays false.  error != NULL means the swap did not happen (or did not
 * complete); rolled_back says Game_close()/Game_open() were undone so the
 * frontend's idea of the content still matches the disc in the drive. */
typedef struct {
	bool eject;        // tray open (true when it already was)
	bool replace;      // image in slot 0 replaced / target index set
	bool close;        // tray closed again -- this is what loads the media
	bool noop;         // that disc was already in the drive, nothing ran
	bool rolled_back;  // close/open undone after a failure
	const char* via;   // "replace" (single image) or "set" (playlist)
	const char* error; // NULL on success, else a short static reason
} GameDiscSwap;

bool Game_changeDisc(char* path, GameDiscSwap* steps);
int extract_zip(char** extensions);
