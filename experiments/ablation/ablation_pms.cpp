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

#define NUWLS_BUILD_ID "scope-maxsat-isolated-ablation"

#ifndef SCOPE_ABLATION_VARIANT
#error "SCOPE_ABLATION_VARIANT must be set by the Makefile"
#endif

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

struct VariantConfig
{
	const char *id;
	const char *name;
	bool projection;
	bool retention;
	bool state_transfer;
	bool hard_unit_lock;
	bool baseline_probe;
	bool quality_weights;
	double weight_power;
	double weight_scale;
	double weight_mix;
	double weight_anchor;
	double structure_mix;
	int structure_mode;
	double hard_guard;
};

VariantConfig variant_config()
{
	VariantConfig config = {
		"A7", "full_scope", true, true, true, true, true, true,
		1.0, 3000.0, 1.0, 0.0, -0.5, 3, 1.05
	};
#if SCOPE_ABLATION_VARIANT == 1
	config.id = "A1";
	config.name = "projection_without_retention";
	config.retention = false;
#elif SCOPE_ABLATION_VARIANT == 2
	config.id = "A2";
	config.name = "retention_without_state_transfer";
	config.state_transfer = false;
#elif SCOPE_ABLATION_VARIANT == 3
	config.id = "A3";
	config.name = "no_structural_schedule";
	config.hard_unit_lock = false;
	config.baseline_probe = false;
#elif SCOPE_ABLATION_VARIANT == 4
	config.id = "A4";
	config.name = "objective_only";
	config.structure_mix = 0.0;
#elif SCOPE_ABLATION_VARIANT == 5
	config.id = "A5";
	config.name = "no_hard_guard";
	config.hard_guard = 0.0;
#elif SCOPE_ABLATION_VARIANT == 6
	config.id = "A6";
	config.name = "uniform_soft_weights";
	config.weight_mix = 0.0;
	config.structure_mix = 0.0;
#elif SCOPE_ABLATION_VARIANT == 7
	// Full frozen SCOPE-MaxSAT configuration.
#elif SCOPE_ABLATION_VARIANT == 10
	config.id = "Y10";
	config.name = "projection_retention_host_weights";
	config.hard_unit_lock = false;
	config.baseline_probe = false;
	config.quality_weights = false;
#elif SCOPE_ABLATION_VARIANT == 11
	config.id = "Y01";
	config.name = "scope_weights_without_projection";
	config.projection = false;
	config.retention = false;
	config.state_transfer = false;
	config.hard_unit_lock = false;
	config.baseline_probe = false;
#elif SCOPE_ABLATION_VARIANT == 12
	config.id = "Y11";
	config.name = "full_scope_factorial";
#else
#error "Unsupported SCOPE_ABLATION_VARIANT"
#endif
	return config;
}

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

void append_non_quality_arguments(std::vector<std::string> &arguments,
	int argc, char *argv[])
{
	for (int i = 4; i < argc; ++i)
	{
		if (is_quality_option(argv[i]) && i + 1 < argc)
		{
			++i;
			continue;
		}
		arguments.push_back(argv[i]);
	}
}

void append_option(std::vector<std::string> &arguments, const char *name,
	double value)
{
	std::ostringstream text;
	text << std::setprecision(17) << value;
	arguments.push_back(name);
	arguments.push_back(text.str());
}

