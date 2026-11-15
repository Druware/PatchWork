// Build and run: clang++ -std=c++17 -Isrc tests/DiffModelTest.cpp src/DiffModel.cpp src/RunProcess.cpp -o /tmp/dm && /tmp/dm
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

#include "DiffModel.h"


static bool
Same(const Hunk& hunk, int leftStart, int leftCount, int rightStart,
	int rightCount)
{
	return hunk.leftStart == leftStart && hunk.leftCount == leftCount
		&& hunk.rightStart == rightStart && hunk.rightCount == rightCount
		&& hunk.choice == kChooseRight;
}


static void
TestParser()
{
	std::vector<Hunk> hunks;

	assert(ParseNormalDiff("0a1,2\n> x\n> y\n", hunks));
	assert(hunks.size() == 1 && Same(hunks[0], 0, 0, 0, 2));

	assert(ParseNormalDiff("2,3d1\n< a\n< b\n", hunks));
	assert(hunks.size() == 1 && Same(hunks[0], 1, 2, 1, 0));

	assert(ParseNormalDiff("2c2,3\n< b\n---\n> B\n> C\n", hunks));
	assert(hunks.size() == 1 && Same(hunks[0], 1, 1, 1, 2));

	// Content that looks like commands or separators is still content.
	assert(ParseNormalDiff("1a2,3\n> 1c1\n> ---\n4c5\n< > x\n---\n> < y\n"
		"7d7\n< \n", hunks));
	assert(hunks.size() == 3);
	assert(Same(hunks[0], 1, 0, 1, 2));
	assert(Same(hunks[1], 3, 1, 4, 1));
	assert(Same(hunks[2], 6, 1, 7, 0));

	// A bare "<" is an empty line under --suppress-blank-empty.
	assert(ParseNormalDiff("1d0\n<\n", hunks));
	assert(hunks.size() == 1 && Same(hunks[0], 0, 1, 0, 0));

	assert(ParseNormalDiff("2c2\n< b\n\\ No newline at end of file\n---\n> b\n",
		hunks));
	assert(hunks.size() == 1 && Same(hunks[0], 1, 1, 1, 1));
	assert(ParseNormalDiff("2c2\n< b\n---\n> b\n\\ No newline at end of file\n",
		hunks));
	assert(hunks.size() == 1 && Same(hunks[0], 1, 1, 1, 1));

	assert(ParseNormalDiff("", hunks) && hunks.empty());

	// Malformed: unknown command, wrong counts, missing "---", stray text.
	assert(!ParseNormalDiff("2x3\n< a\n---\n> b\n", hunks));
	assert(!ParseNormalDiff("1c1\n< a\n---\n", hunks));
	assert(!ParseNormalDiff("1,2c1\n< a\n---\n> b\n", hunks));
	assert(!ParseNormalDiff("1c1\n< a\n> b\n", hunks));
	assert(!ParseNormalDiff("1a1\n< a\n", hunks));
	assert(!ParseNormalDiff("> a\n", hunks));
	assert(!ParseNormalDiff("1a2\n> a", hunks));
	assert(!ParseNormalDiff("3,2d1\n", hunks));
	assert(!ParseNormalDiff("Binary files a and b differ\n", hunks));
}


static Comparison
Make(const std::string& left, const std::string& right, Hunk hunk)
{
	Comparison comparison;
	comparison.left = SplitLines(left);
	comparison.right = SplitLines(right);
	comparison.hunks.push_back(hunk);
	return comparison;
}


static std::string
Merge(Comparison comparison, Choice choice)
{
	for (Hunk& hunk : comparison.hunks)
		hunk.choice = choice;
	return BuildMerge(comparison);
}


static void
TestMerge()
{
	assert(SplitLines("").empty());
	assert(SplitLines("a").size() == 1);
	assert(SplitLines("a\n\nb").size() == 3);

	Comparison change = Make("a\nb\nc\n", "a\nB\nc\n", { 1, 1, 1, 1,
		kChooseRight });
	assert(Merge(change, kChooseLeft) == "a\nb\nc\n");
	assert(Merge(change, kChooseRight) == "a\nB\nc\n");
	assert(Merge(change, kChooseBothLeftFirst) == "a\nb\nB\nc\n");
	assert(Merge(change, kChooseBothRightFirst) == "a\nB\nb\nc\n");
	assert(Merge(change, kChooseNeither) == "a\nc\n");

	std::vector<int> lines;
	BuildMerge(change, &lines);
	assert(lines.size() == 1 && lines[0] == 1);

	// A line missing its '\n' gets one only when something follows it.
	Comparison tail = Make("a\nb", "a\nc\n", { 1, 1, 1, 1, kChooseRight });
	assert(Merge(tail, kChooseLeft) == "a\nb");
	assert(Merge(tail, kChooseBothLeftFirst) == "a\nb\nc\n");
	assert(Merge(tail, kChooseBothRightFirst) == "a\nc\nb");
	assert(Merge(tail, kChooseNeither) == "a\n");

	// Insertion and deletion hunks.
	Comparison add = Make("a\n", "a\nx\ny\n", { 1, 0, 1, 2, kChooseRight });
	assert(Merge(add, kChooseLeft) == "a\n");
	assert(Merge(add, kChooseBothRightFirst) == "a\nx\ny\n");
	Comparison remove = Make("a\nb\n", "b\n", { 0, 1, 0, 0, kChooseRight });
	assert(Merge(remove, kChooseRight) == "b\n");
	assert(Merge(remove, kChooseBothLeftFirst) == "a\nb\n");
}


