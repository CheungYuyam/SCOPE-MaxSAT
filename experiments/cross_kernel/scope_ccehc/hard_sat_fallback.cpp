#include "hard_sat_fallback.h"

#include "third_party/cadical/src/cadical.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
using Clock = std::chrono::steady_clock;

struct WcnfHeader
{
	int variables;
	long long clauses;
	long long top;
};

class DeadlineTerminator : public CaDiCaL::Terminator
{
  public:
	explicit DeadlineTerminator(const Clock::time_point &deadline)
		: deadline_(deadline)
	{
	}

	bool terminate()
	{
		return Clock::now() >= deadline_;
	}

  private:
	Clock::time_point deadline_;
};

double elapsed_since(const Clock::time_point &started)
{
	return std::chrono::duration<double>(Clock::now() - started).count();
}

bool parse_integer(const char *&cursor, long long &value)
{
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r')
		++cursor;
	bool negative = false;
	if (*cursor == '-')
	{
		negative = true;
		++cursor;
	}
	if (*cursor < '0' || *cursor > '9')
		return false;
	value = 0;
	while (*cursor >= '0' && *cursor <= '9')
	{
		value = value * 10 + (*cursor - '0');
		++cursor;
	}
	if (negative)
		value = -value;
	return true;
}

bool read_header(const char *filename, WcnfHeader &header,
	const Clock::time_point &deadline, std::string &error)
{
	std::ifstream input(filename);
	if (!input)
	{
		error = "cannot open WCNF file";
		return false;
	}

	std::string line;
	while (std::getline(input, line))
	{
		if (Clock::now() >= deadline)
		{
			error = "global cutoff reached while reading WCNF header";
			return false;
		}
		std::istringstream fields(line);
		std::string marker;
		fields >> marker;
		if (marker.empty() || marker == "c")
			continue;
		if (marker == "p")
		{
			std::string format;
			if (!(fields >> format >> header.variables >> header.clauses >> header.top) ||
				format != "wcnf" || header.variables < 0 || header.clauses < 0 || header.top <= 0)
			{
				error = "invalid WCNF header";
				return false;
			}
			return true;
		}
	}

	error = "missing WCNF header";
	return false;
}

bool load_hard_clauses(const char *filename, const WcnfHeader &header,
	CaDiCaL::Solver &solver, long long &hard_clause_count,
	long long &hard_unit_clause_count, int &max_hard_clause_length,
	const Clock::time_point &deadline, std::string &error)
{
	std::ifstream input(filename);
	if (!input)
	{
		error = "cannot reopen WCNF file";
		return false;
	}

	std::string line;
	while (std::getline(input, line))
	{
		if (Clock::now() >= deadline)
		{
			error = "global cutoff reached while reading hard clauses";
			return false;
		}

		const char *cursor = line.c_str();
		while (*cursor == ' ' || *cursor == '\t')
			++cursor;
		if (!*cursor || *cursor == 'c' || *cursor == 'p')
			continue;

		long long weight = 0;
		if (!parse_integer(cursor, weight))
		{
			error = "invalid clause weight";
			return false;
		}

		if (weight != header.top)
			continue;

		bool terminated = false;
		int clause_length = 0;
		long long parsed_literal = 0;
		while (parse_integer(cursor, parsed_literal))
		{
			if (parsed_literal < -header.variables || parsed_literal > header.variables)
			{
				error = "literal outside declared variable range";
				return false;
			}
			const int literal = static_cast<int>(parsed_literal);
			solver.add(literal);
			if (literal == 0)
			{
				terminated = true;
				break;
			}
			++clause_length;
		}
		if (!terminated)
		{
			error = "unterminated hard clause";
			return false;
		}
		++hard_clause_count;
		if (clause_length == 1)
			++hard_unit_clause_count;
		if (clause_length > max_hard_clause_length)
			max_hard_clause_length = clause_length;
	}
	return true;
}

