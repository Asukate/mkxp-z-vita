#pragma once

#include <string>

class Bitmap;

/* RPGXPVideoDecoder.dll on the Vita. Some XP games (The Music Box) play MP4
 * videos through this Windows DLL, called with Win32API. The same calls are
 * answered here with the Vita's hardware H.264/AAC player: frames are drawn
 * into the game's Bitmap, sound goes through the engine's OpenAL mixer.
 * Return values follow the DLL's error codes. */
namespace VitaVideoDecoder
{
enum Result
{
	Success = 0,
	VideoNotFinished = 1,
	DecoderInstanceAlreadyCreated = 2,
	DecoderNotCreated = 3,
	FileNotFound = 4,
	InvalidFile = 5,
	BitmapIsDisposed = 6,
	FailedToFindVideoStream = 7,
	FailedToFindAudioStream = 8,
	FailedToOpenAudioDevice = 9,
	InternalError = 10,
};

/* path is relative to the game folder; volume is 0..128 like SDL's mixer. */
int createContext(const std::string &path, int volume, Bitmap *target);
int startRender();
/* Draws the newest decoded frame; VideoNotFinished while playing. */
int getVideoState();
int setVolume(int volume);
int closeContext();
int wasBadTermination();
int getInternalError();
const char *getInternalErrorMessage();
/* The Bitmap the open context draws into, or null. */
Bitmap *target();
}
