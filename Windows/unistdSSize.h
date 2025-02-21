#ifndef _UNISTDSSIZE_H
#define _UNISTDSSIZE_H 1


// Other replacements for Unix functions (e.g., dup2, execve, etc.)

// Define ssize_t based on platform
#ifdef _WIN64
#define ssize_t __int64
#else
#define ssize_t long
#endif




#endif // _UNISTDSSIZE_H#pragma once
