#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "client.h"
#include <pthread.h>
#include <thread>
#include <google/protobuf/util/time_util.h>
#include "sns.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
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

std::string ghost1 = "";  //master1 host
std::string gport1 = "";  //master1 port
std::string ghost2 = "";  //master2 host
std::string gport2 = "";  //master2 port
std::string guser = "";
std::string rhost = "";
std::string rport = "3410";
bool stress_test = false;
int max_posts = 1000;
int test = 0;

Message MakeMessage(const std::string& username, const std::string& msg)
{
    Message m;
    m.set_username(username);
    m.set_msg(msg);
    google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
    timestamp->set_seconds(time(NULL));
    timestamp->set_nanos(0);
    m.set_allocated_timestamp(timestamp);
    return m;
}

class Client : public IClient
{
public:
    Client(const std::string& hname,
           const std::string& uname,
           const std::string& p)
        : hostname(hname), username(uname), port(p)
    {}

    void talk_to_server()
    {
        pthread_t th;
        pthread_attr_t ta;
        pthread_attr_init(&ta);
        pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);
        pthread_create(&th, NULL, &Client::__talk_to_server__helper__, this);
    }
protected:
    virtual int connectTo();
    virtual IReply processCommand(std::string& input);
    virtual void processTimeline();
private:
    static void* __talk_to_server__helper__(void* arg)
    {
        return ((Client*)arg)->__talk_to_server__();
    }

    void* __talk_to_server__(void);

private:
    std::string hostname;
    std::string username;
    std::string port;
    // You can have an instance of the client stub
    // as a member variable.
    std::unique_ptr<SNSService::Stub> stub_;

    IReply Login();
    IReply List();
    IReply Follow(const std::string& username2);
    IReply UnFollow(const std::string& username2);
    void Timeline(const std::string& username);
};



//input: router_host
//output:assigns master1 information in ghost1 gport1
void get_master1(std::string rhost)
{
    std::unique_ptr<SNSService::Stub> stub(SNSService::NewStub(
            grpc::CreateChannel(rhost + ":" + rport, grpc::InsecureChannelCredentials())
                                           ));
    //this is a request
    Simple_string master1_req;
    master1_req.set_str("give_me_a_master_1");
    master1_req.set_master_host("nothing");
    master1_req.set_master_port("nothing");
    //this is a reply holder
    Host_port master1_reply;
    //CLientContext
    ClientContext master_context;
    //sending and receiving happening here
    Status master_status = stub->Onetime_c_to_r(&master_context, master1_req, &master1_reply);
    std::cout << "primary master info: " << master1_reply.host() + ":" + master1_reply.port()
              << std::endl;

    if (master1_reply.host() == "" || master1_reply.port() == "")
    {
        std::cout << "No available server found for master1." << std::endl;
        exit(0);
    }
    else
    {
        ghost1 = master1_reply.host();
        gport1 = master1_reply.port();
    }
}

//input: router_host, master1 host, master1 port ..
//output:assigns master2 information in ghost2 gport2
void get_master2(std::string rhost, std::string& master1_host, std::string& master1_port)
{
    std::unique_ptr<SNSService::Stub> stub(SNSService::NewStub(
            grpc::CreateChannel(rhost + ":" + rport, grpc::InsecureChannelCredentials())
                                           ));
    //this is a request
    Simple_string master2_req;
    master2_req.set_str("give_me_a_master_2");
    master2_req.set_master_host(master1_host);
    master2_req.set_master_port(master1_port);
    //this is a reply holder
    Host_port master2_reply;
    //CLientContext
    ClientContext master_context;
    //sending and receiving happening here
    Status master_status = stub->Onetime_c_to_r(&master_context, master2_req, &master2_reply);
    std::cout << "new secondary master(master2) info: " << master2_reply.host() + ":" + master2_reply.port()
              << std::endl;

    if (master2_reply.host() == "" || master2_reply.port() == "")
    {
        std::cout << "No available server found for master2." << std::endl;
        exit(0);
    }
    else
    {
        ghost2 = master2_reply.host();
        gport2 = master2_reply.port();
    }
}

