#ifndef __QDL_OSCOMPAT_H__
#define __QDL_OSCOMPAT_H__

#include <sys/stat.h>
#include <sys/time.h>

#ifdef _WIN32
    // Windows-specific includes and definitions
    #include <windows.h>
    #include <stdio.h>
    #include <io.h>
    #include <shlwapi.h>

    #define err(exitcode, format, ...) { \
        fprintf(stderr, format, __VA_ARGS__); \
        ExitProcess(exitcode); \
    }
    //#define errx err
    #define OPEN _open
    #define READ _read
    #define WRITE _write
    #define CLOSE _close
    #define LSEEK _lseek
    #define ACCESS _access
    #define STRDUP _strdup
    #define STRTOK strtok_s
    #define SPRINTF sprintf_s
    #define SLEEP(x) Sleep(x)
    #define WARN printf
    #define FSTAT64 _fstat64
    #define STAT64 _stat64
    #define O_BINARY _O_BINARY
    #define O_RDONLY _O_RDONLY
    #define O_RDWR _O_RDWR
    #define O_WRONLY _O_WRONLY
    #define O_CREAT _O_CREAT
    #define O_TRUNC _O_TRUNC
    #define PATH_MAX 256

/*
    struct option  //on linux, it's defined in getopt.h, not on windows
	{
		const char* name;
		//has_arg can't be an enum because some compilers complain about
			//type mismatches in all the code that assumes it is an int.  
		int has_arg;
		int* flag;
		int val;
	};*/
#else
    // Linux-specific includes
    #include <err.h>
    #include <fcntl.h>
    #include <sys/ioctl.h>
    #include <fnmatch.h>
    #include <poll.h>
    #include <termios.h>
    #define OPEN open
    #define READ read
    #define WRITE write
    #define CLOSE close
    #define LSEEK lseek
    #define ACCESS access
    #define STRDUP strdup
    #define STRTOK strtok_r
    #define SPRINTF sprintf
    #define SLEEP(x) usleep((x)*1000)
    #define WARN warn
    #define FSTAT64 fstat
    #define STAT64  stat
    #define O_BINARY 0
#endif


#ifdef _WIN32

    void errx(int exit_code, const char* fmt, ...);
    void timeval_to_filetime(const struct timeval* tv, FILETIME* ft);
    void filetime_to_timeval(const FILETIME* ft, struct timeval* tv);
    void timeradd(const struct timeval* a, const struct timeval* b, struct timeval* res);
	

  
    int OPENAT(const char* ramdump_path, const char* fileName, int ramdump_dir);
    
    void CLOSEHANDLE(int fd);
    
    int PATHMATCH(const char* pattern, const char* filename);
  
    int OPENDIRECTORY(const char* ramdump_path);

    void CLOSERAMDUMPHANDLE(int ramdump_dir);


    /*
void err(int status, const char* format)
{
	fprintf(stderr, "%s: select: %d\n", format, status);

}*/

    char** ListFiles(const char* pattern, int* count);

#else
//Linux and Mac implementation
int OPENAT(const char* ramdump_path, const char* fileName, int ramdump_dir);
    void CLOSEHANDLE(int fd);
    int PATHMATCH(const char* pattern, const char* filename);
    int OPENDIRECTORY(const char* ramdump_path);
    void CLOSERAMDUMPHANDLE(int ramdump_dir);
#endif

#endif
