/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Sony PlayStation Vita (VitaSDK) platform glue; see vita_platform.h.
 */
#include "vita_platform.h"

#ifdef __vita__

#include "SDL.h"
#include "keyboard.h"          // keydown/mousedown, see the end of vita_swkbd
#include "video.h"             // main_window, video_repeat_last_present()

#include <psp2/io/stat.h>      // sceIoMkdir
#include <psp2/sysmodule.h>    // sceSysmoduleLoadModule, SCE_SYSMODULE_IME
#include <psp2/common_dialog.h>// SceCommonDialogStatus
#include <psp2/ime_dialog.h>   // sceImeDialog*

void vita_platform_init(void)
{
	// app0:data (the read-only data bundled in the VPK) is auto-mounted by the loader,
	// and ux0:data always exists, so we only need to create our writable subfolder.
	// sceIoMkdir no-ops (EEXIST) if it is already there.
	sceIoMkdir(VITA_USER_DIR, 0777);

	// Make sure the IME sysmodule behind the text-entry dialog is resident before
	// the first sceImeDialogInit in vita_swkbd.
	sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
}

// Convert BMP UTF-16 to UTF-8; drop unsupported surrogate pairs.

static void swkbd_utf8_to_utf16(const char *src, SceWChar16 *dst, size_t dst_cap)
{
	size_t n = 0;
	if (src != NULL)
	{
		for (const Uint8 *s = (const Uint8 *)src; *s != '\0' && n + 1 < dst_cap; )
		{
			Uint32 cp;
			if (s[0] < 0x80)
			{
				cp = s[0];
				s += 1;
			}
			else if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80)
			{
				cp = ((Uint32)(s[0] & 0x1f) << 6) | (s[1] & 0x3f);
				s += 2;
			}
			else if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80)
			{
				cp = ((Uint32)(s[0] & 0x0f) << 12) | ((Uint32)(s[1] & 0x3f) << 6) | (s[2] & 0x3f);
				s += 3;
			}
			else  // 4-byte sequence or malformed byte: skip it
			{
				s += 1;
				continue;
			}
			dst[n++] = (SceWChar16)cp;
		}
	}
	dst[n] = 0;
}

// dst_cap counts bytes including the NUL; output is cut at whole characters only.
static void swkbd_utf16_to_utf8(const SceWChar16 *src, char *dst, size_t dst_cap)
{
	size_t n = 0;
	for (size_t i = 0; src[i] != 0; ++i)
	{
		const Uint16 cp = src[i];
		if (cp >= 0xd800 && cp <= 0xdfff)
			continue;  // surrogate half; the game can't render these anyway
		if (cp < 0x80)
		{
			if (n + 1 >= dst_cap)
				break;
			dst[n++] = (char)cp;
		}
		else if (cp < 0x800)
		{
			if (n + 2 >= dst_cap)
				break;
			dst[n++] = (char)(0xc0 | (cp >> 6));
			dst[n++] = (char)(0x80 | (cp & 0x3f));
		}
		else
		{
			if (n + 3 >= dst_cap)
				break;
			dst[n++] = (char)(0xe0 | (cp >> 12));
			dst[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
			dst[n++] = (char)(0x80 | (cp & 0x3f));
		}
	}
	dst[n] = '\0';
}

bool vita_swkbd(char *out, size_t out_size, size_t max_len,
                const char *initial, const char *guide, bool numeric)
{
	if (out == NULL || out_size == 0)
		return false;

	size_t cap = out_size - 1;
	if (max_len > 0 && max_len < cap)
		cap = max_len;
	if (cap == 0)
		return false;
	if (cap > SCE_IME_DIALOG_MAX_TEXT_LENGTH)
		cap = SCE_IME_DIALOG_MAX_TEXT_LENGTH;

	// The modal dialog retains these buffers for its lifetime.
	static SceWChar16 kbTitle[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
	static SceWChar16 kbInitial[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
	static SceWChar16 kbInput[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

	swkbd_utf8_to_utf16(guide, kbTitle, SCE_IME_DIALOG_MAX_TITLE_LENGTH);
	swkbd_utf8_to_utf16(initial, kbInitial, cap + 1);
	SDL_memset(kbInput, 0, sizeof(kbInput));

	// Own the IME natively. SDL_PollEvent otherwise terminates it before this loop finishes cleanup;
	// SDL_RenderPresent still lets the system compositor draw it.
	SceImeDialogParam param;
	sceImeDialogParamInit(&param);
	param.supportedLanguages = 0;
	param.languagesForced = SCE_FALSE;
	param.type = numeric ? SCE_IME_TYPE_NUMBER : SCE_IME_TYPE_DEFAULT;
	param.option = 0;
	param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
	param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
	param.title = kbTitle;
	param.maxTextLength = (SceUInt32)cap;
	param.initialText = kbInitial;
	param.inputTextBuffer = kbInput;

	SceInt32 res = sceImeDialogInit(&param);
	if (res < 0 && numeric)
	{
		// If the number pad is refused, retry as a plain keyboard; every numeric call
		// site re-filters the result anyway.
		param.type = SCE_IME_TYPE_DEFAULT;
		res = sceImeDialogInit(&param);
	}
	if (res < 0)
		return false;

	// Drain stale events so a queued press isn't mistaken for dialog input later.
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) { }

	bool confirmed = false;

	for (int frame = 0; ; ++frame)
	{
		// Present to service the IME; pump SDL only to drain unrelated queued events.
		video_repeat_last_present();
		while (SDL_PollEvent(&ev)) { }

		const SceCommonDialogStatus st = sceImeDialogGetStatus();
		if (st == SCE_COMMON_DIALOG_STATUS_FINISHED)
		{
			SceImeDialogResult result;
			SDL_memset(&result, 0, sizeof(result));
			if (sceImeDialogGetResult(&result) >= 0 && result.button == SCE_IME_DIALOG_BUTTON_ENTER)
				confirmed = true;
			break;
		}
		if (st != SCE_COMMON_DIALOG_STATUS_RUNNING &&
		    (st != SCE_COMMON_DIALOG_STATUS_NONE || frame > 120))
			break;  // error status, or it never came up within ~2s: treat as a cancel

		SDL_Delay(16);
	}

	sceImeDialogTerm();

	while (SDL_PollEvent(&ev)) { }   // drop anything the dialog session left queued

	// The dialog drain can consume the opening tap's release edge; clear held-state latches on exit.
	keydown = false;
	mousedown = false;

	if (confirmed)
		swkbd_utf16_to_utf8(kbInput, out, cap + 1);
	else if (out != initial)  // a cancel leaves the caller's value unchanged
		SDL_strlcpy(out, initial != NULL ? initial : "", out_size);

	return confirmed;
}

void vita_get_output_size(int *w, int *h)
{
	if (w != NULL)
		*w = 960;
	if (h != NULL)
		*h = 544;
}

#endif // __vita__