static void
TestMapLine()
{
	// left: 0 1 [2 3] 4 ; right: 0 1 [2 3 4 5] 6 ; then right inserts at 7.
	std::vector<Hunk> hunks = { { 2, 2, 2, 4, kChooseRight },
		{ 5, 0, 7, 3, kChooseRight } };
	assert(MapLine(hunks, 1, true) == 1);
	assert(MapLine(hunks, 3, true) == 4);
	assert(MapLine(hunks, 4, true) == 6);
	assert(MapLine(hunks, 5, true) == 10);
	assert(MapLine(hunks, 6, true) == 11);
	assert(MapLine(hunks, 6, false) == 4);
	assert(MapLine(hunks, 8, false) == 5);
	assert(MapLine(hunks, 11, false) == 6);
	assert(MapLine({}, 7.5, true) == 7.5);
}


static void
WriteFile(const std::string& path, const std::string& text)
{
	std::ofstream(path, std::ios::binary) << text;
}


static void
TestRoundTrips(const std::string& dir)
{
	const char* cases[][2] = {
		{ "", "one\ntwo\n" },
		{ "one\ntwo\n", "" },
		{ "a\nb", "a\nb\n" },
		{ "a\nb\n", "a\nb" },
		{ "a\nb", "a\nc" },
		{ "x\r\ny\r\nz\r\n", "x\r\nY\r\nz\r\nw\r\n" },
		{ "< a\n> b\n---\n1c1\n\\ x\n", "> a\n< b\n---\n---\n2d1\n" },
		{ "1\n2\n3\n4\n5\n6\n7\n8\n9\n", "0\n1\n2\nthree\n4\n6\n7\n8\n9\nten" },
		{ "\n\n\n", "\n\nx\n\n" },
	};
	std::string left = dir + "/-left file.txt";	// "--" keeps "-" an operand
	std::string right = dir + "/right's file.txt";
	for (const auto& texts : cases) {
		WriteFile(left, texts[0]);
		WriteFile(right, texts[1]);
		Comparison comparison;
		std::string error;
		int status = CompareFiles(left.c_str(), right.c_str(), comparison,
			error);
		if (status != 1)
			fprintf(stderr, "status %d: %s\n", status, error.c_str());
		assert(status == 1);
		assert(!comparison.hunks.empty());
		assert(Merge(comparison, kChooseLeft) == texts[0]);
		assert(Merge(comparison, kChooseRight) == texts[1]);
	}

	Comparison comparison;
	std::string error;
	WriteFile(left, "same\n");
	WriteFile(right, "same\n");
	assert(CompareFiles(left.c_str(), right.c_str(), comparison, error) == 0);

	WriteFile(left, std::string("bin\0ary\n", 8));
	WriteFile(right, std::string("bin\0ARY\n", 8));
	error.clear();
	assert(CompareFiles(left.c_str(), right.c_str(), comparison, error) == 2);
	assert(error.find("differ") != std::string::npos);

	error.clear();
	assert(CompareFiles((dir + "/missing").c_str(), right.c_str(), comparison,
		error) == 2);
	assert(error.find("missing") != std::string::npos);
	error.clear();
	assert(CompareFiles(dir.c_str(), right.c_str(), comparison, error) == 2);
	assert(error.find("not a regular file") != std::string::npos);
}


int
main()
{
	TestParser();
	TestMerge();
	TestMapLine();

	char dirTemplate[] = "/tmp/patchwork-model-XXXXXX";
	assert(mkdtemp(dirTemplate) != NULL);
	std::string dir = dirTemplate;
	TestRoundTrips(dir);
	std::string cleanup = "rm -rf '" + dir + "'";
	system(cleanup.c_str());

	puts("DiffModelTest: all passed");
	return 0;
}
