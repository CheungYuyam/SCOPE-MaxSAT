#include "hard_sat_fallback.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		std::cout << "usage: " << argv[0]
			<< " instance.wcnf solver_output.log" << std::endl;
		return 2;
	}
	std::ifstream input(argv[2]);
	if (!input)
	{
		std::cout << "c batch_external_verified 0" << std::endl;
		std::cout << "c batch_verifier_note cannot open solver output" << std::endl;
		return 2;
	}

	std::string assignment_bits;
	long long reported_cost = 0;
	bool has_cost = false;
	std::string line;
	while (std::getline(input, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.size() > 2 && line[0] == 'v' && line[1] == ' ')
			assignment_bits = line.substr(2);
		else if (line.size() > 2 && line[0] == 'o' && line[1] == ' ')
		{
			std::istringstream fields(line.substr(2));
			long long cost = 0;
			if (fields >> cost)
			{
				reported_cost = cost;
				has_cost = true;
			}
		}
	}

	if (assignment_bits.empty() || !has_cost)
	{
		std::cout << "c batch_external_verified 0" << std::endl;
		std::cout << "c batch_verifier_note no complete candidate in solver output"
			<< std::endl;
		return 1;
	}
	std::vector<unsigned char> assignment(assignment_bits.size() + 1, 0);
	for (size_t i = 0; i < assignment_bits.size(); ++i)
	{
		if (assignment_bits[i] != '0' && assignment_bits[i] != '1')
		{
			std::cout << "c batch_external_verified 0" << std::endl;
			std::cout << "c batch_verifier_note assignment is not binary" << std::endl;
			return 1;
		}
		assignment[i + 1] = assignment_bits[i] == '1' ? 1 : 0;
	}

	long long verified_cost = 0;
	std::string error;
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::minutes(5);
	if (!verify_wcnf_assignment(argv[1], assignment, verified_cost, deadline, error))
	{
		std::cout << "c batch_external_verified 0" << std::endl;
		std::cout << "c batch_verifier_note " << error << std::endl;
		return 1;
	}
	if (verified_cost != reported_cost)
	{
		std::cout << "c batch_external_verified 0" << std::endl;
		std::cout << "c batch_verifier_note reported cost " << reported_cost
			<< " differs from verified cost " << verified_cost << std::endl;
		return 1;
	}

	std::cout << "c batch_external_verified 1" << std::endl;
	std::cout << "c batch_verified_cost " << verified_cost << std::endl;
	return 0;
}