bool compute_cost(const char *filename, const WcnfHeader &header,
	const std::vector<unsigned char> &assignment, long long &cost,
	const Clock::time_point &deadline, std::string &error)
{
	std::ifstream input(filename);
	if (!input)
	{
		error = "cannot reopen WCNF file for verification";
		return false;
	}

	cost = 0;
	std::string line;
	while (std::getline(input, line))
	{
		if (Clock::now() >= deadline)
		{
			error = "finalization grace exhausted while verifying fallback assignment";
			return false;
		}
		const char *cursor = line.c_str();
		while (*cursor == ' ' || *cursor == '\t')
			++cursor;
		if (!*cursor || *cursor == 'c' || *cursor == 'p')
			continue;

		long long weight = 0;
		if (!parse_integer(cursor, weight))
		{
			error = "invalid clause weight during verification";
			return false;
		}

		bool satisfied = false;
		bool terminated = false;
		long long parsed_literal = 0;
		while (parse_integer(cursor, parsed_literal))
		{
			if (parsed_literal < -header.variables || parsed_literal > header.variables)
			{
				error = "literal outside declared variable range";
				return false;
			}
			const int literal = static_cast<int>(parsed_literal);
			if (literal == 0)
			{
				terminated = true;
				break;
			}
			const int variable = literal < 0 ? -literal : literal;
			const bool value = assignment[variable] != 0;
			if (value == (literal > 0))
				satisfied = true;
		}
		if (!terminated)
		{
			error = "unterminated clause during verification";
			return false;
		}
		if (!satisfied)
		{
			if (weight == header.top)
			{
				error = "CaDiCaL model violates a hard clause";
				return false;
			}
			cost += weight;
		}
	}
	return true;
}
} // namespace

HardSatFallbackResult find_hard_sat_fallback(const char *filename,
	const Clock::time_point &search_deadline,
	const Clock::time_point &finalization_deadline)
{
	HardSatFallbackResult result;
	const Clock::time_point started = Clock::now();
	if (started >= search_deadline)
	{
		result.error = "global cutoff reached before hard-SAT fallback";
		return result;
	}

	WcnfHeader header = {0, 0, 0};
	if (!read_header(filename, header, search_deadline, result.error))
	{
		result.elapsed_seconds = elapsed_since(started);
		return result;
	}
	CaDiCaL::Solver solver;
	solver.reserve(header.variables);
	if (!load_hard_clauses(filename, header, solver, result.hard_clause_count,
		result.hard_unit_clause_count, result.max_hard_clause_length,
		search_deadline, result.error))
	{
		result.elapsed_seconds = elapsed_since(started);
		return result;
	}

	DeadlineTerminator terminator(search_deadline);
	solver.connect_terminator(&terminator);
	const int status = solver.solve();
	solver.disconnect_terminator();
	if (status != 10)
	{
		result.error = status == 20 ? "hard clauses are UNSAT" : "global cutoff reached during hard-SAT solving";
		result.elapsed_seconds = elapsed_since(started);
		return result;
	}

	result.assignment.assign(static_cast<size_t>(header.variables) + 1, 0);
	for (int variable = 1; variable <= header.variables; ++variable)
	{
		if ((variable & 4095) == 0 && Clock::now() >= finalization_deadline)
		{
			result.assignment.clear();
			result.error = "finalization grace exhausted while collecting fallback assignment";
			result.elapsed_seconds = elapsed_since(started);
			return result;
		}
		result.assignment[variable] = solver.val(variable) > 0 ? 1 : 0;
	}

	if (!compute_cost(filename, header, result.assignment, result.cost,
		finalization_deadline, result.error))
	{
		result.assignment.clear();
		result.elapsed_seconds = elapsed_since(started);
		return result;
	}

	result.found = true;
	result.elapsed_seconds = elapsed_since(started);
	return result;
}

void print_hard_sat_assignment(const HardSatFallbackResult &result)
{
	std::cout << "v ";
	for (size_t variable = 1; variable < result.assignment.size(); ++variable)
		std::cout << (result.assignment[variable] ? '1' : '0');
	std::cout << std::endl;
}

bool verify_wcnf_assignment(const char *filename,
	const std::vector<unsigned char> &assignment, long long &cost,
	const Clock::time_point &deadline, std::string &error)
{
	WcnfHeader header = {0, 0, 0};
	if (!read_header(filename, header, deadline, error))
		return false;
	if (assignment.size() != static_cast<size_t>(header.variables) + 1)
	{
		error = "candidate assignment length does not match WCNF header";
		return false;
	}
	return compute_cost(filename, header, assignment, cost, deadline, error);
}