void append_quality_profile(std::vector<std::string> &arguments,
	const VariantConfig &config)
{
	arguments.push_back("-quality_phase_enabled");
	arguments.push_back(config.quality_weights ? "1" : "0");
	append_option(arguments, "-quality_weight_power", config.weight_power);
	append_option(arguments, "-quality_weight_scale", config.weight_scale);
	append_option(arguments, "-quality_weight_mix", config.weight_mix);
	append_option(arguments, "-quality_weight_anchor", config.weight_anchor);
	append_option(arguments, "-quality_structure_mix", config.structure_mix);
	arguments.push_back("-quality_structure_mode");
	arguments.push_back(std::to_string(config.structure_mode));
	append_option(arguments, "-quality_hard_guard", config.hard_guard);
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

bool should_probe_baseline(const HardSatFallbackResult &fallback,
	double remaining_seconds)
{
	return fallback.found && !hard_unit_locked(fallback) &&
		fallback.max_hard_clause_length > STRUCTURE_WEIGHT_WIDTH_LIMIT &&
		fallback.elapsed_seconds <= BASELINE_PROBE_MAX_HARD_SAT_SECONDS &&
		remaining_seconds >= BASELINE_PROBE_SECONDS + MIN_QUALITY_SECONDS_AFTER_PROBE;
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
	const VariantConfig config = variant_config();
	std::cout << "c build " << NUWLS_BUILD_ID << std::endl;
	std::cout << "c ablation_id " << config.id << std::endl;
	std::cout << "c ablation_name " << config.name << std::endl;
	std::cout << "c ablation_flags projection " << (config.projection ? 1 : 0)
		<< " retention " << (config.retention ? 1 : 0)
		<< " state_transfer " << (config.state_transfer ? 1 : 0)
		<< " hard_unit_lock " << (config.hard_unit_lock ? 1 : 0)
		<< " baseline_probe " << (config.baseline_probe ? 1 : 0)
		<< " quality_weights " << (config.quality_weights ? 1 : 0) << std::endl;
	std::cout << "c ablation_quality_profile weight_power " << config.weight_power
		<< " weight_scale " << config.weight_scale
		<< " weight_mix " << config.weight_mix
		<< " weight_anchor " << config.weight_anchor
		<< " structure_mix " << config.structure_mix
		<< " structure_mode " << config.structure_mode
		<< " hard_guard " << config.hard_guard << std::endl;
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

	ProcessResult baseline_probe_child;
	ProcessResult refinement_child;
	double baseline_probe_started_seconds = 0.0;
	double refinement_started_seconds = 0.0;
	bool baseline_probe_started = false;
	bool refinement_started = false;
	bool refinement_is_quality = false;
	bool refinement_seeded = false;
	const std::string core_path = sibling_executable_path(argv[0], "nuwls-core");
	const std::string quality_core_path = sibling_executable_path(argv[0],
		"nuwls-quality-core");
	const double fallback_started_seconds =
		std::chrono::duration<double>(Clock::now() - started).count();
	HardSatFallbackResult fallback;
	std::string quality_seed_path;
	std::string quality_seed_error;
	bool locked = false;
	bool baseline_probe_requested = false;


	if (config.projection)
		fallback = find_hard_sat_fallback(argv[1], search_deadline,
			finalization_deadline);
	locked = config.hard_unit_lock && fallback.found && hard_unit_locked(fallback);
	const double initial_remaining_seconds =
		std::chrono::duration<double>(search_deadline - Clock::now()).count();
	baseline_probe_requested = config.baseline_probe && !locked &&
		should_probe_baseline(fallback, initial_remaining_seconds);

	const auto run_refinement = [&](bool use_quality_core, bool use_seed)
	{
		const double remaining_seconds =
			std::chrono::duration<double>(search_deadline - Clock::now()).count();
		if (remaining_seconds <= 0.0)
			return;
		std::ostringstream cutoff_argument;
		cutoff_argument << std::fixed << std::setprecision(3) << remaining_seconds;
		std::vector<std::string> arguments;
		arguments.push_back(argv[1]);
		arguments.push_back(std::to_string(seed));
		arguments.push_back(cutoff_argument.str());
		append_non_quality_arguments(arguments, argc, argv);
		arguments.push_back("-deadline_epoch_ms");
		arguments.push_back(std::to_string(deadline_epoch_milliseconds));
		if (use_quality_core)
		{
			if (use_seed)
			{
				arguments.push_back("-quality_seed_file");
				arguments.push_back(quality_seed_path);
			}
			append_quality_profile(arguments, config);
		}
		refinement_is_quality = use_quality_core;
		refinement_seeded = use_seed;
		refinement_started_seconds =
			std::chrono::duration<double>(Clock::now() - started).count();
		RunningProcess *process = start_process(
			use_quality_core ? quality_core_path : core_path, arguments,
			refinement_child.error);
		if (process)
		{
			refinement_started = true;
			refinement_child = finish_process(process, finalization_deadline);
		}
	};

	if (!config.projection)
	{
		run_refinement(true, false);
	}
	else if (fallback.found && initial_remaining_seconds > 0.0)
	{
		bool seed_ready = false;
		if (!locked && config.state_transfer)
			seed_ready = write_quality_seed(fallback, quality_seed_path,
				quality_seed_error);

		if (baseline_probe_requested)
		{
			std::ostringstream probe_cutoff_argument;
			probe_cutoff_argument << std::fixed << std::setprecision(3)
				<< BASELINE_PROBE_SECONDS;
			std::vector<std::string> arguments;
			arguments.push_back(argv[1]);
			arguments.push_back(std::to_string(seed));
			arguments.push_back(probe_cutoff_argument.str());
			append_non_quality_arguments(arguments, argc, argv);
			arguments.push_back("-deadline_epoch_ms");
			arguments.push_back(std::to_string(deadline_epoch_milliseconds));
			baseline_probe_started_seconds =
				std::chrono::duration<double>(Clock::now() - started).count();
			RunningProcess *process = start_process(core_path, arguments,
				baseline_probe_child.error);
			if (process)
			{
				baseline_probe_started = true;
				const Clock::time_point probe_finish_deadline = std::min(
					search_deadline,
					Clock::now() + std::chrono::duration_cast<Clock::duration>(
						std::chrono::duration<double>(BASELINE_PROBE_SECONDS +
							BASELINE_PROBE_GRACE_SECONDS)));
				baseline_probe_child = finish_process(process,
					probe_finish_deadline);
			}
		}

		if (locked)
			run_refinement(false, false);
		else if (!config.state_transfer || seed_ready)
			run_refinement(true, seed_ready);
		else
			run_refinement(false, false);
	}

	if (!quality_seed_path.empty())
		std::remove(quality_seed_path.c_str());
	const double fallback_seconds = fallback.elapsed_seconds;
	const double fallback_event_seconds = fallback_started_seconds + fallback_seconds;
	std::cout << "c hard_projection enabled " << (config.projection ? 1 : 0)
		<< " retained " << (config.retention ? 1 : 0)
		<< " state_transferred " << (refinement_seeded ? 1 : 0) << std::endl;
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
			<< (refinement_is_quality && refinement_started ? 1 : 0)
			<< " hard_unit_lock_threshold " << HARD_UNIT_LOCK_RATIO << std::endl;
		std::cout << "c baseline_probe requested "
			<< (baseline_probe_requested ? 1 : 0) << " started "
			<< (baseline_probe_started ? 1 : 0) << " seconds "
			<< BASELINE_PROBE_SECONDS << " max_hard_sat_seconds "
			<< BASELINE_PROBE_MAX_HARD_SAT_SECONDS << " width_threshold "
			<< STRUCTURE_WEIGHT_WIDTH_LIMIT << std::endl;
		std::cout << "c sequential_refinement mode "
			<< (baseline_probe_started && refinement_started
				? "baseline_probe_then_seeded_quality"
				: (refinement_started
					? (refinement_is_quality
						? (refinement_seeded ? "seeded_quality" : "unseeded_quality")
						: "preserved_baseline")
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
	consider_candidate(config.retention && fallback.found, fallback.cost,
		fallback_event_seconds,
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
			<< (refinement_is_quality
				? (refinement_seeded ? "seeded quality search" : "unseeded quality search")
				: "preserved baseline refinement")
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


