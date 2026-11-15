#include "DiffModel.h"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "RunProcess.h"


std::vector<std::string>
SplitLines(const std::string& text)
{
	std::vector<std::string> lines;
	size_t start = 0;
	while (start < text.size()) {
		size_t end = text.find('\n', start);
		end = end == std::string::npos ? text.size() : end + 1;
		lines.push_back(text.substr(start, end - start));
		start = end;
	}
	return lines;
}


/** Reads "N" or "N,M" at line[pos]; advances pos. */
static bool
ParseRange(const std::string& line, size_t& pos, int& first, int& last)
{
	if (pos >= line.size() || !isdigit((unsigned char)line[pos]))
		return false;
	first = 0;
	while (pos < line.size() && isdigit((unsigned char)line[pos]))
		first = first * 10 + (line[pos++] - '0');
	last = first;
	if (pos < line.size() && line[pos] == ',') {
		pos++;
		if (pos >= line.size() || !isdigit((unsigned char)line[pos]))
			return false;
		last = 0;
		while (pos < line.size() && isdigit((unsigned char)line[pos]))
			last = last * 10 + (line[pos++] - '0');
	}
	return first <= last;
}


/** Parses one command line ("2,3c4") into a hunk. */
static bool
ParseCommand(const std::string& line, Hunk& hunk)
{
	size_t pos = 0;
	int l1, l2, r1, r2;
	if (!ParseRange(line, pos, l1, l2) || pos >= line.size())
		return false;
	char command = line[pos++];
	if (!ParseRange(line, pos, r1, r2) || pos != line.size())
		return false;

	hunk.choice = kChooseRight;
	switch (command) {
		case 'a':	// after left line l1, add right lines r1..r2
			if (l1 != l2 || r1 < 1)
				return false;
			hunk.leftStart = l1;
			hunk.leftCount = 0;
			hunk.rightStart = r1 - 1;
			hunk.rightCount = r2 - r1 + 1;
			return true;
		case 'd':	// delete left lines l1..l2, right continues after r1
			if (r1 != r2 || l1 < 1)
				return false;
			hunk.leftStart = l1 - 1;
			hunk.leftCount = l2 - l1 + 1;
			hunk.rightStart = r1;
			hunk.rightCount = 0;
			return true;
		case 'c':
			if (l1 < 1 || r1 < 1)
				return false;
			hunk.leftStart = l1 - 1;
			hunk.leftCount = l2 - l1 + 1;
			hunk.rightStart = r1 - 1;
			hunk.rightCount = r2 - r1 + 1;
			return true;
	}
	return false;
}


/** "< text", or a bare "<" for an empty line (--suppress-blank-empty). */
static bool
IsContent(const std::string& line, char marker)
{
	return !line.empty() && line[0] == marker
		&& (line.size() == 1 || line[1] == ' ');
}


bool
ParseNormalDiff(const std::string& output, std::vector<Hunk>& hunks)
{
	hunks.clear();
	// Count the "<" and ">" lines so a misread command cannot slip through.
	int leftSeen = 0;
	int rightSeen = 0;
	bool separator = false;
	std::vector<std::string> lines = SplitLines(output);
	for (size_t i = 0; i <= lines.size(); i++) {
		std::string line;
		if (i < lines.size()) {
			line = lines[i];
			if (line.empty() || line.back() != '\n')
				return false;
			line.pop_back();
		}
		bool isCommand = i < lines.size() && !line.empty()
			&& isdigit((unsigned char)line[0]);
		if (i == lines.size() || isCommand) {
			if (!hunks.empty()) {
				const Hunk& last = hunks.back();
				if (leftSeen != last.leftCount || rightSeen != last.rightCount
					|| separator != (last.leftCount > 0 && last.rightCount > 0))
					return false;
			}
			if (i == lines.size())
				break;
			Hunk hunk;
			if (!ParseCommand(line, hunk))
				return false;
			hunks.push_back(hunk);
			leftSeen = rightSeen = 0;
			separator = false;
		} else if (hunks.empty())
			return false;
		else if (IsContent(line, '<') && !separator)
			leftSeen++;
		else if (IsContent(line, '>'))
			rightSeen++;
		else if (line == "---" && !separator && rightSeen == 0)
			separator = true;
		else if (line.empty() || line[0] != '\\')	// "\ No newline at end of file"
			return false;
	}
	return true;
}


