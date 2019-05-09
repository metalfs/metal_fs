#include <utility>

#pragma once

#include <string>
#include <unordered_set>
#include <Messages.pb.h>
#include <metal_pipeline/operator_registry.hpp>
#include "agent_pool.hpp"

void* start_socket(void* args);

namespace metal {

class MessageHeader;

class Server {
public:

    explicit Server(std::string socketFileName);
    virtual ~Server();

    static void start(std::string socket_file_name, int card);

protected:
    void startInternal(int card);
    void process_request(int connfd, int card);

    AgentPool _agents;
    std::string _socketFileName;
    int _listenfd;
};
} // namespace metal
