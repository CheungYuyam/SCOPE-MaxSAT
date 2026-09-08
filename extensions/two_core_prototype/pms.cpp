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

#define NUWLS_BUILD_ID "scope-maxsat-two-core-prototype"

namespace
{
using Clock = std::chrono::steady_clock;
const double FINALIZATION_GRACE_SECONDS = 5.0;
const double HARD_UNIT_LOCK_RATIO = 0.001;

struct BaselineOutput
{
	bool valid;
	long long cost;
	double found_time;
	BaselineOutput() : valid(false), cost(0), found_time(0.0) {}
};

std::string sibling_executable_path(const char *launcher, const char *basename)
{
	const std::string path(launcher);
	const size_t separator = path.find_last_of("/\\");
	const std::string directory = separator == std::string::npos ? "." : path.substr(0, separator);
#ifdef _WIN32
	return directory + "\\" + basename + ".exe";
#else
	return directory + "/" + basename;
#endif
}

bool is_quality_option(const std::string &argument)
{
	return argument == "-quality_seed_file" ||
		argument == "-quality_phase_enabled" ||
		argument == "-quality_weight_power" ||
		argument == "-quality_weight_scale" ||
		argument == "-quality_weight_mix" ||
		argument == "-quality_weight_anchor" ||
		argument == "-quality_structure_mix" ||
		argument == "-quality_structure_mode" ||
		argument == "-quality_hard_guard";
}

double hard_unit_ratio(const HardSatFallbackResult &fallback)
{
	return fallback.hard_clause_count > 0
		? static_cast<double>(fallback.hard_unit_clause_count) /
			fallback.hard_clause_count
		: 0.0;
}

bool hard_unit_locked(const HardSatFallbackResult &fallback)
{
	return fallback.hard_clause_count >= 1000 &&
		hard_unit_ratio(fallback) >= HARD_UNIT_LOCK_RATIO;
}

BaselineOutput parse_baseline_output(const std::string &text)
{
	BaselineOutput result;
	bool internally_verified = false;
	bool has_assignment = false;
	bool has_objective = false;
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
			has_assignment = true;
			for (size_t i = 2; i < line.size(); ++i)
				if (line[i] != '0' && line[i] != '1')
					has_assignment = false;
		}
		else if (line.size() > 2 && line[0] == 'o' && line[1] == ' ')
		{
			std::istringstream fields(line.substr(2));
			double found_time = 0.0;
			long long cost = 0;
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
	result.valid = internally_verified && has_assignment && has_objective;
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

bool write_quality_seed(const HardSatFallbackResult &fallback,
	std::string &path, std::string &error)
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
	path += "nuwls-quality-seed-" + std::to_string(stamp) + ".txt";

	std::ofstream output(path.c_str(), std::ios::binary);
	if (!output)
	{
		error = "cannot create quality seed file";
		return false;
	}
	for (size_t v = 1; v < fallback.assignment.size(); ++v)
		output << (fallback.assignment[v] ? '1' : '0');
	output << '\n';
	if (!output)
	{
		error = "cannot write quality seed file";
		output.close();
		std::remove(path.c_str());
		path.clear();
		return false;
	}
	return true;
}
} // namespace

