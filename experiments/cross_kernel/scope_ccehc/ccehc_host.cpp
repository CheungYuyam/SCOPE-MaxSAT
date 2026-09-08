#include "subprocess_runner.h"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
const double FINALIZATION_GRACE_SECONDS = 5.0;
const double DEFAULT_PROBABILITY = 0.279;
const double DEFAULT_SMOOTH_PROBABILITY = 0.085;

std::string sibling_executable_path(const char *launcher, const char *basename)
{
	const std::string path(launcher);
	const size_t separator = path.find_last_of("/\\");
	const std::string directory = separator == std::string::npos
		? "." : path.substr(0, separator);
#ifdef _WIN32
	return directory + "\\" + basename + ".exe";
#else
	return directory + "/" + basename;
#endif
}

bool parse_double(const char *text, double &value)
{
	char *end = NULL;
	value = std::strtod(text, &end);
	return end && *end == '\0';
}
} // namespace

int main(int argc, char *argv[])
{
	const Clock::time_point started = Clock::now();
	std::cout << "c build ccehc-host-wall-clock-wrapper" << std::endl;
	if (argc < 4)
	{
		std::cout << "usage: " << argv[0]
			<< " instance.wcnf seed cutoff_seconds [-ccehc_p value] [-ccehc_sp value]"
			<< std::endl;
		return 1;
	}

	double cutoff = 0.0;
	if (!parse_double(argv[3], cutoff) || cutoff <= 0.0)
	{
		std::cout << "cutoff_seconds must be positive" << std::endl;
		return 1;
	}
	double probability = DEFAULT_PROBABILITY;
	double smooth_probability = DEFAULT_SMOOTH_PROBABILITY;
	for (int i = 4; i < argc; ++i)
	{
		const std::string option(argv[i]);
		if ((option == "-ccehc_p" || option == "-ccehc_sp") && i + 1 < argc)
		{
			double value = 0.0;
			if (!parse_double(argv[++i], value) || value < 0.0 || value > 1.0)
			{
				std::cout << option << " must be in [0,1]" << std::endl;
				return 1;
			}
			if (option == "-ccehc_p")
				probability = value;
			else
				smooth_probability = value;
		}
		else
		{
			std::cout << "unknown or incomplete option: " << option << std::endl;
			return 1;
		}
	}

	std::ostringstream cutoff_argument;
	cutoff_argument << std::fixed << std::setprecision(6) << cutoff;
	std::ostringstream probability_argument;
	probability_argument << std::setprecision(17) << probability;
	std::ostringstream smooth_argument;
	smooth_argument << std::setprecision(17) << smooth_probability;
	std::vector<std::string> arguments;
	arguments.push_back("-inst");
	arguments.push_back(argv[1]);
	arguments.push_back("-seed");
	arguments.push_back(argv[2]);
	arguments.push_back("-t");
	arguments.push_back(cutoff_argument.str());
	arguments.push_back("-p");
	arguments.push_back(probability_argument.str());
	arguments.push_back("-sp");
	arguments.push_back(smooth_argument.str());

	std::string launch_error;
	RunningProcess *process = start_process(
		sibling_executable_path(argv[0], "ccehc-seeded"), arguments, launch_error);
	ProcessResult result;
	if (process)
	{
		const Clock::time_point deadline = started +
			std::chrono::duration_cast<Clock::duration>(
				std::chrono::duration<double>(cutoff + FINALIZATION_GRACE_SECONDS));
		result = finish_process(process, deadline);
	}
	else
		result.error = launch_error;

	std::cout << "c host_schedule ccehc_only" << std::endl;
	std::cout << "c concurrent_search_processes 1" << std::endl;
	std::cout << result.stdout_text;
	if (!result.stderr_text.empty())
		std::cout << "c host-core stderr " << result.stderr_text << std::endl;
	if (!result.error.empty())
		std::cout << "c host-core error " << result.error << std::endl;
	if (result.timed_out)
		std::cout << "c host-core timed_out 1" << std::endl;
	std::cout << "c total elapsed_time " << std::fixed << std::setprecision(6)
		<< std::chrono::duration<double>(Clock::now() - started).count() << std::endl;
	return result.return_code;
}
