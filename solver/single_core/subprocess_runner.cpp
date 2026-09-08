#include "subprocess_runner.h"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace
{
std::string read_file(const std::string &path)
{
	std::ifstream input(path.c_str(), std::ios::binary);
	std::ostringstream contents;
	contents << input.rdbuf();
	return contents.str();
}
} // namespace

#ifdef _WIN32

#include <windows.h>

struct RunningProcess
{
	HANDLE process;
	HANDLE thread;
	std::string stdout_path;
	std::string stderr_path;
};

namespace
{
std::string quote_windows_argument(const std::string &argument)
{
	std::string quoted = "\"";
	size_t backslashes = 0;
	for (size_t i = 0; i < argument.size(); ++i)
	{
		const char ch = argument[i];
		if (ch == '\\')
		{
			++backslashes;
			continue;
		}
		if (ch == '"')
		{
			quoted.append(backslashes * 2 + 1, '\\');
			quoted.push_back('"');
			backslashes = 0;
			continue;
		}
		quoted.append(backslashes, '\\');
		backslashes = 0;
		quoted.push_back(ch);
	}
	quoted.append(backslashes * 2, '\\');
	quoted.push_back('"');
	return quoted;
}

bool create_temp_output(std::string &path, HANDLE &handle)
{
	char directory[MAX_PATH + 1] = {0};
	char filename[MAX_PATH + 1] = {0};
	if (!GetTempPathA(MAX_PATH, directory) ||
		!GetTempFileNameA(directory, "nwl", 0, filename))
		return false;

	SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
	handle = CreateFileA(filename, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		&security, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		DeleteFileA(filename);
		return false;
	}
	path = filename;
	return true;
}
} // namespace

RunningProcess *start_process(const std::string &executable,
	const std::vector<std::string> &arguments, std::string &error)
{
	std::string stdout_path;
	std::string stderr_path;
	HANDLE stdout_handle = INVALID_HANDLE_VALUE;
	HANDLE stderr_handle = INVALID_HANDLE_VALUE;
	if (!create_temp_output(stdout_path, stdout_handle) ||
		!create_temp_output(stderr_path, stderr_handle))
	{
		if (stdout_handle != INVALID_HANDLE_VALUE)
			CloseHandle(stdout_handle);
		if (stderr_handle != INVALID_HANDLE_VALUE)
			CloseHandle(stderr_handle);
		if (!stdout_path.empty())
			DeleteFileA(stdout_path.c_str());
		if (!stderr_path.empty())
			DeleteFileA(stderr_path.c_str());
		error = "failed to create child output files";
		return NULL;
	}

	std::string command = quote_windows_argument(executable);
	for (size_t i = 0; i < arguments.size(); ++i)
		command += " " + quote_windows_argument(arguments[i]);
	std::vector<char> mutable_command(command.begin(), command.end());
	mutable_command.push_back('\0');

	STARTUPINFOA startup;
	PROCESS_INFORMATION process;
	ZeroMemory(&startup, sizeof(startup));
	ZeroMemory(&process, sizeof(process));
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = stdout_handle;
	startup.hStdError = stderr_handle;

	const BOOL created = CreateProcessA(executable.c_str(), mutable_command.data(), NULL,
		NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
	CloseHandle(stdout_handle);
	CloseHandle(stderr_handle);
	if (!created)
	{
		error = "CreateProcess failed with code " + std::to_string(GetLastError());
		DeleteFileA(stdout_path.c_str());
		DeleteFileA(stderr_path.c_str());
		return NULL;
	}

	RunningProcess *running = new RunningProcess;
	running->process = process.hProcess;
	running->thread = process.hThread;
	running->stdout_path = stdout_path;
	running->stderr_path = stderr_path;
	return running;
}

ProcessResult finish_process(RunningProcess *running,
	const std::chrono::steady_clock::time_point &deadline)
{
	ProcessResult result;
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	const long long remaining_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
		deadline - now).count();
	const DWORD wait_milliseconds = remaining_milliseconds > 0
		? static_cast<DWORD>(remaining_milliseconds)
		: 0;
	const DWORD wait_result = WaitForSingleObject(running->process, wait_milliseconds);
	if (wait_result == WAIT_TIMEOUT)
	{
		result.timed_out = true;
		TerminateProcess(running->process, 1);
		WaitForSingleObject(running->process, 5000);
	}
	else if (wait_result == WAIT_FAILED)
	{
		result.error = "WaitForSingleObject failed with code " + std::to_string(GetLastError());
		TerminateProcess(running->process, 1);
		WaitForSingleObject(running->process, 5000);
	}
	DWORD exit_code = 1;
	GetExitCodeProcess(running->process, &exit_code);
	result.return_code = static_cast<int>(exit_code);
	CloseHandle(running->thread);
	CloseHandle(running->process);

	result.stdout_text = read_file(running->stdout_path);
	result.stderr_text = read_file(running->stderr_path);
	DeleteFileA(running->stdout_path.c_str());
	DeleteFileA(running->stderr_path.c_str());
	delete running;
	return result;
}

