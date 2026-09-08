#include "basis_pms.h"
#include "build.h"
#include "pms.h"
#include "heuristic.h"
#include <signal.h>

#ifdef NUWLS_QUALITY_SEARCH
#define NUWLS_BUILD_ID "scope-maxsat-two-core-refinement"
#else
#define NUWLS_BUILD_ID "aaai23-deterministic-baseline-v1"
#endif

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
		bool verified = s.verify_sol();
		cout << "c final internal_verified " << (verified ? 1 : 0) << endl;
		if (verified)
			s.print_best_solution();
	}
	else
	{
		cout << "c final no_feasible_solution" << endl;
	}
	s.free_memory();
	exit(10);
}

int main(int argc, char *argv[])
{
	start_timing();
	cout << "c build " << NUWLS_BUILD_ID << endl;

	signal(SIGTERM, interrupt);
	if (argc < 4)
	{
		cout << "usage: " << argv[0] << " instance.wcnf seed cutoff_seconds" << endl;
		return 1;
	}

	sscanf(argv[2], "%d", &seed);
	srand(seed);
	double allowed_time = strtod(argv[3], NULL);
	for (int i = 4; i + 1 < argc; ++i)
	{
		if (strcmp(argv[i], "-deadline_epoch_ms") == 0)
		{
			const long long deadline_epoch_milliseconds = strtoll(argv[i + 1], NULL, 10);
			const long long now_epoch_milliseconds =
				chrono::duration_cast<chrono::milliseconds>(
					chrono::system_clock::now().time_since_epoch()).count();
			const double global_remaining =
				static_cast<double>(deadline_epoch_milliseconds - now_epoch_milliseconds) / 1000.0;
			const double cutoff_from_core_start = get_runtime() + global_remaining;
			if (cutoff_from_core_start < allowed_time)
				allowed_time = cutoff_from_core_start;
			++i;
		}
	}
	if (allowed_time <= 0.0)
	{
		cout << "cutoff_seconds must be positive" << endl;
		return 1;
	}
	s.build_instance(argv[1]);

	s.settings(allowed_time);

#ifdef NUWLS_QUALITY_SEARCH
	if (!s.parse_parameters2(argc, argv))
	{
		cout << "c invalid quality-search parameter" << endl;
		s.free_memory();
		return 1;
	}
#else
	s.parse_parameters2(argc, argv);
#endif
	s.local_search_with_decimation(argv[1]);

	if (s.has_feasible_solution())
	{
		bool verified = s.verify_sol();
		cout << "c final internal_verified " << (verified ? 1 : 0) << endl;
		if (verified)
			s.print_best_solution();
	}
	else
	{
		cout << "c final no_feasible_solution" << endl;
	}
	s.free_memory();

	return 0;
}
