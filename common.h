#ifndef COMMON_H
#define COMMON_H

#include <vector>
#include <string>

#define DINFO		("[I]  ")
#define DWARNING	("[W]  ")
#define DERROR		("[E]  ")
#define DINDENT		("     ")

//#define PRINT_VERBOSE

#ifdef PRINT_VERBOSE
#define V_PRINT(x) \
			std::cout << DINFO << x << std::endl;
#else
#define V_PRINT(x)
#endif

struct NDSDirectory {
	unsigned short firstFileID;
	unsigned short directoryID;
	std::string dirName;
	std::vector<std::string> files;
	std::vector<NDSDirectory> dirs;
};

#endif  // COMMON_H