#pragma once

#ifdef _WIN32
    #ifdef BLZ_EXPORTS
        #define BLZ_API __declspec(dllexport)
    #else
        #define BLZ_API __declspec(dllimport)
    #endif
#elif defined __linux__
    #define BLZ_API
#else
    #error "Architecture not compatible"
#endif

#include <vector>


namespace BLZ {

    /*
        Compresses the given vector and adds an appropriate header at the end of the compressed block.
    */
    BLZ_API void compress(std::vector<unsigned char>& data);


    /*
        Decompresses the given vector. Input data must have the header at the end.
    */
    BLZ_API void decompress(std::vector<unsigned char>& data);

}

