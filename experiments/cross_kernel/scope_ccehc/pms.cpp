#include "hard_sat_fallback.h"
#include "subprocess_runner.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define SCOPE_BUILD_ID "scope-maxsat-ccehc-cross-kernel"

namespace
{
using Clock = std::chrono::steady_clock;
const double FINALIZATION_GRACE_SECONDS = 5.0;
const double DEFAULT_CCEHC_PROBABILITY = 0.279;
const double DEFAULT_CCEHC_SMOOTH_PROBABILITY = 0.085;

struct KernelOutput
{
	bool valid;
	long long cost;
	double found_time;
	std::vector<unsigned char> assignment;
	std::string verification_error;

	KernelOutput() : valid(false), cost(0), found_time(0.0) {}
};

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

bool parse_options(int argc, char *argv[], double &probability,
	double &smooth_probability, std::string &error)
{
	probability = DEFAULT_CCEHC_PROBABILITY;
	smooth_probability = DEFAULT_CCEHC_SMOOTH_PROBABILITY;
	for (int i = 4; i < argc; ++i)
	{
		const std::string option(argv[i]);
		if ((option == "-ccehc_p" || option == "-ccehc_sp") && i + 1 < argc)
		{
			double value = 0.0;
			if (!parse_double(argv[++i], value) || value < 0.0 || value > 1.0)
			{
				error = option + " must be in [0,1]";
				return false;
			}
			if (option == "-ccehc_p")
				probability = value;
			else
				smooth_probability = value;
		}
		else
		{
			error = "unknown or incomplete option: " + option;
			return false;
		}
	}
	return true;
}

bool write_seed(const HardSatFallbackResult &fallback, std::string &path,
	std::string &error)
{
#ifdef _WIN32
	const char *directory = std::getenv("TEMP");
	const char separator = '\\';
#else
	const char *directory = std::getenv("TMPDIR");
	const char separator = '/';
#endif
	if (!directory || !directory[0])
		directory = ".";
	const long long stamp = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	path = directory;
	if (!path.empty() && path[path.size() - 1] != '/' && path[path.size() - 1] != '\\')
		path.push_back(separator);
	path += "scope-ccehc-seed-" + std::to_string(stamp) + ".txt";

	std::ofstream output(path.c_str(), std::ios::binary);
	if (!output)
	{
		error = "cannot create CCEHC seed file";
		return false;
	}
	for (size_t variable = 1; variable < fallback.assignment.size(); ++variable)
		output << (fallback.assignment[variable] ? '1' : '0');
	output << '\n';
	if (!output)
	{
		error = "cannot write CCEHC seed file";
		output.close();
		std::remove(path.c_str());
		path.clear();
		return false;
	}
	return true;
}

KernelOutput parse_and_verify_kernel_output(const char *instance,
	const std::string &text, const Clock::time_point &verification_deadline)
{
	KernelOutput result;
	bool internally_verified = false;
	bool has_assignment = false;
	bool has_objective = false;
	std::string assignment_bits;
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line == "c final internal_verified 1")
			internally_verified = true;
		else if (line.size() > 2 && line[0] == 'v' && line[1] == ' ')
		{
			assignment_bits = line.substr(2);
			has_assignment = !assignment_bits.empty();
			for (size_t i = 0; i < assignment_bits.size(); ++i)
				if (assignment_bits[i] != '0' && assignment_bits[i] != '1')
					has_assignment = false;
		}
		else if (line.size() > 2 && line[0] == 'o' && line[1] == ' ')
		{
			std::istringstream fields(line.substr(2));
			long long cost = 0;
			double found_time = 0.0;
			if (fields >> cost >> found_time)
			{
				if (!has_objective || cost < result.cost ||
					(cost == result.cost && found_time < result.found_time))
				{
					result.cost = cost;
					result.found_time = found_time;
				}
				has_objective = true;
			}
		}
	}

	if (!internally_verified || !has_assignment || !has_objective)
	{
		result.verification_error = "CCEHC output is incomplete or not internally verified";
		return result;
	}
	result.assignment.assign(assignment_bits.size() + 1, 0);
	for (size_t i = 0; i < assignment_bits.size(); ++i)
		result.assignment[i + 1] = assignment_bits[i] == '1' ? 1 : 0;
	long long verified_cost = 0;
	if (!verify_wcnf_assignment(instance, result.assignment, verified_cost,
		verification_deadline, result.verification_error))
		return result;
	if (verified_cost != result.cost)
	{
		result.verification_error = "CCEHC reported cost differs from controller verification";
		return result;
	}
	result.valid = true;
	return result;
}

