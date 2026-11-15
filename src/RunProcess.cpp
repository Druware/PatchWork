#include "RunProcess.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


int
RunProcess(const char* const argv[], std::string& out, std::string& err)
{
	int outPipe[2];
	int errPipe[2];
	if (pipe(outPipe) != 0)
		return -1;
	if (pipe(errPipe) != 0) {
		close(outPipe[0]);
		close(outPipe[1]);
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(outPipe[0]);
		close(outPipe[1]);
		close(errPipe[0]);
		close(errPipe[1]);
		return -1;
	}

	if (pid == 0) {
		int devNull = open("/dev/null", O_RDONLY);
		if (devNull >= 0)
			dup2(devNull, STDIN_FILENO);
		dup2(outPipe[1], STDOUT_FILENO);
		dup2(errPipe[1], STDERR_FILENO);
		close(outPipe[0]);
		close(outPipe[1]);
		close(errPipe[0]);
		close(errPipe[1]);
		execvp(argv[0], const_cast<char* const*>(argv));
		const char* reason = strerror(errno);
		write(STDERR_FILENO, argv[0], strlen(argv[0]));
		write(STDERR_FILENO, ": ", 2);
		write(STDERR_FILENO, reason, strlen(reason));
		write(STDERR_FILENO, "\n", 1);
		_exit(127);
	}

	close(outPipe[1]);
	close(errPipe[1]);

	// Drain both pipes together: reading one to EOF first could deadlock
	// once the child fills the other pipe's buffer.
	struct pollfd fds[2] = {
		{ outPipe[0], POLLIN, 0 },
		{ errPipe[0], POLLIN, 0 }
	};
	std::string* sinks[2] = { &out, &err };
	int openCount = 2;
	while (openCount > 0) {
		if (poll(fds, 2, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (int i = 0; i < 2; i++) {
			if (fds[i].fd < 0 || fds[i].revents == 0)
				continue;
			char buffer[4096];
			ssize_t count = read(fds[i].fd, buffer, sizeof(buffer));
			if (count > 0)
				sinks[i]->append(buffer, count);
			else if (count == 0 || errno != EINTR) {
				close(fds[i].fd);
				fds[i].fd = -1;
				openCount--;
			}
		}
	}
	for (int i = 0; i < 2; i++) {
		if (fds[i].fd >= 0)
			close(fds[i].fd);
	}

	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
