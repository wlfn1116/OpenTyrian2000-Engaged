package net.opentyrian.engaged;

import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import org.libsdl.app.SDLActivity;

public class OpenTyrian2000Activity extends SDLActivity
{
	/**
	 * SDL loads its own libraries plus the game in this order; SDL2_net has to be resident
	 * before libmain.so binds to it.
	 */
	@Override
	protected String[] getLibraries()
	{
		return new String[] { "SDL2", "SDL2_net", "main" };
	}

	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		hideSystemBars();
	}

	@Override
	public void onWindowFocusChanged(boolean hasFocus)
	{
		super.onWindowFocusChanged(hasFocus);

		// The bars come back on their own after the soft keyboard, a notification pull, or a
		// task switch, so the request has to be renewed rather than made once.
		if (hasFocus)
			hideSystemBars();
	}

	/**
	 * Immersive fullscreen, which nothing else in the stack asks for. The theme's
	 * windowFullscreen does not survive Android 15: an app targeting SDK 35 is edge to edge
	 * by default and keeps the bars as overlays over the frame. SDL hides them only for a
	 * window it was told to make fullscreen, and this game leaves the window alone on a
	 * handheld so the driver keeps owning its size.
	 */
	private void hideSystemBars()
	{
		Window window = getWindow();

		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
		{
			// Needed on API 30 through 34 to get out from under the bars; SDK 35 is already
			// edge to edge and disables this call.
			window.setDecorFitsSystemWindows(false);

			WindowInsetsController insets = window.getInsetsController();
			if (insets != null)
			{
				insets.hide(WindowInsets.Type.systemBars());
				// An edge swipe then shows the bars over the frame for a few seconds instead
				// of ending fullscreen, so a stray gesture in a level cannot leave them up.
				insets.setSystemBarsBehavior(
					WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
			}
		}
		else
		{
			// setSystemUiVisibility is the only route before API 30, and IMMERSIVE_STICKY is
			// the same bargain as BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE above.
			window.getDecorView().setSystemUiVisibility(
				View.SYSTEM_UI_FLAG_LAYOUT_STABLE
				| View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
				| View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
				| View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
				| View.SYSTEM_UI_FLAG_FULLSCREEN
				| View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
		}
	}
}
