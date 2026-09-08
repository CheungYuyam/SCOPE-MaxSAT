#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
struct Clause
{
	long long weight;
	std::vector<int> literals;
};

bool load_wcnf(const char *path, int &variables, long long &top,
	std::vector<Clause> &clauses, std::string &error)
{
	std::ifstream input(path);
	if (!input)
	{
		error = "cannot open WCNF file";
		return false;
	}
	long long declared_clauses = -1;
	bool header_seen = false;
	std::string line;
	while (std::getline(input, line))
	{
		std::istringstream fields(line);
		std::string marker;
		if (!(fields >> marker) || marker == "c")
			continue;
		if (marker == "p")
		{
			std::string format;
			if (!(fields >> format >> variables >> declared_clauses >> top) ||
				format != "wcnf" || variables < 0 || declared_clauses < 0 || top <= 0)
			{
				error = "invalid WCNF header";
				return false;
			}
			header_seen = true;
			continue;
		}
		if (!header_seen)
		{
			error = "clause before WCNF header";
			return false;
		}
		std::istringstream clause_fields(line);
		Clause clause;
		if (!(clause_fields >> clause.weight) || clause.weight <= 0)
		{
			error = "invalid clause weight";
			return false;
		}
		int literal = 0;
		bool terminated = false;
		while (clause_fields >> literal)
		{
			if (literal == 0)
			{
				terminated = true;
				break;
			}
			if (literal < -variables || literal > variables)
			{
				error = "literal outside declared variable range";
				return false;
			}
			clause.literals.push_back(literal);
		}
		if (!terminated)
		{
			error = "unterminated clause";
			return false;
		}
		clauses.push_back(clause);
	}
	if (!header_seen)
		error = "missing WCNF header";
	else if (static_cast<long long>(clauses.size()) != declared_clauses)
		error = "clause count differs from WCNF header";
	return error.empty();
}

bool parse_assignment(const std::string &text, int variables,
	std::vector<unsigned char> &assignment)
{
	std::istringstream fields(text);
	std::vector<std::string> tokens;
	std::string token;
	while (fields >> token)
		tokens.push_back(token);
	if (tokens.size() == 1 && static_cast<int>(tokens[0].size()) == variables)
	{
		assignment.assign(static_cast<size_t>(variables) + 1, 0);
		for (int v = 1; v <= variables; ++v)
		{
			const char bit = tokens[0][static_cast<size_t>(v - 1)];
			if (bit != '0' && bit != '1')
				return false;
			assignment[v] = static_cast<unsigned char>(bit - '0');
		}
		return true;
	}

	assignment.assign(static_cast<size_t>(variables) + 1, 0);
	std::vector<unsigned char> assigned(static_cast<size_t>(variables) + 1, 0);
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		char *end = NULL;
		const long value = std::strtol(tokens[i].c_str(), &end, 10);
		if (!end || *end || value == 0)
			continue;
		const long variable = value < 0 ? -value : value;
		if (variable > variables)
			return false;
		assignment[static_cast<size_t>(variable)] = value > 0 ? 1 : 0;
		assigned[static_cast<size_t>(variable)] = 1;
	}
	for (int v = 1; v <= variables; ++v)
		if (!assigned[v])
			return false;
	return true;
}

bool load_log(const char *path, int variables,
	std::vector<unsigned char> &assignment, long long &reported_cost,
	bool &has_reported_cost, std::string &error)
{
	std::ifstream input(path);
	if (!input)
	{
		error = "cannot open solver log";
		return false;
	}
	std::string assignment_text;
	bool has_paper_cost = false;
	std::string line;
	while (std::getline(input, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.size() > 2 && line[0] == 'v' && line[1] == ' ')
			assignment_text = line.substr(2);
		else if (line.compare(0, 24, "c paper_table_best_cost ") == 0)
		{
			std::istringstream fields(line.substr(24));
			if (fields >> reported_cost)
			{
				has_reported_cost = true;
				has_paper_cost = true;
			}
		}
		else if (line.size() > 2 && line[0] == 'o' && line[1] == ' ' &&
			!has_paper_cost)
		{
			std::istringstream fields(line.substr(2));
			if (fields >> reported_cost)
				has_reported_cost = true;
		}
	}
	if (assignment_text.empty())
	{
		error = "solver log has no assignment line";
		return false;
	}
	if (!parse_assignment(assignment_text, variables, assignment))
	{
		error = "invalid or incomplete assignment in solver log";
		return false;
	}
	return true;
}
} // namespace

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		std::cerr << "usage: " << argv[0] << " instance.wcnf solver.log" << std::endl;
		return 2;
	}
	int variables = 0;
	long long top = 0;
	std::vector<Clause> clauses;
	std::string error;
	if (!load_wcnf(argv[1], variables, top, clauses, error))
	{
		std::cout << "c external_verified 0" << std::endl;
		std::cout << "c external_verify_error " << error << std::endl;
		return 2;
	}

	std::vector<unsigned char> assignment;
	long long reported_cost = 0;
	bool has_reported_cost = false;
	if (!load_log(argv[2], variables, assignment, reported_cost,
		has_reported_cost, error))
	{
		std::cout << "c external_verified 0" << std::endl;
		std::cout << "c external_verify_error " << error << std::endl;
		return 1;
	}

	long long cost = 0;
	long long hard_violations = 0;
	for (size_t c = 0; c < clauses.size(); ++c)
	{
		bool satisfied = false;
		for (size_t i = 0; i < clauses[c].literals.size(); ++i)
		{
			const int literal = clauses[c].literals[i];
			const bool value = assignment[static_cast<size_t>(
				literal < 0 ? -literal : literal)] != 0;
			if ((literal > 0 && value) || (literal < 0 && !value))
			{
				satisfied = true;
				break;
			}
		}
		if (!satisfied)
		{
			if (clauses[c].weight == top)
				++hard_violations;
			else if (cost > std::numeric_limits<long long>::max() - clauses[c].weight)
			{
				std::cout << "c external_verified 0" << std::endl;
				std::cout << "c external_verify_error soft cost overflow" << std::endl;
				return 2;
			}
			else
				cost += clauses[c].weight;
		}
	}

	const bool cost_matches = !has_reported_cost || cost == reported_cost;
	const bool verified = hard_violations == 0 && cost_matches;
	std::cout << "c external_verified " << (verified ? 1 : 0) << std::endl;
	std::cout << "c external_cost " << cost << std::endl;
	std::cout << "c external_hard_violations " << hard_violations << std::endl;
	std::cout << "c external_reported_cost_present " << (has_reported_cost ? 1 : 0)
		<< std::endl;
	if (has_reported_cost)
		std::cout << "c external_reported_cost " << reported_cost << std::endl;
	if (!cost_matches)
		std::cout << "c external_verify_error reported cost mismatch" << std::endl;
	return verified ? 0 : 1;
}
