#ifndef SUBPROCESS_RUNNER_H
#define SUBPROCESS_RUNNER_H

#include <chrono>
#include <string>
#include <vector>

struct ProcessResult
{
	int return_code;
	bool timed_out;
	std::string stdout_text;
	std::string stderr_text;
	std::string error;

	ProcessResult()
		: return_code(-1), timed_out(false)
	{
	}
};

struct RunningProcess;

RunningProcess *start_process(const std::string &executable,
	const std::vector<std::string> &arguments, std::string &error);

ProcessResult finish_process(RunningProcess *process,
	const std::chrono::steady_clock::time_point &deadline);

#endif
