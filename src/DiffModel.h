#ifndef DIFF_MODEL_H
#define DIFF_MODEL_H

#include <string>
#include <vector>

/** Which side(s) of a difference go into the merge. Order matches the menus. */
enum Choice {
	kChooseLeft,
	kChooseRight,
	kChooseBothLeftFirst,
	kChooseBothRightFirst,
	kChooseNeither
};

/**
 * One difference. Starts are 0-based line indexes; a side with count 0 is an
 * insertion point: the hunk sits before that side's line `start`.
 */
struct Hunk {
	int		leftStart;
	int		leftCount;
	int		rightStart;
	int		rightCount;
	Choice	choice;
};

/** Both files split into lines (each keeps its '\n'; the last may lack it). */
struct Comparison {
	std::vector<std::string>	left;
	std::vector<std::string>	right;
	std::vector<Hunk>			hunks;
};

/** Splits text after each '\n'. "" gives no lines; "a" gives one. */
std::vector<std::string> SplitLines(const std::string& text);

/**
 * Parses `diff` normal-format output ("2,3c4", "5a6,7", "8d9" commands with
 * their "<", "---", ">" and "\ No newline" lines) into hunks with choice
 * kChooseRight. Returns false on anything it does not understand.
 */
bool ParseNormalDiff(const std::string& output, std::vector<Hunk>& hunks);

/**
 * Runs `diff left right` and fills result.
 * @return diff's status: 0 identical, 1 different (result filled), anything
 *         else is an error described in error.
 */
int CompareFiles(const char* leftPath, const char* rightPath,
	Comparison& result, std::string& error);

/**
 * Builds the merged text: left's common lines plus each hunk's chosen lines.
 * A line without a final '\n' gets one when another line follows it.
 * @param hunkLines if not NULL, receives the merged line index of each hunk.
 */
std::string BuildMerge(const Comparison& comparison,
	std::vector<int>* hunkLines = NULL);

/**
 * Maps a (fractional) line of one side to the other side: common lines map
 * one to one, lines inside a hunk proportionally. Used to keep panes aligned.
 */
double MapLine(const std::vector<Hunk>& hunks, double line, bool fromLeft);

#endif
