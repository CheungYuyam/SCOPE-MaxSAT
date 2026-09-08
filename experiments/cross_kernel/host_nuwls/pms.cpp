#include "basis_pms.h"
#include "build.h"
#include "pms.h"
#include "heuristic.h"
#include <signal.h>

#define NUWLS_BUILD_ID "official-filyouzicha-nuwls-instrumented-cutoff"

ISDist s;
int seed = 1;
long long best_known;
long long total_step = 0;
long long consecutive_better_soft = 0;
char * file_name = NULL;
void interrupt(int sig)
{
	cout << "c termination signal " << sig << endl;
	if (s.has_feasible_solution())
	{
		const bool verified = s.verify_sol();
		cout << "c final internal_verified " << (verified ? 1 : 0) << endl;
		if (verified)
			s.print_best_solution();
	}
	else
		cout << "c final no_feasible_solution" << endl;
	s.free_memory();
	exit(10);
}

int main(int argc, char *argv[])
{
	start_timing();
	cout << "c build " << NUWLS_BUILD_ID << endl;
	cout << "c upstream_repository https://github.com/filyouzicha/NuWLS" << endl;
	cout << "c upstream_commit 62e858063e867b8d8fda219e629daf908156d7dc" << endl;
	cout << "c instrumentation search_logic_unchanged cutoff_and_final_output_only" << endl;
	cout << "c concurrent_search_processes 1" << endl;
	if (argc < 4)
	{
		cout << "usage: " << argv[0] << " instance.wcnf seed cutoff_seconds" << endl;
		return 1;
	}

	signal(SIGTERM, interrupt);

	sscanf(argv[2], "%d", &seed);
	srand(seed);
	const double allowed_time = strtod(argv[3], NULL);
	if (allowed_time <= 0.0)
	{
		cout << "cutoff_seconds must be positive" << endl;
		return 1;
	}
	s.build_instance(argv[1]);

	s.settings(allowed_time);

	s.parse_parameters2(argc, argv);
	s.local_search_with_decimation(argv[1]);

	if (s.has_feasible_solution())
	{
		const bool verified = s.verify_sol();
		cout << "c final internal_verified " << (verified ? 1 : 0) << endl;
		if (verified)
			s.print_best_solution();
	}
	else
		cout << "c final no_feasible_solution" << endl;
	s.free_memory();

	return 0;
}
