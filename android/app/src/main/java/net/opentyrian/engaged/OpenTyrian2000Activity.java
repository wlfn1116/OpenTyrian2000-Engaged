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
	/** Load SDL2_net before libmain.so binds to it. */
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

		// System bars return after focus changes, so hide them again.
		if (hasFocus)
			hideSystemBars();
	}

	/** Keep the game edge-to-edge with transient system bars across supported API levels. */
	private void hideSystemBars()
	{
		Window window = getWindow();

		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
		{
			// API 30 through 34 need this; API 35 is already edge-to-edge.
			window.setDecorFitsSystemWindows(false);

			WindowInsetsController insets = window.getInsetsController();
			if (insets != null)
			{
				insets.hide(WindowInsets.Type.systemBars());
				// Let edge swipes reveal bars temporarily without leaving fullscreen.
				insets.setSystemBarsBehavior(
					WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
			}
		}
		else
		{
			// Before API 30, IMMERSIVE_STICKY provides the same transient-bar behavior.
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