//this function is run by a detached  thread
//solely used to maintain a bidir comm between server and client
void* Client::__talk_to_server__(void)
{
    // Create brand new Stub instance(BuddyConnection)
    std::unique_ptr<SNSService::Stub> stub(SNSService::NewStub(
            grpc::CreateChannel(ghost1 + ":" + gport1, grpc::InsecureChannelCredentials())));
    //clientcontext
    ClientContext context;
    //client readerwriter
    std::shared_ptr<ClientReaderWriter<Simple_string, Simple_string>> stream(
                stub->Bidirectional_s_and_c(&context));
    //now entering the threaded R/W
    std::thread writer([stream]()
    {
        Simple_string empty;

        while (stream->Write(empty))
        {
        }

        stream->WritesDone();
    });
    std::thread reader([stream]()
    {
        Simple_string empty;

        while (stream->Read(&empty))
        {
        }
    });
    writer.join();
    reader.join();
    //reaching here means server died
    std::cout << "master1 has died out..connecting to master2" << std::endl;
    //making master2 as master1
    ghost1 = ghost2;
    gport1 = gport2;
    std::cout << "master2 has become master1 and connected to server" << std::endl;
    //getting new master2, but before that go to sleep for half sec
    get_master2(rhost, ghost1, gport1);
    std::cout << "client got new master2" << std::endl;
    connectTo();
    return (void*)0;
}


int main(int argc, char** argv)
{
    //NOte: these are default variables which we will never used
    //we will always use ghost1, gport1, ghost2, gport2, rhost stuff
    std::string hostname = "localhost";
    std::string username = "default";
    std::string port = "3010";
    int opt = 0;

    while ((opt = getopt(argc, argv, "u:r:t:")) != -1)
    {
        switch (opt)
        {
        case 'r':
            rhost = optarg;
            get_master1(optarg); 	          //initializes ghost1, gport1
            get_master2(optarg, ghost1, gport1);  //initializes ghost2  gport2
            break;

        case 'u':
            username = optarg;
            guser = username;
            break;

	case 't':
	    stress_test = true;
	    max_posts = atoi(optarg);
	    break;

        default:
            std::cerr << "Invalid Command Line Argument\n";
        }
    }

    Client myc(ghost1, guser, gport1);
    // You MUST invoke "run_client" function to start business logic
    myc.run_client();
    return 0;
}

int Client::connectTo()
{
    // ------------------------------------------------------------
    //basically initializing the stub_
    // ------------------------------------------------------------
    std::cout << "connecting to: " << ghost1 + ":" + gport1 << std::endl;
    std::string login_info = ghost1 + ":" + gport1;
    stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(
                grpc::CreateChannel(
                    login_info, grpc::InsecureChannelCredentials())));
    IReply ire = Login();

    if (!ire.grpc_status.ok())
    {
        std::cout << "could not login" << std::endl;
        return -1;
        //this could fail due to master
        //dying between router sending this info and client connecting(which is unlikely),
        //OR,
        //username is already currently logged in
    }
    else
    {
        std::cout << "logged in" << std::endl;
        talk_to_server();
    }

    return 1;
}



IReply Client::processCommand(std::string& input)
{
    // ------------------------------------------------------------
    // ------------------------------------------------------------
    IReply ire;
    std::size_t index = input.find_first_of(" ");

    if (index != std::string::npos)
    {
        std::string cmd = input.substr(0, index);
        /*
        if (input.length() == index + 1) {
            std::cout << "Invalid Input -- No Arguments Given\n";
        }
        */
        std::string argument = input.substr(index + 1, (input.length() - index));

        if (cmd == "FOLLOW")
        {
            return Follow(argument);
        }
        else if (cmd == "UNFOLLOW")
        {
            return UnFollow(argument);
        }
    }
    else
    {
        if (input == "LIST")
        {
            return List();
        }
        else if (input == "TIMELINE")
        {
            ire.comm_status = SUCCESS;
            return ire;
        }
    }

    ire.comm_status = FAILURE_INVALID;
    return ire;
}

void Client::processTimeline()
{
    Timeline(username);
    // ------------------------------------------------------------
    // In this function, you are supposed to get into timeline mode.
    // You may need to call a service method to communicate with
    // the server. Use getPostMessage/displayPostMessage functions
    // for both getting and displaying messages in timeline mode.
    // You should use them as you did in hw1.
    // ------------------------------------------------------------
    // ------------------------------------------------------------
    // IMPORTANT NOTICE:
    //
    // Once a user enter to timeline mode , there is no way
    // to command mode. You don't have to worry about this situation,
    // and you can terminate the client program by pressing
    // CTRL-C (SIGINT)
    // ------------------------------------------------------------
}



