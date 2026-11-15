// Build and run: clang++ -std=c++17 -Isrc tests/RunProcessTest.cpp src/RunProcess.cpp -o /tmp/rt && /tmp/rt
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include "RunProcess.h"


static void
WriteFile(const std::string& path, const std::string& text)
{
	std::ofstream(path) << text;
}


static std::string
ReadFile(const std::string& path)
{
	std::stringstream stream;
	stream << std::ifstream(path).rdbuf();
	return stream.str();
}


int
main()
{
	char dirTemplate[] = "/tmp/patchwork-test-XXXXXX";
	char* made = mkdtemp(dirTemplate);
	assert(made != NULL);
	std::string dir = dirTemplate;
	// Spaces, quotes and a "$" would break a shell; execvp passes them as-is.
	std::string original = dir + "/it's \"original\" $HOME.txt";
	std::string modified = dir + "/modified copy.txt";
	std::string patch = dir + "/change.patch";
	WriteFile(original, "one\ntwo\nthree\n");
	WriteFile(modified, "one\n2\nthree\nfour\n");

	std::string out, err;
	const char* same[] = { "diff", "-u", original.c_str(), original.c_str(),
		NULL };
	assert(RunProcess(same, out, err) == 0);
	assert(out.empty());

	const char* differ[] = { "diff", "-u", original.c_str(), modified.c_str(),
		NULL };
	assert(RunProcess(differ, out, err) == 1);
	assert(out.find("-two\n+2\n") != std::string::npos);
	assert(out.find("+four\n") != std::string::npos);
	assert(err.empty());
	WriteFile(patch, out);

	std::string missing = dir + "/missing.txt";
	out.clear();
	const char* broken[] = { "diff", "-u", missing.c_str(), modified.c_str(),
		NULL };
	assert(RunProcess(broken, out, err) == 2);
	assert(err.find("missing.txt") != std::string::npos);

	err.clear();
	const char* nosuch[] = { "patchwork-no-such-tool", NULL };
	assert(RunProcess(nosuch, out, err) == 127);
	assert(err.find("patchwork-no-such-tool") != std::string::npos);

	out.clear();
	err.clear();
	const char* apply[] = { "patch", "-N", original.c_str(), patch.c_str(),
		NULL };
	int status = RunProcess(apply, out, err);
	assert(status == 0);
	assert(ReadFile(original) == ReadFile(modified));

	// Applying it again must fail, neither hanging on the "reversed patch?"
	// prompt nor silently reverting the file (BSD patch's default answer).
	out.clear();
	err.clear();
	assert(RunProcess(apply, out, err) != 0);
	assert(ReadFile(original) == ReadFile(modified));

	// Large output on both pipes at once must not deadlock.
	out.clear();
	err.clear();
	const char* flood[] = { "sh", "-c",
		"i=0; while [ $i -lt 20000 ]; do echo out$i; echo err$i >&2;"
		" i=$((i+1)); done", NULL };
	assert(RunProcess(flood, out, err) == 0);
	assert(out.find("out19999\n") != std::string::npos);
	assert(err.find("err19999\n") != std::string::npos);

	std::string cleanup = "rm -rf '" + dir + "'";
	system(cleanup.c_str());
	puts("RunProcessTest: all passed");
	return 0;
}
