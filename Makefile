## Haiku makefile-engine Makefile for PatchWork.
## Build on Haiku with: make

NAME = PatchWork
TYPE = APP
SRCS = src/App.cpp src/CompareWindow.cpp src/DiffModel.cpp src/MergeWindow.cpp \
	src/RunProcess.cpp
RDEFS = PatchWork.rdef
LIBS = be tracker $(STDCPPLIBS)
LOCAL_INCLUDE_PATHS = src
OPTIMIZE := FULL
WARNINGS = ALL

## Include the Makefile-Engine
DEVEL_DIRECTORY := \
	$(shell findpaths -r "makefile_engine" B_FIND_PATH_DEVELOP_DIRECTORY)
include $(DEVEL_DIRECTORY)/etc/makefile-engine
