#include "common/repository.hpp"
#include "common/connection.hpp"
#include "common/protocol.hpp"
#include "fmt/printf.h"
#include <cstdio>
#include <thread>
#include <atomic>
#include <unordered_map>
#include "yaml-cpp/yaml.h"
#include "common/log.hpp"

using namespace std;

struct Server{
    bool IsRunning = true;
    RepositoriesRegistry Registry;
    TcpListener ConnectionListener;

    std::unordered_map<IpAddress, Connection> Connections;

    Server(){
        ConnectionListener.listen(s_DefaultServerPort);
        ConnectionListener.setBlocking(false);
    }

    bool Init(const char *init_filename = "init.yaml"){
        constexpr const char *Open = "Open";
        constexpr const char *Name = "Name";
        constexpr const char *Path = "Path";

        if(!fs::exists(init_filename))
            return Error("Server::Init: File '{}' does not exist", init_filename);
        
        YAML::Node config = YAML::LoadFile(init_filename);

        if(config[Open]){
            for(const YAML::Node &repo: config[Open]){
                if(repo[Name] && repo[Path])
                    Registry.OpenRepository(repo[Path].as<std::string>(), repo[Name].as<std::string>());
                else
                    Error("Server::Init: Repo is ill-formated\n");
            }
        }


        return true;
    }

    void CheckPendingConnections(){
        Connection connection;
        if(ConnectionListener.accept(connection) == Socket::Done){
            
            Log("{}:{} connected", connection.getRemoteAddress().toString(), connection.getRemotePort());

            auto remote = connection.getRemoteAddress();


            RepositoriesInfo info; // XXX excess copying
            info.Repositories.reserve(Registry.Repositories.size());
            for(const auto &repo: Registry.Repositories)
                info.Repositories.push_back({repo.first, repo.second.LastState});

            connection.Send(info);

            Connections.emplace(connection.getRemoteAddress(), std::move(connection));
        }
    }

    void CheckPendingRequests(){
        for(auto &c: Connections){
            
            sf::Packet packet;
            if(c.second.receive(packet) == Socket::Done)
                Log("Connection {}:{} has sent {} bytes\n", c.second.getRemoteAddress().toString(), c.second.getRemotePort(), packet.getDataSize());
        }
    }

    void PollRepositoriesState(){
        for(auto &[name, repo]: Registry.Repositories){
            auto ops = repo.UpdateState();
            if(ops.size())
                PushChanges(name, repo);
        }
    }

    void PushChanges(const std::string &name, const Repository &repo){
        Log("Pushing changes\n");

        for(auto &[addresss, connection]: Connections)
            connection.Send(RepositoryStateNotify{name, repo.LastState});
    }

    void Run(){
        while(IsRunning){
            CheckPendingConnections();
            CheckPendingRequests();

            PollRepositoriesState();

            std::this_thread::sleep_for(1s);
        }
    }
};

int main(){
    Server server;
    if(server.Init())
        server.Run();
}