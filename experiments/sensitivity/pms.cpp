#include "hard_sat_fallback.h"
#include "subprocess_runner.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define NUWLS_BUILD_ID "scope-maxsat-sensitivity"

namespace
{
using Clock = std::chrono::steady_clock;
const double FINALIZATION_GRACE_SECONDS = 5.0;
const double HARD_UNIT_LOCK_RATIO = 0.001;
const int STRUCTURE_WEIGHT_WIDTH_LIMIT = 8;
const double BASELINE_PROBE_SECONDS = 12.5;
const double BASELINE_PROBE_GRACE_SECONDS = 1.5;
const double BASELINE_PROBE_MAX_HARD_SAT_SECONDS = 3.0;
const double MIN_QUALITY_SECONDS_AFTER_PROBE = 30.0;

struct ScheduleConfig
{
	int hard_unit_lock_min_hard_clauses;
	double hard_unit_lock_ratio;
	int structure_weight_width_limit;
	double baseline_probe_seconds;
	double baseline_probe_max_hard_sat_seconds;
	double min_quality_seconds_after_probe;

	ScheduleConfig()
		: hard_unit_lock_min_hard_clauses(1000),
		  hard_unit_lock_ratio(HARD_UNIT_LOCK_RATIO),
		  structure_weight_width_limit(STRUCTURE_WEIGHT_WIDTH_LIMIT),
		  baseline_probe_seconds(BASELINE_PROBE_SECONDS),
		  baseline_probe_max_hard_sat_seconds(BASELINE_PROBE_MAX_HARD_SAT_SECONDS),
		  min_quality_seconds_after_probe(MIN_QUALITY_SECONDS_AFTER_PROBE)
	{}
};

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

bool is_schedule_option(const std::string &argument)
{
	return argument == "-schedule_hard_clause_threshold" ||
		argument == "-schedule_hard_unit_ratio" ||
		argument == "-schedule_width_limit" ||
		argument == "-schedule_probe_seconds" ||
		argument == "-schedule_probe_max_hard_sat_seconds" ||
		argument == "-schedule_min_quality_seconds";
}

template <typename T>
bool parse_number(const char *text, T &value)
{
	std::istringstream input(text);
	input >> value;
	return input && input.eof();
}

bool parse_schedule_options(int argc, char *argv[], ScheduleConfig &config,
	std::string &error)
{
	for (int i = 4; i < argc; ++i)
	{
		const std::string option(argv[i]);
		if (!is_schedule_option(option))
			continue;
		if (++i >= argc)
		{
			error = "missing value for " + option;
			return false;
		}
		bool parsed = false;
		if (option == "-schedule_hard_clause_threshold")
			parsed = parse_number(argv[i], config.hard_unit_lock_min_hard_clauses);
		else if (option == "-schedule_hard_unit_ratio")
			parsed = parse_number(argv[i], config.hard_unit_lock_ratio);
		else if (option == "-schedule_width_limit")
			parsed = parse_number(argv[i], config.structure_weight_width_limit);
		else if (option == "-schedule_probe_seconds")
			parsed = parse_number(argv[i], config.baseline_probe_seconds);
		else if (option == "-schedule_probe_max_hard_sat_seconds")
			parsed = parse_number(argv[i], config.baseline_probe_max_hard_sat_seconds);
		else if (option == "-schedule_min_quality_seconds")
			parsed = parse_number(argv[i], config.min_quality_seconds_after_probe);
		if (!parsed)
		{
			error = "invalid numeric value for " + option;
			return false;
		}
	}
	if (config.hard_unit_lock_min_hard_clauses < 0 ||
		config.hard_unit_lock_ratio < 0.0 || config.hard_unit_lock_ratio > 1.0 ||
		config.structure_weight_width_limit < 0 ||
		config.baseline_probe_seconds < 0.0 ||
		config.baseline_probe_max_hard_sat_seconds < 0.0 ||
		config.min_quality_seconds_after_probe < 0.0)
	{
		error = "schedule parameter outside its valid range";
		return false;
	}
	return true;
}

double hard_unit_ratio(const HardSatFallbackResult &fallback)
{
	return fallback.hard_clause_count > 0
		? static_cast<double>(fallback.hard_unit_clause_count) /
			fallback.hard_clause_count
		: 0.0;
}

bool hard_unit_locked(const HardSatFallbackResult &fallback,
	const ScheduleConfig &config)
{
	return fallback.hard_clause_count >= config.hard_unit_lock_min_hard_clauses &&
		hard_unit_ratio(fallback) >= config.hard_unit_lock_ratio;
}

bool should_probe_baseline(const HardSatFallbackResult &fallback,
	double remaining_seconds, const ScheduleConfig &config)
{
	return fallback.found && !hard_unit_locked(fallback, config) &&
		config.baseline_probe_seconds > 0.0 &&
		fallback.max_hard_clause_length > config.structure_weight_width_limit &&
		fallback.elapsed_seconds <= config.baseline_probe_max_hard_sat_seconds &&
		remaining_seconds >= config.baseline_probe_seconds +
			config.min_quality_seconds_after_probe;
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
	ScheduleConfig schedule;
	std::string schedule_error;
	if (!parse_schedule_options(argc, argv, schedule, schedule_error))
	{
		std::cout << "c invalid schedule parameter: " << schedule_error << std::endl;
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

	std::vector<std::string> baseline_arguments;
	std::vector<std::string> quality_arguments;
	ProcessResult baseline_probe_child;
	ProcessResult refinement_child;
	double baseline_probe_started_seconds = 0.0;
	double refinement_started_seconds = 0.0;
	bool baseline_probe_started = false;
	bool refinement_started = false;
	bool refinement_is_quality = false;
	const std::string core_path = sibling_executable_path(argv[0], "nuwls-core");
	const std::string quality_core_path = sibling_executable_path(argv[0],
		"nuwls-quality-core");

	const double fallback_started_seconds =
		std::chrono::duration<double>(Clock::now() - started).count();
	const HardSatFallbackResult fallback = find_hard_sat_fallback(
		argv[1], search_deadline, finalization_deadline);
	std::string quality_seed_path;
	std::string quality_seed_error;
	const bool locked = fallback.found && hard_unit_locked(fallback, schedule);
	const double initial_remaining_seconds =
		std::chrono::duration<double>(search_deadline - Clock::now()).count();
	const bool baseline_probe_requested = !locked &&
		should_probe_baseline(fallback, initial_remaining_seconds, schedule);
	if (fallback.found && initial_remaining_seconds > 0.0)
	{
		if (!locked && write_quality_seed(fallback, quality_seed_path,
			quality_seed_error))
		{
			if (baseline_probe_requested)
			{
				std::ostringstream probe_cutoff_argument;
				probe_cutoff_argument << std::fixed << std::setprecision(3)
					<< schedule.baseline_probe_seconds;
				baseline_arguments.push_back(argv[1]);
				baseline_arguments.push_back(std::to_string(seed));
				baseline_arguments.push_back(probe_cutoff_argument.str());
				for (int i = 4; i < argc; ++i)
				{
					if ((is_quality_option(argv[i]) || is_schedule_option(argv[i])) &&
						i + 1 < argc)
					{
						++i;
						continue;
					}
					baseline_arguments.push_back(argv[i]);
				}
				baseline_arguments.push_back("-deadline_epoch_ms");
				baseline_arguments.push_back(std::to_string(deadline_epoch_milliseconds));
				baseline_probe_started_seconds =
					std::chrono::duration<double>(Clock::now() - started).count();
				RunningProcess *baseline_probe_process = start_process(core_path,
					baseline_arguments, baseline_probe_child.error);
				if (baseline_probe_process)
				{
					baseline_probe_started = true;
					const Clock::time_point probe_finish_deadline = std::min(
						search_deadline,
						Clock::now() + std::chrono::duration_cast<Clock::duration>(
							std::chrono::duration<double>(schedule.baseline_probe_seconds +
								BASELINE_PROBE_GRACE_SECONDS)));
					baseline_probe_child = finish_process(baseline_probe_process,
						probe_finish_deadline);
				}
			}

			const double quality_remaining_seconds =
				std::chrono::duration<double>(search_deadline - Clock::now()).count();
			if (quality_remaining_seconds > 0.0)
			{
				std::ostringstream quality_cutoff_argument;
				quality_cutoff_argument << std::fixed << std::setprecision(3)
					<< quality_remaining_seconds;
				quality_arguments.push_back(argv[1]);
				quality_arguments.push_back(std::to_string(seed));
				quality_arguments.push_back(quality_cutoff_argument.str());
				for (int i = 4; i < argc; ++i)
				{
					if (is_schedule_option(argv[i]) && i + 1 < argc)
					{
						++i;
						continue;
					}
					quality_arguments.push_back(argv[i]);
				}
				quality_arguments.push_back("-deadline_epoch_ms");
				quality_arguments.push_back(std::to_string(deadline_epoch_milliseconds));
				quality_arguments.push_back("-quality_seed_file");
				quality_arguments.push_back(quality_seed_path);
				quality_arguments.push_back("-quality_phase_enabled");
				quality_arguments.push_back("1");
				refinement_is_quality = true;
				refinement_started_seconds =
					std::chrono::duration<double>(Clock::now() - started).count();
				RunningProcess *refinement_process = start_process(quality_core_path,
					quality_arguments, refinement_child.error);
				if (refinement_process)
				{
					refinement_started = true;
					refinement_child = finish_process(refinement_process,
						finalization_deadline);
				}
			}
		}
		else
		{
			std::ostringstream cutoff_argument;
			cutoff_argument << std::fixed << std::setprecision(3)
				<< initial_remaining_seconds;
			baseline_arguments.push_back(argv[1]);
			baseline_arguments.push_back(std::to_string(seed));
			baseline_arguments.push_back(cutoff_argument.str());
			for (int i = 4; i < argc; ++i)
			{
				if ((is_quality_option(argv[i]) || is_schedule_option(argv[i])) &&
					i + 1 < argc)
				{
					++i;
					continue;
				}
				baseline_arguments.push_back(argv[i]);
			}
			baseline_arguments.push_back("-deadline_epoch_ms");
			baseline_arguments.push_back(std::to_string(deadline_epoch_milliseconds));
			refinement_started_seconds =
				std::chrono::duration<double>(Clock::now() - started).count();
			RunningProcess *refinement_process = start_process(core_path, baseline_arguments,
				refinement_child.error);
			if (refinement_process)
			{
				refinement_started = true;
				refinement_child = finish_process(refinement_process,
					finalization_deadline);
			}
		}
	}
	if (!quality_seed_path.empty())
		std::remove(quality_seed_path.c_str());
	const double fallback_seconds = fallback.elapsed_seconds;
	const double fallback_event_seconds = fallback_started_seconds + fallback_seconds;
	std::cout << "c hard_sat_fallback elapsed_time " << std::fixed << std::setprecision(6)
		<< fallback_seconds << " success " << (fallback.found ? 1 : 0) << std::endl;
	std::cout << "c single_core_schedule hard_sat_then_refinement" << std::endl;
	std::cout << "c concurrent_search_processes 1" << std::endl;
	if (!fallback.found && !fallback.error.empty())
		std::cout << "c hard_sat_fallback note " << fallback.error << std::endl;
	if (fallback.hard_clause_count > 0)
	{
		std::cout << "c quality_structure hard_clauses "
			<< fallback.hard_clause_count << " hard_unit_clauses "
			<< fallback.hard_unit_clause_count << " hard_unit_ratio "
			<< std::fixed << std::setprecision(6)
			<< hard_unit_ratio(fallback) << " max_hard_clause_length "
			<< fallback.max_hard_clause_length << std::endl;
		std::cout << "c quality_search scheduled "
			<< (!locked && refinement_is_quality && refinement_started ? 1 : 0)
			<< " hard_unit_lock_threshold " << schedule.hard_unit_lock_ratio
			<< " hard_clause_threshold "
			<< schedule.hard_unit_lock_min_hard_clauses << std::endl;
		std::cout << "c baseline_probe requested "
			<< (baseline_probe_requested ? 1 : 0) << " started "
			<< (baseline_probe_started ? 1 : 0) << " seconds "
			<< schedule.baseline_probe_seconds << " max_hard_sat_seconds "
			<< schedule.baseline_probe_max_hard_sat_seconds
			<< " min_quality_seconds " << schedule.min_quality_seconds_after_probe
			<< " width_threshold " << schedule.structure_weight_width_limit
			<< std::endl;
		std::cout << "c sequential_refinement mode "
			<< (baseline_probe_started && refinement_started
				? "baseline_probe_then_seeded_quality"
				: (refinement_started
					? (refinement_is_quality ? "seeded_quality" : "preserved_baseline")
					: (baseline_probe_started ? "baseline_probe_only" : "none")))
			<< std::endl;
	}
	const BaselineOutput baseline_probe =
		parse_baseline_output(baseline_probe_child.stdout_text);
	const BaselineOutput refinement = parse_baseline_output(refinement_child.stdout_text);
	enum SelectedCandidate
	{
		SELECT_NONE,
		SELECT_BASELINE_PROBE,
		SELECT_REFINEMENT,
		SELECT_FALLBACK
	};
	SelectedCandidate selected = SELECT_NONE;
	long long final_best_cost = 0;
	double paper_table_time = 0.0;
	const auto consider_candidate = [&](bool valid, long long cost, double found_time,
		SelectedCandidate candidate)
	{
		if (valid && (selected == SELECT_NONE || cost < final_best_cost ||
			(cost == final_best_cost && found_time < paper_table_time)))
		{
			selected = candidate;
			final_best_cost = cost;
			paper_table_time = found_time;
		}
	};
	consider_candidate(baseline_probe.valid, baseline_probe.cost,
		baseline_probe_started_seconds + baseline_probe.found_time,
		SELECT_BASELINE_PROBE);
	consider_candidate(refinement.valid, refinement.cost,
		refinement_started_seconds + refinement.found_time, SELECT_REFINEMENT);
	consider_candidate(fallback.found, fallback.cost, fallback_event_seconds,
		SELECT_FALLBACK);

	if (selected == SELECT_BASELINE_PROBE)
	{
		std::cout << "c selected baseline probe" << std::endl;
		print_with_adjusted_times(baseline_probe_child.stdout_text,
			baseline_probe_started_seconds);
	}
	else if (selected == SELECT_REFINEMENT)
	{
		std::cout << "c selected "
			<< (refinement_is_quality ? "seeded quality search" : "preserved baseline refinement")
			<< std::endl;
		print_with_adjusted_times(refinement_child.stdout_text,
			refinement_started_seconds);
	}
	else if (selected == SELECT_FALLBACK)
	{
		std::cout << "c selected hard-SAT fallback over baseline_valid "
			<< (refinement.valid ? 1 : 0) << std::endl;
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

	if (!quality_seed_error.empty())
		std::cout << "c quality_seed note " << quality_seed_error << std::endl;
	if (!baseline_probe_child.error.empty())
		std::cout << "c baseline-probe error " << baseline_probe_child.error << std::endl;
	if (baseline_probe_child.timed_out)
		std::cout << "c baseline-probe timed_out 1" << std::endl;
	if (!refinement_child.error.empty())
		std::cout << "c refinement-core error " << refinement_child.error << std::endl;
	if (refinement_child.timed_out && selected == SELECT_NONE)
		std::cout << "c refinement-core timed_out 1" << std::endl;
	if (!refinement_child.stderr_text.empty())
	{
		std::istringstream errors(refinement_child.stderr_text);
		std::string line;
		while (std::getline(errors, line))
			std::cout << "c refinement-core stderr " << line << std::endl;
	}
	std::cout << "c total elapsed_time " << std::fixed << std::setprecision(6)
		<< std::chrono::duration<double>(Clock::now() - started).count() << std::endl;
	return selected != SELECT_NONE ? 0 : refinement_child.return_code;
}
