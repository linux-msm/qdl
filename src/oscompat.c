// SPDX-License-Identifier: BSD-3-Clause
#include <errno.h>


#ifdef _WIN32

#include <stdio.h>

#include <stdarg.h>
#include <windows.h>

void timeradd(const struct timeval *a, const struct timeval *b, struct timeval *result)
{
	result->tv_sec = a->tv_sec + b->tv_sec;
	result->tv_usec = a->tv_usec + b->tv_usec;
	if (result->tv_usec >= 1000000) {
		result->tv_sec += 1;
		result->tv_usec -= 1000000;
	}
}

void err(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", __progname);
	if (fmt) {
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, ": ");
	}
	fprintf(stderr, "%s\n", strerror(errno));
	va_end(ap);
	exit(eval);
}

void errx(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", __progname);
	if (fmt)
		vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(eval);
}

void warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", __progname);
	if (fmt) {
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, ": ");
	}
	fprintf(stderr, "%s\n", strerror(errno));
	va_end(ap);
}

void warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", __progname);
	if (fmt)
		vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

/* Windows implementation of qdl_run_capture */
int qdl_run_capture(char *const argv[], int timeout_ms, struct qdl_process_result *result)
{
	HANDLE hPipeRead, hPipeWrite;
	SECURITY_ATTRIBUTES sa;
	PROCESS_INFORMATION pi;
	STARTUPINFOW si;
	DWORD dwRead, dwWaitResult;
	char *buffer = NULL;
	size_t buffer_size = 0;
	size_t buffer_len = 0;
	DWORD timeout = timeout_ms > 0 ? timeout_ms : INFINITE;
	int i, cmd_len;
	wchar_t *cmd_line = NULL;
	wchar_t *exe_path = NULL;

	memset(result, 0, sizeof(*result));

	if (!argv || !argv[0]) {
		errno = EINVAL;
		return -1;
	}

	/* Create pipe for stdout capture with larger buffer */
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	if (!CreatePipe(&hPipeRead, &hPipeWrite, &sa, 65536)) {
		errno = ENOMEM;
		return -1;
	}

	/* Convert argv[0] to wide char */
	int len = MultiByteToWideChar(CP_UTF8, 0, argv[0], -1, NULL, 0);
	if (len <= 0) {
		CloseHandle(hPipeRead);
		CloseHandle(hPipeWrite);
		errno = EINVAL;
		return -1;
	}

	exe_path = malloc(len * sizeof(wchar_t));
	if (!exe_path) {
		CloseHandle(hPipeRead);
		CloseHandle(hPipeWrite);
		errno = ENOMEM;
		return -1;
	}

	MultiByteToWideChar(CP_UTF8, 0, argv[0], -1, exe_path, len);

	/* Build command line from argv - like QIL does */
	cmd_len = 0;
	for (i = 0; argv[i]; i++) {
		cmd_len += strlen(argv[i]) + 3; /* for quotes and space */
	}

	cmd_line = malloc((cmd_len + 256) * sizeof(wchar_t)); /* extra space for "cmd /c " */
	if (!cmd_line) {
		free(exe_path);
		CloseHandle(hPipeRead);
		CloseHandle(hPipeWrite);
		errno = ENOMEM;
		return -1;
	}

	wchar_t *cmd_ptr = cmd_line;

	/* Start with "cmd /c " like QIL does */
	wcscpy(cmd_ptr, L"cmd /c ");
	cmd_ptr += wcslen(L"cmd /c ");

	for (i = 0; argv[i]; i++) {
		int arg_len = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, NULL, 0);
		if (arg_len <= 0) {
			free(cmd_line);
			free(exe_path);
			CloseHandle(hPipeRead);
			CloseHandle(hPipeWrite);
			errno = EINVAL;
			return -1;
		}

		wchar_t *arg_wide = malloc(arg_len * sizeof(wchar_t));
		if (!arg_wide) {
			free(cmd_line);
			free(exe_path);
			CloseHandle(hPipeRead);
			CloseHandle(hPipeWrite);
			errno = ENOMEM;
			return -1;
		}

		MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, arg_wide, arg_len);

		/* Convert forward slashes to backslashes for Windows paths */
		for (wchar_t *p = arg_wide; *p; p++) {
			if (*p == L'/')
				*p = L'\\';
		}

		if (i > 0)
			*cmd_ptr++ = L' ';

		/* Copy argument without quoting */
		wcscpy(cmd_ptr, arg_wide);
		cmd_ptr += wcslen(arg_wide);

		free(arg_wide);
	}

	*cmd_ptr = L'\0';

	/* Setup process info */
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = hPipeWrite;
	si.hStdError = hPipeWrite;

	memset(&pi, 0, sizeof(pi));

	/* Create process - EXACTLY like QIL: NULL executable, full cmd line with "cmd /c" */
	if (!CreateProcessW(NULL, cmd_line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
		DWORD err = GetLastError();
		LPSTR errMsg = NULL;
		FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			NULL, err, 0, (LPSTR)&errMsg, 0, NULL);
		fprintf(stderr, "CreateProcessW failed: error %lu - %s\n", err, errMsg ? errMsg : "unknown");
		if (errMsg) LocalFree(errMsg);
		free(cmd_line);
		free(exe_path);
		CloseHandle(hPipeRead);
		CloseHandle(hPipeWrite);
		errno = ENOENT;
		return -1;
	}

	free(cmd_line);
	free(exe_path);
	CloseHandle(hPipeWrite);

	/* Wait for process with timeout */
	dwWaitResult = WaitForSingleObject(pi.hProcess, timeout);

	if (dwWaitResult == WAIT_TIMEOUT) {
		TerminateProcess(pi.hProcess, 1);
		result->timed_out = true;
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		CloseHandle(hPipeRead);
		return -1;
	}

	if (dwWaitResult != WAIT_OBJECT_0) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		CloseHandle(hPipeRead);
		errno = EIO;
		return -1;
	}

	/* Get exit code */
	DWORD exit_code;
	if (!GetExitCodeProcess(pi.hProcess, &exit_code)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		CloseHandle(hPipeRead);
		errno = EIO;
		return -1;
	}

	result->exit_code = (int)exit_code;

	/* Read stdout */
	buffer_size = 4096;
	buffer = malloc(buffer_size);
	if (!buffer) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		CloseHandle(hPipeRead);
		errno = ENOMEM;
		return -1;
	}

	while (ReadFile(hPipeRead, buffer + buffer_len, buffer_size - buffer_len - 1, &dwRead, NULL)) {
		if (dwRead == 0)
			break;

		buffer_len += dwRead;

		if (buffer_len >= buffer_size - 1) {
			char *new_buffer = realloc(buffer, buffer_size * 2);
			if (!new_buffer) {
				free(buffer);
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
				CloseHandle(hPipeRead);
				errno = ENOMEM;
				return -1;
			}
			buffer = new_buffer;
			buffer_size *= 2;
		}
	}

	buffer[buffer_len] = '\0';
	result->stdout_buf = buffer;
	result->stdout_len = buffer_len;

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	CloseHandle(hPipeRead);

	return result->exit_code == 0 ? 0 : -1;
}

