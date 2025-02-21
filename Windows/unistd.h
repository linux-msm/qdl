#ifndef _UNISTD_H
#define _UNISTD_H 1

#include <stdlib.h>
#include <io.h>
#include <getopt.h> // getopt at: [^1^][6]
#include <process.h> // for getpid() and the exec..() family
#include <direct.h> // for _getcwd() and _chdir()

#define srandom srand
#define random rand

// Values for the second argument to access.
#define R_OK 4 // Test for read permission.
#define W_OK 2 // Test for write permission.
#define F_OK 0 // Test for existence.

// Other replacements for Unix functions (e.g., dup2, execve, etc.)

// Define ssize_t based on platform
#ifdef _WIN64
#define ssize_t __int64
#else
#define ssize_t long
#endif

// File descriptors
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Types
typedef __int8 int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
// ...

#endif // _UNISTD_H#pragma once