void print_with_adjusted_times(const std::string &text, double prefix_seconds)
{
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.size() > 2 && line[0] == 'o' && line[1] == ' ')
		{
			std::istringstream fields(line.substr(2));
			long long cost = 0;
			double child_time = 0.0;
			if (fields >> cost >> child_time)
			{
				std::cout << "o " << cost << " " << std::fixed << std::setprecision(6)
					<< prefix_seconds + child_time << std::endl;
				continue;
			}
		}
		std::cout << line << std::endl;
	}
}
} // namespace

int main(int argc, char *argv[])
{
	const Clock::time_point started = Clock::now();
	std::cout << "c build " << SCOPE_BUILD_ID << std::endl;
	if (argc < 4)
	{
		std::cout << "usage: " << argv[0]
			<< " instance.wcnf seed cutoff_seconds [-ccehc_p value] [-ccehc_sp value]"
			<< std::endl;
		return 1;
	}

	const int seed = std::atoi(argv[2]);
	double cutoff = 0.0;
	if (!parse_double(argv[3], cutoff) || cutoff <= 0.0)
	{
		std::cout << "cutoff_seconds must be positive" << std::endl;
		return 1;
	}
	double probability = 0.0;
	double smooth_probability = 0.0;
	std::string option_error;
	if (!parse_options(argc, argv, probability, smooth_probability, option_error))
	{
		std::cout << option_error << std::endl;
		return 1;
	}

	const Clock::time_point search_deadline = started +
		std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(cutoff));
	const Clock::time_point finalization_deadline = search_deadline +
		std::chrono::duration_cast<Clock::duration>(
			std::chrono::duration<double>(FINALIZATION_GRACE_SECONDS));

	const double fallback_started_seconds =
		std::chrono::duration<double>(Clock::now() - started).count();
	const HardSatFallbackResult fallback = find_hard_sat_fallback(
		argv[1], search_deadline, finalization_deadline);
	const double fallback_event_seconds = fallback_started_seconds + fallback.elapsed_seconds;

	ProcessResult refinement_child;
	double refinement_started_seconds = 0.0;
	bool refinement_started = false;
	std::string seed_path;
	std::string seed_error;
	if (fallback.found && Clock::now() < search_deadline &&
		write_seed(fallback, seed_path, seed_error))
	{
		const double remaining_seconds =
			std::chrono::duration<double>(search_deadline - Clock::now()).count();
		std::ostringstream cutoff_argument;
		cutoff_argument << std::fixed << std::setprecision(6) << remaining_seconds;
		std::ostringstream probability_argument;
		probability_argument << std::setprecision(17) << probability;
		std::ostringstream smooth_argument;
		smooth_argument << std::setprecision(17) << smooth_probability;

		std::vector<std::string> arguments;
		arguments.push_back("-inst");
		arguments.push_back(argv[1]);
		arguments.push_back("-seed");
		arguments.push_back(std::to_string(seed));
		arguments.push_back("-t");
		arguments.push_back(cutoff_argument.str());
		arguments.push_back("-p");
		arguments.push_back(probability_argument.str());
		arguments.push_back("-sp");
		arguments.push_back(smooth_argument.str());
		arguments.push_back("-seed-file");
		arguments.push_back(seed_path);

		refinement_started_seconds =
			std::chrono::duration<double>(Clock::now() - started).count();
		std::string launch_error;
		RunningProcess *process = start_process(
			sibling_executable_path(argv[0], "ccehc-seeded"), arguments, launch_error);
		if (process)
		{
			refinement_started = true;
			refinement_child = finish_process(process, finalization_deadline);
		}
		else
			refinement_child.error = launch_error;
	}
	if (!seed_path.empty())
		std::remove(seed_path.c_str());

	const KernelOutput refinement = parse_and_verify_kernel_output(
		argv[1], refinement_child.stdout_text, finalization_deadline);
	const bool choose_refinement = refinement.valid &&
		(!fallback.found || refinement.cost < fallback.cost);
	const bool have_solution = choose_refinement || fallback.found;
	const long long final_cost = choose_refinement ? refinement.cost : fallback.cost;
	const double paper_table_time = choose_refinement
		? refinement_started_seconds + refinement.found_time
		: fallback_event_seconds;

	std::cout << "c hard_sat_fallback elapsed_time " << std::fixed
		<< std::setprecision(6) << fallback.elapsed_seconds << " success "
		<< (fallback.found ? 1 : 0) << std::endl;
	std::cout << "c single_core_schedule hard_sat_then_ccehc" << std::endl;
	std::cout << "c concurrent_search_processes 1" << std::endl;
	std::cout << "c hard_projection_kernel CaDiCaL" << std::endl;
	std::cout << "c refinement_kernel CCEHC" << std::endl;
	std::cout << "c refinement_seeded " << (refinement_started ? 1 : 0) << std::endl;
	std::cout << "c ccehc_p " << probability << " ccehc_sp "
		<< smooth_probability << std::endl;
	std::cout << "c ccehc_external_verified " << (refinement.valid ? 1 : 0)
		<< std::endl;

	if (choose_refinement)
	{
		std::cout << "c selected seeded CCEHC refinement" << std::endl;
		print_with_adjusted_times(refinement_child.stdout_text,
			refinement_started_seconds);
	}
	else if (fallback.found)
	{
		std::cout << "c selected retained hard-SAT fallback" << std::endl;
		std::cout << "o " << fallback.cost << " " << std::fixed
			<< std::setprecision(6) << fallback_event_seconds << std::endl;
		std::cout << "c final internal_verified 1" << std::endl;
		print_hard_sat_assignment(fallback);
	}
	else
		std::cout << "c final no_feasible_solution" << std::endl;

	if (have_solution)
	{
		std::cout << "c paper_table_best_cost " << final_cost << std::endl;
		std::cout << "c paper_table_time " << std::fixed << std::setprecision(6)
			<< paper_table_time << std::endl;
		std::cout << "c paper_table_time_definition first_time_final_best_solution_found"
			<< std::endl;
		std::cout << "c paper_table_time_unit wall_clock_seconds" << std::endl;
	}
	if (!fallback.found && !fallback.error.empty())
		std::cout << "c hard_sat_fallback note " << fallback.error << std::endl;
	if (!seed_error.empty())
		std::cout << "c seed note " << seed_error << std::endl;
	if (!refinement_child.error.empty())
		std::cout << "c refinement-core error " << refinement_child.error << std::endl;
	if (refinement_child.timed_out)
		std::cout << "c refinement-core timed_out 1" << std::endl;
	if (!refinement.verification_error.empty())
		std::cout << "c refinement verification_note "
			<< refinement.verification_error << std::endl;
	if (!refinement_child.stderr_text.empty())
	{
		std::istringstream errors(refinement_child.stderr_text);
		std::string line;
		while (std::getline(errors, line))
			std::cout << "c refinement-core stderr " << line << std::endl;
	}
	std::cout << "c total elapsed_time " << std::fixed << std::setprecision(6)
		<< std::chrono::duration<double>(Clock::now() - started).count() << std::endl;
	return have_solution ? 0 : 2;
}