#else

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct RunningProcess
{
	pid_t child;
	std::string stdout_path;
	std::string stderr_path;
};

RunningProcess *start_process(const std::string &executable,
	const std::vector<std::string> &arguments, std::string &error)
{
	char stdout_template[] = "/tmp/nuwls-out-XXXXXX";
	char stderr_template[] = "/tmp/nuwls-err-XXXXXX";
	const int stdout_fd = mkstemp(stdout_template);
	const int stderr_fd = mkstemp(stderr_template);
	if (stdout_fd < 0 || stderr_fd < 0)
	{
		if (stdout_fd >= 0)
			close(stdout_fd);
		if (stderr_fd >= 0)
			close(stderr_fd);
		if (stdout_fd >= 0)
			std::remove(stdout_template);
		if (stderr_fd >= 0)
			std::remove(stderr_template);
		error = "failed to create child output files";
		return NULL;
	}

	const pid_t child = fork();
	if (child == 0)
	{
		dup2(stdout_fd, STDOUT_FILENO);
		dup2(stderr_fd, STDERR_FILENO);
		close(stdout_fd);
		close(stderr_fd);

		std::vector<char *> argv;
		argv.push_back(const_cast<char *>(executable.c_str()));
		for (size_t i = 0; i < arguments.size(); ++i)
			argv.push_back(const_cast<char *>(arguments[i].c_str()));
		argv.push_back(NULL);
		execv(executable.c_str(), argv.data());
		_exit(127);
	}
	close(stdout_fd);
	close(stderr_fd);
	if (child < 0)
	{
		error = "fork failed";
		std::remove(stdout_template);
		std::remove(stderr_template);
		return NULL;
	}

	RunningProcess *running = new RunningProcess;
	running->child = child;
	running->stdout_path = stdout_template;
	running->stderr_path = stderr_template;
	return running;
}

ProcessResult finish_process(RunningProcess *running,
	const std::chrono::steady_clock::time_point &deadline)
{
	ProcessResult result;
	int status = 0;
	pid_t wait_result = 0;
	while ((wait_result = waitpid(running->child, &status, WNOHANG)) == 0)
	{
		if (std::chrono::steady_clock::now() >= deadline)
		{
			result.timed_out = true;
			kill(running->child, SIGKILL);
			waitpid(running->child, &status, 0);
			break;
		}
		usleep(10000);
	}
	if (wait_result < 0)
		result.error = "waitpid failed";
	else if (WIFEXITED(status))
		result.return_code = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		result.return_code = 128 + WTERMSIG(status);

	result.stdout_text = read_file(running->stdout_path);
	result.stderr_text = read_file(running->stderr_path);
	std::remove(running->stdout_path.c_str());
	std::remove(running->stderr_path.c_str());
	delete running;
	return result;
}

#endif
