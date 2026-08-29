#include <iostream>
#include <string>

#include "command/command_agent_endpoint.h"

int main()
{
    kpengine::runtime::command::CommandRegistry registry;
    kpengine::runtime::command::CommandAgentEndpoint endpoint{registry};
    std::string line;
    while (std::getline(std::cin, line))
    {
        std::cout << endpoint.HandleJsonLine(line) << '\n';
    }
    return 0;
}