IReply Client::List()
{
    //Data being sent to the server
    Request request;
    request.set_username(username);
    //Container for the data from the server
    ListReply list_reply;
    //Context for the client
    ClientContext context;
    Status status = stub_->List(&context, request, &list_reply);
    IReply ire;
    ire.grpc_status = status;

    //Loop through list_reply.all_users and list_reply.following_users
    //Print out the name of each room
    if (status.ok())
    {
        ire.comm_status = SUCCESS;
        std::string all_users;
        std::string following_users;

        for (std::string s : list_reply.all_users())
        {
            ire.all_users.push_back(s);
        }

        for (std::string s : list_reply.followers())
        {
            ire.followers.push_back(s);
        }
    }

    return ire;
}

IReply Client::Follow(const std::string& username2)
{
    Request request;
    request.set_username(username);
    request.add_arguments(username2);
    Reply reply;
    ClientContext context;
    Status status = stub_->Follow(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;

    if (reply.msg() == "Join Failed -- Invalid Username")
    {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    }
    else if (reply.msg() == "Join Failed -- Already Following User")
    {
        ire.comm_status = FAILURE_INVALID;
    }
    else if (reply.msg() == "Follow Successful")
    {
        ire.comm_status = SUCCESS;
    }
    else
    {
        ire.comm_status = FAILURE_UNKNOWN;
    }

    return ire;
}

IReply Client::UnFollow(const std::string& username2)
{
    Request request;
    request.set_username(username);
    request.add_arguments(username2);
    Reply reply;
    ClientContext context;
    Status status = stub_->UnFollow(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;

    if (reply.msg() == "Leave Failed -- Invalid Username")
    {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    }
    else if (reply.msg() == "Leave Failed -- Not Following User")
    {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    }
    else if (reply.msg() == "Leave Successful")
    {
        ire.comm_status = SUCCESS;
    }
    else
    {
        ire.comm_status = FAILURE_UNKNOWN;
    }

    return ire;
}



IReply Client::Login()
{
    Request request;
    request.set_username(username);
    Reply reply;
    ClientContext context;
    Status status = stub_->Login(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;

    if (reply.msg() == "you have already joined")
    {
        ire.comm_status = FAILURE_ALREADY_EXISTS;
    }
    else
    {
        ire.comm_status = SUCCESS;
    }

    return ire;
}



void Client::Timeline(const std::string& username)
{
    std::cout << "in timeline mode" << std::endl;
    ClientContext context;
    std::shared_ptr<ClientReaderWriter<Message, Message>> stream(
                stub_->Timeline(&context));
    //Thread used to read chat messages and send them to the server
    std::thread writer([username, stream]()
    {
        std::string input = "Set Stream";
        Message m = MakeMessage(username, input);
        stream->Write(m);

	//stress_test block
	if(stress_test){
	   using namespace std;
	   clock_t begin = clock();
	   for(int i=0; i<max_posts; i++){
	      this_thread::sleep_for(chrono::milliseconds(2));
	      cout <<"send message: "<< i << endl;
	      m = MakeMessage(username, to_string(i));
	      stream->Write(m);
	   }
	   clock_t end = clock();
	   cout <<"Execution time: " <<double(end-begin)/CLOCKS_PER_SEC << " seconds" <<endl;
	   stress_test = false;
	}

        while (1)
        {
            input = getPostMessage();
            m = MakeMessage(username, input);
            stream->Write(m);
        }

        stream->WritesDone();
    });
    std::thread reader([username, stream]()
    {
        Message m;

        while (stream->Read(&m))
        {
            google::protobuf::Timestamp temptime = m.timestamp();
            std::time_t time = temptime.seconds();
            displayPostMessage(m.username(), m.msg(), time);
        }
    });
    //Wait for the threads to finish
    writer.join();
    reader.join();
    // reconnect to the server
    
    //reaching here means server died
    std::cout << "master1 has died out..connecting to master2" << std::endl;
    //making master2 as master1
    ghost1 = ghost2;
    gport1 = gport2;
    std::cout << "master2 has become master1 and connected to server" << std::endl;
    //getting new master2, but before that go to sleep for half sec
    get_master2(rhost, ghost1, gport1);
    std::cout << "client got new master2" << std::endl;
    connectTo();
    Timeline(username);
}

