#ifndef SDLUTIL_H
#define SDLUTIL_H

#include <SDL_atomic.h>
#include <SDL_thread.h>
#include <SDL_rwops.h>

#include <string>
#include <iostream>
#include <cstdio>
#include <unistd.h>

struct AtomicFlag
{
	AtomicFlag()
	{
		clear();
	}

	void set()
	{
		SDL_AtomicSet(&atom, 1);
	}

	void clear()
	{
		SDL_AtomicSet(&atom, 0);
	}
    
    void wait()
    {
        while (SDL_AtomicGet(&atom)) {}
    }
    
    void reset()
    {
        wait();
        set();
    }

	operator bool() const
	{
		return SDL_AtomicGet(&atom);
	}

private:
	mutable SDL_atomic_t atom;
};

template<class C, void (C::*func)()>
int __sdlThreadFun(void *obj)
{
	(static_cast<C*>(obj)->*func)();
	return 0;
}

template<class C, void (C::*func)()>
SDL_Thread *createSDLThread(C *obj, const std::string &name = std::string())
{
	#ifdef __vita__
	/* The recovered Vita SDL port leaves pthread_attr_t at its platform
	 * default when SDL_CreateThread() is used. On current VitaSDK that can
	 * produce a 4 KiB native stack, which overflows in libvorbis while the
	 * BGM streaming worker builds its codebooks. Always size mkxp-z helper
	 * threads explicitly; the main RGSS thread already does this separately. */
	constexpr size_t vitaThreadStackSize = 1024 * 1024;
	return SDL_CreateThreadWithStackSize((__sdlThreadFun<C, func>),
	                                     name.c_str(),
	                                     vitaThreadStackSize,
	                                     obj);
	#else
	return SDL_CreateThread((__sdlThreadFun<C, func>), name.c_str(), obj);
	#endif
}

/* On Android, SDL_RWFromFile always opens files from inside
 * the apk asset folder even when a file with same name exists
 * on the physical filesystem. This wrapper attempts to open a
 * real file first before falling back to the assets folder */
static inline
SDL_RWops *RWFromFile(const char *filename,
                      const char *mode)
{
	FILE *f = fopen(filename, mode);

	if (!f)
		return SDL_RWFromFile(filename, mode);

	return SDL_RWFromFP(f, SDL_TRUE);
}

inline bool readFileSDL(const char *path,
                        std::string &out)
{
#ifdef __vita__
    /* SDL_RWFromFP currently loses the length of Vita file streams. Read
     * packaged files through newlib directly when possible, then retain the
     * SDL/asset fallback for paths that are not ordinary files. */
    FILE *direct = std::fopen(path, "rb");
    if (direct) {
        if (std::fseek(direct, 0, SEEK_END) == 0) {
            long size = std::ftell(direct);
            if (size >= 0 && std::fseek(direct, 0, SEEK_SET) == 0) {
                size_t back = out.size();
                out.resize(back + static_cast<size_t>(size));
                size_t read = std::fread(&out[back], 1, static_cast<size_t>(size), direct);
                std::fclose(direct);
                if (read != static_cast<size_t>(size))
                    out.resize(back + read);
                return read == static_cast<size_t>(size);
            }
        }
        std::fclose(direct);
    }
#endif

	SDL_RWops *f = RWFromFile(path, "rb");

	if (!f)
		return false;

	long size = SDL_RWsize(f);
	size_t back = out.size();

	out.resize(back+size);
	size_t read = SDL_RWread(f, &out[back], 1, size);
	SDL_RWclose(f);

	if (read != (size_t) size)
		out.resize(back+read);

	return true;
}

template<size_t bufSize = 248, size_t pbSize = 8>
class SDLRWBuf : public std::streambuf
{
public:
	SDLRWBuf(SDL_RWops *ops)
	    : ops(ops)
	{
		char *end = buf + bufSize + pbSize;
		setg(end, end, end);
	}

private:
	int_type underflow()
	{
		if (!ops)
			return traits_type::eof();

		if (gptr() < egptr())
			return traits_type::to_int_type(*gptr());

		char *base = buf;
		char *start = base;

		if (eback() == base)
		{
			memmove(base, egptr() - pbSize, pbSize);
			start += pbSize;
		}

		size_t n = SDL_RWread(ops, start, 1, bufSize - (start - base));
		if (n == 0)
			return traits_type::eof();

		setg(base, start, start + n);

		return underflow();
	}

	SDL_RWops *ops;
	char buf[bufSize+pbSize];
};

class SDLRWStream
{
public:
	SDLRWStream(const char *filename,
	            const char *mode)
	    : ops(RWFromFile(filename, mode)),
	      buf(ops),
	      s(&buf)
	{}

	~SDLRWStream()
	{
		if (ops)
			SDL_RWclose(ops);
	}

	operator bool() const
	{
		return ops != 0;
	}

	std::istream &stream()
	{
		return s;
	}

private:
	SDL_RWops *ops;
	SDLRWBuf<> buf;
	std::istream s;
};

#endif // SDLUTIL_H
