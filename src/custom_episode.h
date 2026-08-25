/*
 * Custom episodes (*.clv). See doc/notes.md for the format and runtime contract.
 *
 * This program is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later version.
 */
#ifndef CUSTOM_EPISODE_H
#define CUSTOM_EPISODE_H

#include "opentyr.h"

#define CUSTOM_EPISODE_MAX       64
#define CUSTOM_EPISODE_TITLE_LEN 64
#define CUSTOM_EPISODE_FILE_LEN  64

/* Extracted file names; all fit the engine's char[13] buffers. */
#define CUSTOM_EP_LVL_NAME    "custom.lvl"
#define CUSTOM_EP_SCRIPT_NAME "custom.lev"
#define CUSTOM_EP_CUBES_NAME  "custom.cub"

/* Returns "<user directory>/custom_levels". */
const char *custom_episode_dir(void);

/* Migrates loose containers and rebuilds the list. */
void customEpisodeScan(void);

int customEpisodeCount(void);
const char *customEpisodeTitle(int index);
const char *customEpisodeAuthor(int index);
const char *customEpisodeFile(int index);      /* file name only */
JE_byte customEpisodeBase(int index);
Uint32 customEpisodeSize(int index);
int customEpisodeFindByFile(const char *fileName);

/* Returns -1, false, or "" while a stock episode is active. */
bool customEpisodeActive(void);
int customEpisodeCurrent(void);
const char *customEpisodeActiveTitle(void);
const char *customEpisodeActiveFile(void);

/* True during JE_initEpisodeCustom's inner JE_initEpisode call. */
bool customEpisodeActivating(void);

/* Extracts and loads a container. Failure leaves the stock state intact. */
bool JE_initEpisodeCustom(int index);

/* Leaves custom mode and forces the next stock episode load. */
void customEpisodeDeactivate(void);

/* Whole-container transfer helpers. SaveDownloaded returns the list index. */
Uint8 *customEpisodeReadWhole(int index, Uint32 *lenOut);
int customEpisodeSaveDownloaded(const char *fileName, const Uint8 *data, Uint32 len);

/* Accepts a plain *.clv file name without path components. */
bool customEpisodeFileNameValid(const char *name);

/* Returns the byte size and FNV-1a hash used for network identity. */
bool customEpisodeIdentity(int index, Uint32 *sizeOut, Uint32 *hashOut);

/* Includes invalid containers and an empty custom_levels directory. */
bool customEpisodeAnyPresent(void);

/* Deletes containers, extracted files, and the directory when empty. */
void customEpisodeClearAll(void);

#endif /* CUSTOM_EPISODE_H */