void qdl_process_result_free(struct qdl_process_result *result)
{
	if (result) {
		free(result->stdout_buf);
		result->stdout_buf = NULL;
		result->stdout_len = 0;
	}
}

#else /* POSIX */

#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>

/* POSIX implementation of qdl_run_capture */
int qdl_run_capture(char *const argv[], int timeout_ms, struct qdl_process_result *result)
{
	pid_t pid;
	int pipefd[2];
	char *buffer = NULL;
	size_t buffer_size = 4096;
	size_t buffer_len = 0;
	ssize_t nread;
	int status;
	struct timespec deadline, now;
	long timeout_ns;

	memset(result, 0, sizeof(*result));

	if (!argv || !argv[0]) {
		errno = EINVAL;
		return -1;
	}

	if (pipe(pipefd) < 0) {
		errno = ENOMEM;
		return -1;
	}

	buffer = malloc(buffer_size);
	if (!buffer) {
		close(pipefd[0]);
		close(pipefd[1]);
		errno = ENOMEM;
		return -1;
	}

	/* Calculate deadline if timeout is specified */
	if (timeout_ms > 0) {
		clock_gettime(CLOCK_MONOTONIC, &deadline);
		timeout_ns = (long)timeout_ms * 1000000;
		deadline.tv_sec += timeout_ns / 1000000000;
		deadline.tv_nsec += timeout_ns % 1000000000;
		if (deadline.tv_nsec >= 1000000000) {
			deadline.tv_sec++;
			deadline.tv_nsec -= 1000000000;
		}
	}

	pid = fork();
	if (pid < 0) {
		free(buffer);
		close(pipefd[0]);
		close(pipefd[1]);
		errno = ENOMEM;
		return -1;
	}

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);

		execvp(argv[0], argv);
		exit(127); /* exec failed */
	}

	/* Parent process */
	close(pipefd[1]);

	/* Read from pipe with timeout */
	while (1) {
		fd_set readfds;
		struct timeval tv, *tvp = NULL;

		FD_ZERO(&readfds);
		FD_SET(pipefd[0], &readfds);

		if (timeout_ms > 0) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			long remaining_ns = (deadline.tv_sec - now.tv_sec) * 1000000000 +
					    (deadline.tv_nsec - now.tv_nsec);

			if (remaining_ns <= 0) {
				/* Timeout */
				kill(pid, SIGKILL);
				waitpid(pid, &status, 0);
				result->timed_out = true;
				close(pipefd[0]);
				free(buffer);
				return -1;
			}

			tv.tv_sec = remaining_ns / 1000000000;
			tv.tv_usec = (remaining_ns % 1000000000) / 1000;
			tvp = &tv;
		}

		int ret = select(pipefd[0] + 1, &readfds, NULL, NULL, tvp);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			close(pipefd[0]);
			free(buffer);
			return -1;
		}

		if (ret == 0) {
			/* Timeout */
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			result->timed_out = true;
			close(pipefd[0]);
			free(buffer);
			return -1;
		}

		nread = read(pipefd[0], buffer + buffer_len, buffer_size - buffer_len - 1);
		if (nread < 0) {
			if (errno == EINTR)
				continue;
			close(pipefd[0]);
			free(buffer);
			return -1;
		}

		if (nread == 0)
			break;

		buffer_len += nread;

		if (buffer_len >= buffer_size - 1) {
			char *new_buffer = realloc(buffer, buffer_size * 2);
			if (!new_buffer) {
				close(pipefd[0]);
				free(buffer);
				kill(pid, SIGKILL);
				waitpid(pid, &status, 0);
				errno = ENOMEM;
				return -1;
			}
			buffer = new_buffer;
			buffer_size *= 2;
		}
	}

	close(pipefd[0]);

	/* Wait for child to finish */
	if (waitpid(pid, &status, 0) < 0) {
		free(buffer);
		return -1;
	}

	buffer[buffer_len] = '\0';
	result->stdout_buf = buffer;
	result->stdout_len = buffer_len;

	if (WIFEXITED(status)) {
		result->exit_code = WEXITSTATUS(status);
	} else {
		result->exit_code = -1;
	}

	return result->exit_code == 0 ? 0 : -1;
}

void qdl_process_result_free(struct qdl_process_result *result)
{
	if (result) {
		free(result->stdout_buf);
		result->stdout_buf = NULL;
		result->stdout_len = 0;
	}
}

#endif /* _WIN32 */

