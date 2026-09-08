#ifndef HARD_SAT_FALLBACK_H
#define HARD_SAT_FALLBACK_H

#include <chrono>
#include <string>
#include <vector>

struct HardSatFallbackResult
{
	bool found;
	long long cost;
	double elapsed_seconds;
	long long hard_clause_count;
	long long hard_unit_clause_count;
	int max_hard_clause_length;
	std::vector<unsigned char> assignment;
	std::string error;

	HardSatFallbackResult()
		: found(false), cost(0), elapsed_seconds(0.0), hard_clause_count(0),
		  hard_unit_clause_count(0), max_hard_clause_length(0)
	{
	}
};

HardSatFallbackResult find_hard_sat_fallback(const char *filename,
	const std::chrono::steady_clock::time_point &search_deadline,
	const std::chrono::steady_clock::time_point &finalization_deadline);
void print_hard_sat_assignment(const HardSatFallbackResult &result);
bool verify_wcnf_assignment(const char *filename,
	const std::vector<unsigned char> &assignment, long long &cost,
	const std::chrono::steady_clock::time_point &deadline, std::string &error);

#endif
