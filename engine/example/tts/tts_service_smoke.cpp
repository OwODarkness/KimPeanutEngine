#include "tts_example.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: TTSServiceSmoke <server-side-reference-audio-path>\n";
        return 2;
    }

    if (!kpengine::example::TTSExample(std::string(argv[1])))
    {
        std::cerr << "TTS synthesis or audible drain failed\n";
        return 1;
    }

    std::cout << "TTS response played and drained\n";
    return 0;
}