static bool
ReadFile(const char* path, std::vector<std::string>& lines, std::string& error)
{
	struct stat info;
	if (stat(path, &info) != 0) {
		error = std::string(path) + ": " + strerror(errno);
		return false;
	}
	if (!S_ISREG(info.st_mode)) {
		error = std::string(path) + ": not a regular file";
		return false;
	}
	std::ifstream file(path, std::ios::binary);
	std::stringstream text;
	text << file.rdbuf();
	if (!file) {
		error = std::string(path) + ": could not be read";
		return false;
	}
	lines = SplitLines(text.str());
	return true;
}


int
CompareFiles(const char* leftPath, const char* rightPath, Comparison& result,
	std::string& error)
{
	result = Comparison();
	if (!ReadFile(leftPath, result.left, error)
		|| !ReadFile(rightPath, result.right, error))
		return 2;

	const char* argv[] = { "diff", "--", leftPath, rightPath, NULL };
	std::string out;
	// shortcut: runs on the caller's thread, so huge files pause the UI;
	// move to a worker thread if that ever matters.
	int status = RunProcess(argv, out, error);
	if (status == 2 && error.empty())
		error = out;	// GNU diff reports "Binary files … differ" on stdout
	if (status != 1)
		return status;

	if (!ParseNormalDiff(out, result.hunks) || result.hunks.empty()) {
		// e.g. "Binary files a and b differ", which GNU diff exits 1 for
		error = "Could not understand diff's output:\n"
			+ out.substr(0, out.find('\n'));
		return 2;
	}
	for (const Hunk& hunk : result.hunks) {
		if (hunk.leftStart + hunk.leftCount > (int)result.left.size()
			|| hunk.rightStart + hunk.rightCount > (int)result.right.size()) {
			error = "diff's output does not match the files; did one change?";
			return 2;
		}
	}
	return 1;
}


std::string
BuildMerge(const Comparison& comparison, std::vector<int>* hunkLines)
{
	std::string merged;
	int lineCount = 0;
	auto add = [&](const std::vector<std::string>& lines, int start,
		int count) {
		for (int i = start; i < start + count; i++) {
			if (!merged.empty() && merged.back() != '\n')
				merged += '\n';
			merged += lines[i];
			lineCount++;
		}
	};

	if (hunkLines != NULL)
		hunkLines->clear();
	int next = 0;
	for (const Hunk& hunk : comparison.hunks) {
		add(comparison.left, next, hunk.leftStart - next);
		if (hunkLines != NULL)
			hunkLines->push_back(lineCount);
		if (hunk.choice == kChooseLeft || hunk.choice == kChooseBothLeftFirst)
			add(comparison.left, hunk.leftStart, hunk.leftCount);
		if (hunk.choice == kChooseRight || hunk.choice == kChooseBothLeftFirst
			|| hunk.choice == kChooseBothRightFirst)
			add(comparison.right, hunk.rightStart, hunk.rightCount);
		if (hunk.choice == kChooseBothRightFirst)
			add(comparison.left, hunk.leftStart, hunk.leftCount);
		next = hunk.leftStart + hunk.leftCount;
	}
	add(comparison.left, next, (int)comparison.left.size() - next);
	return merged;
}


double
MapLine(const std::vector<Hunk>& hunks, double line, bool fromLeft)
{
	// Each hunk's start pair also fixes the offset of the common run before it.
	double from = 0;
	double to = 0;
	for (const Hunk& hunk : hunks) {
		from = fromLeft ? hunk.leftStart : hunk.rightStart;
		to = fromLeft ? hunk.rightStart : hunk.leftStart;
		double fromCount = fromLeft ? hunk.leftCount : hunk.rightCount;
		double toCount = fromLeft ? hunk.rightCount : hunk.leftCount;
		if (line < from)
			return line - from + to;
		if (line < from + fromCount)
			return to + (line - from) * toCount / fromCount;
		from += fromCount;
		to += toCount;
	}
	return line - from + to;
}