int main(int argc, char *argv[])
{
	const Clock::time_point started = Clock::now();
	std::cout << "c build " << NUWLS_BUILD_ID << std::endl;
	if (argc < 4)
	{
		std::cout << "usage: " << argv[0] << " instance.wcnf seed cutoff_seconds" << std::endl;
		return 1;
	}

	const int seed = std::atoi(argv[2]);
	const double cutoff = std::strtod(argv[3], NULL);
	if (cutoff <= 0.0)
	{
		std::cout << "cutoff_seconds must be positive" << std::endl;
		return 1;
	}
	const Clock::time_point search_deadline = started +
		std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(cutoff));
	const Clock::time_point finalization_deadline = search_deadline +
		std::chrono::duration_cast<Clock::duration>(
			std::chrono::duration<double>(FINALIZATION_GRACE_SECONDS));
	const double system_deadline_seconds = std::chrono::duration<double>(
		search_deadline - Clock::now()).count();
	const std::chrono::system_clock::time_point system_deadline =
		std::chrono::system_clock::now() +
		std::chrono::duration_cast<std::chrono::system_clock::duration>(
			std::chrono::duration<double>(system_deadline_seconds));
	const long long deadline_epoch_milliseconds =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			system_deadline.time_since_epoch()).count();

	std::vector<std::string> child_arguments;
	std::vector<std::string> quality_arguments;
	ProcessResult child;
	ProcessResult quality_child;
	const double remaining_seconds = std::chrono::duration<double>(search_deadline - Clock::now()).count();
	RunningProcess *child_process = NULL;
	RunningProcess *quality_process = NULL;
	double core_started_seconds = 0.0;
	double quality_started_seconds = 0.0;
	const std::string core_path = sibling_executable_path(argv[0], "nuwls-core");
	const std::string quality_core_path = sibling_executable_path(argv[0],
		"nuwls-quality-core");
	if (remaining_seconds > 0.0)
	{
		std::ostringstream cutoff_argument;
		cutoff_argument << std::fixed << std::setprecision(3)
			<< remaining_seconds;
		child_arguments.push_back(argv[1]);
		child_arguments.push_back(std::to_string(seed));
		child_arguments.push_back(cutoff_argument.str());
		for (int i = 4; i < argc; ++i)
		{
			if (is_quality_option(argv[i]) && i + 1 < argc)
			{
				++i;
				continue;
			}
			child_arguments.push_back(argv[i]);
		}
		child_arguments.push_back("-deadline_epoch_ms");
		child_arguments.push_back(std::to_string(deadline_epoch_milliseconds));
		quality_arguments.push_back(argv[1]);
		quality_arguments.push_back(std::to_string(seed));
		quality_arguments.push_back(cutoff_argument.str());
		for (int i = 4; i < argc; ++i)
			quality_arguments.push_back(argv[i]);
		quality_arguments.push_back("-deadline_epoch_ms");
		quality_arguments.push_back(std::to_string(deadline_epoch_milliseconds));
		core_started_seconds = std::chrono::duration<double>(Clock::now() - started).count();
		child_process = start_process(core_path, child_arguments, child.error);
	}
	else
		child.error = "no time remains for nuwls-core";

	const double fallback_started_seconds =
		std::chrono::duration<double>(Clock::now() - started).count();
	const HardSatFallbackResult fallback = find_hard_sat_fallback(
		argv[1], search_deadline, finalization_deadline);
	std::string quality_seed_path;
	std::string quality_seed_error;
	if (fallback.found && Clock::now() < search_deadline &&
		!hard_unit_locked(fallback) &&
		write_quality_seed(fallback, quality_seed_path, quality_seed_error))
	{
		quality_arguments.push_back("-quality_seed_file");
		quality_arguments.push_back(quality_seed_path);
		quality_arguments.push_back("-quality_phase_enabled");
		quality_arguments.push_back("1");
		quality_started_seconds =
			std::chrono::duration<double>(Clock::now() - started).count();
		quality_process = start_process(quality_core_path, quality_arguments,
			quality_child.error);
	}
	if (child_process)
		child = finish_process(child_process, finalization_deadline);
	if (quality_process)
		quality_child = finish_process(quality_process, finalization_deadline);
	if (!quality_seed_path.empty())
		std::remove(quality_seed_path.c_str());
	const double fallback_seconds = fallback.elapsed_seconds;
	const double fallback_event_seconds = fallback_started_seconds + fallback_seconds;
	std::cout << "c hard_sat_fallback elapsed_time " << std::fixed << std::setprecision(6)
		<< fallback_seconds << " success " << (fallback.found ? 1 : 0) << std::endl;
	if (!fallback.found && !fallback.error.empty())
		std::cout << "c hard_sat_fallback note " << fallback.error << std::endl;
	if (fallback.hard_clause_count > 0)
	{
		std::cout << "c quality_structure hard_clauses "
			<< fallback.hard_clause_count << " hard_unit_clauses "
			<< fallback.hard_unit_clause_count << " hard_unit_ratio "
			<< std::fixed << std::setprecision(6)
			<< hard_unit_ratio(fallback) << std::endl;
		std::cout << "c quality_search scheduled "
			<< (hard_unit_locked(fallback) ? 0 : 1)
			<< " hard_unit_lock_threshold " << HARD_UNIT_LOCK_RATIO << std::endl;
	}
	const BaselineOutput baseline = parse_baseline_output(child.stdout_text);
	const BaselineOutput quality = parse_baseline_output(quality_child.stdout_text);
	enum SelectedCandidate
	{
		SELECT_NONE,
		SELECT_BASELINE,
		SELECT_QUALITY,
		SELECT_FALLBACK
	};
	SelectedCandidate selected = SELECT_NONE;
	long long final_best_cost = 0;
	double paper_table_time = 0.0;
	if (baseline.valid)
	{
		selected = SELECT_BASELINE;
		final_best_cost = baseline.cost;
		paper_table_time = core_started_seconds + baseline.found_time;
	}
	if (quality.valid && (selected == SELECT_NONE || quality.cost < final_best_cost))
	{
		selected = SELECT_QUALITY;
		final_best_cost = quality.cost;
		paper_table_time = quality_started_seconds + quality.found_time;
	}
	if (fallback.found && (selected == SELECT_NONE || fallback.cost < final_best_cost))
	{
		selected = SELECT_FALLBACK;
		final_best_cost = fallback.cost;
		paper_table_time = fallback_event_seconds;
	}
	if (baseline.valid && baseline.cost == final_best_cost &&
		core_started_seconds + baseline.found_time < paper_table_time)
		paper_table_time = core_started_seconds + baseline.found_time;
	if (quality.valid && quality.cost == final_best_cost &&
		quality_started_seconds + quality.found_time < paper_table_time)
		paper_table_time = quality_started_seconds + quality.found_time;
	if (fallback.found && fallback.cost == final_best_cost &&
		fallback_event_seconds < paper_table_time)
		paper_table_time = fallback_event_seconds;

	if (selected == SELECT_BASELINE)
	{
		std::cout << "c selected nuwls baseline" << std::endl;
		print_with_adjusted_times(child.stdout_text, core_started_seconds);
	}
	else if (selected == SELECT_QUALITY)
	{
		std::cout << "c selected seeded quality search" << std::endl;
		print_with_adjusted_times(quality_child.stdout_text, quality_started_seconds);
	}
	else if (selected == SELECT_FALLBACK)
	{
		std::cout << "c selected hard-SAT fallback over baseline_valid "
			<< (baseline.valid ? 1 : 0) << std::endl;
		std::cout << "o " << fallback.cost << " " << std::fixed << std::setprecision(6)
			<< fallback_event_seconds << std::endl;
		std::cout << "c final internal_verified 1" << std::endl;
		print_hard_sat_assignment(fallback);
	}
	else
	{
		std::cout << "c final no_feasible_solution" << std::endl;
	}

	if (selected != SELECT_NONE)
	{
		std::cout << "c paper_table_best_cost " << final_best_cost << std::endl;
		std::cout << "c paper_table_time " << std::fixed << std::setprecision(6)
			<< paper_table_time << std::endl;
		std::cout << "c paper_table_time_definition first_time_final_best_solution_found"
			<< std::endl;
		std::cout << "c paper_table_time_unit wall_clock_seconds" << std::endl;
	}

	if (!child.error.empty())
		std::cout << "c nuwls-core error " << child.error << std::endl;
	if (!quality_seed_error.empty())
		std::cout << "c quality_seed note " << quality_seed_error << std::endl;
	if (!quality_child.error.empty())
		std::cout << "c quality-core error " << quality_child.error << std::endl;
	if (child.timed_out && selected == SELECT_NONE)
		std::cout << "c nuwls-core timed_out 1" << std::endl;
	if (!child.stderr_text.empty())
	{
		std::istringstream errors(child.stderr_text);
		std::string line;
		while (std::getline(errors, line))
			std::cout << "c nuwls-core stderr " << line << std::endl;
	}
	if (!quality_child.stderr_text.empty())
	{
		std::istringstream errors(quality_child.stderr_text);
		std::string line;
		while (std::getline(errors, line))
			std::cout << "c quality-core stderr " << line << std::endl;
	}
	std::cout << "c total elapsed_time " << std::fixed << std::setprecision(6)
		<< std::chrono::duration<double>(Clock::now() - started).count() << std::endl;
	return selected != SELECT_NONE ? 0 : child.return_code;
}
