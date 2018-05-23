
#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/prepared_statement.h>
#include "sns.grpc.pb.h"

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce438::Message;
using csce438::ListReply;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;
using csce438::Simple_string;
using csce438::Host_port;
using csce438::RouterOp;
using csce438::RouterMessage;



//a struct to keep server information(usually gets pushed into servers<> vector)
struct ServerInfo
{
    std::string host;
    std::string port;
    bool active;
};



class SNSServiceImpl final : public SNSService::Service
{
public:
    SNSServiceImpl()
        : available(-1)
        , servers()
    {
    }



private:

    // BidirectionalStream rpc
    // between router and server when server becomes alive for the first time and
    //sends its credentials to router
    Status Bidirectional_r_and_s(ServerContext* context,
                                 ServerReaderWriter<RouterMessage, RouterMessage>* stream) override
    {
        ServerInfo s;
        RouterMessage rm;

        //this while loop continuously reading for upcoming server connections
        //and register them in servers placeholder
        while (stream->Read(&rm))
        {
            //just server connects to router
            if (rm.operation() == csce438::CONNECT)
            {
                s.host = rm.hp().host();
                s.port = rm.hp().port();
                s.active = true;
                servers.push_back(s);
                std::cout << s.host + ":" + s.port + " has connected." << std::endl;
            }
            //when server asks router to send him all the other two VMs IP
            else if (rm.operation() == csce438::BCAST)
            {
                std::vector<std::string> added_hosts;

                for (const ServerInfo& server : servers)
                {
                    if (s.host != server.host)
                    {
                        bool added = false;

                        // only send to one port on each host
                        for (const auto& host : added_hosts)
                        {
                            if (host == server.host)
                            {
                                added = true;
                            }
                        }

                        if (!added)
                        {
                            added_hosts.push_back(server.host);
                            Host_port* hp = rm.add_machines();
                            hp->set_host(server.host);
                            hp->set_port(server.port);
                        }
                    }
                }

                stream->Write(rm);
                rm.Clear();
            }
            //this is none
            else if (rm.operation() == csce438::NONE)
            {
            }
            else
            {
            }
        }

        std::cout << s.host + ":" + s.port + " has disconnected." << std::endl;

        //a server just exited, deactivate that server index
        for (int i = 0; i < servers.size(); i++)
        {
            if (s.port == servers[i].port && s.host == servers[i].host && servers[i].active)
            {
                servers[i].active = false;
                s.active = false;
                servers.erase(servers.begin() + i);
                break;
            }
        }

        std::cout << s.host + ":" + s.port + " has been deactivatd" << std::endl;
        return Status::OK;
    }



    //Onetime rpc
    //between router and client - when client asks for an available server
    Status Onetime_c_to_r(ServerContext* context, const Simple_string* request,
                          Host_port* reply) override
    {
        //this if statement is triggered when client asking for master1
        //simply iterates through servers<> vector and just picks an active one
        if (request->str() == "give_me_a_master_1")
        {
            Host_port hp = get_server(request->master_host(), request->master_port());

            //sending out reply
            if (hp.host() != "" && hp.port() != "")
            {
                reply->set_host(hp.host());
                reply->set_port(hp.port());
                return Status::OK;
            }
            else
            {
                //no avail master2
                reply->set_host("");
                reply->set_port("");
                return Status::OK;
            }
        }

        //this if statement is triggered when client asking for master2
        //iterates through the servers<> vector and picks one master2 from diff VM
        if (request->str() == "give_me_a_master_2")
        {
            Host_port hp = get_server(request->master_host(), request->master_port());

            if (available != -1)
            {
                reply->set_host(hp.host());
                reply->set_port(hp.port());
                std::cout << "a master2 has been given to user=" + hp.host() + ":" + hp.port() << std::endl;
                return Status::OK;
            }
            else
            {
                //no avail master2
                reply->set_host("");
                reply->set_port("");
                return Status::OK;
            }
        }

        return Status::OK;
    }



private:
    //takes master1(host-port) as input returns an active master2(host-port)
    Host_port get_server(std::string master1_host, std::string master1_port)
    {
        sleep(1); //without this client often gets dead servers as master2

        // initial selection of avail server
        // or the available server died
        // or the available server is on the same machine as the master
        if (available == -1 || !servers[available].active || servers[available].host == master1_host)
        {
            //make sure master2 is from diff VM
            for (int i = 0; i < servers.size(); i++)
            {
                if (servers[i].active && servers[i].host != master1_host)
                {
                    available = i;
                    break;
                }
            }
        }

        // to load balance, lets change the available server after each
        // successful connection of a client
        Host_port hp;
        hp.set_host(servers[available].host);
        hp.set_port(servers[available].port);
        available = ++available % servers.size();
        return hp;
    }

private:
    int available; //available server index
    std::vector<ServerInfo> servers;
};



void RunServer(std::string port_no)
{
    std::string server_address = "0.0.0.0:" + port_no;
    SNSServiceImpl service;
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Routing server listening on " << server_address << std::endl;
    server->Wait();
}



int main(int argc, char** argv)
{
    //hard coded port no for the router
    //i choose 3410, far away from the 3000 series
    std::string port = "3410";
    //run the server
    RunServer(port);
    return 0;
}

