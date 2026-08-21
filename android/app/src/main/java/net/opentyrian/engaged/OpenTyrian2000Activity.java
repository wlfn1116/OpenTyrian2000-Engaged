package net.opentyrian.engaged;

import org.libsdl.app.SDLActivity;

/**
 * SDL loads its own libraries plus the game in this order; SDL2_net has to be resident
 * before libmain.so binds to it.
 */
public class OpenTyrian2000Activity extends SDLActivity
{
	@Override
	protected String[] getLibraries()
	{
		return new String[] { "SDL2", "SDL2_net", "main" };
	}
}
