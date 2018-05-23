
#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <thread>
#include <algorithm>
#include <fstream>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/prepared_statement.h>
#include <chrono>
#include <mutex>
#include "sns.grpc.pb.h"


enum SqlStatus
{
    SUCCESS,
    FAILURE_ALREADY_EXISTS,
    FAILURE_NOT_EXISTS,
    FAILURE_ALREADY_FOLLOWING,
    FAILURE_NOT_FOLLOWING,
    FAILURE_UNKNOWN
};


using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using google::protobuf::Timestamp;
using google::protobuf::Duration;
using google::protobuf::Empty;
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
using csce438::ClientPosts;

int database_login(std::string username);
int database_add_follower(std::string username, std::string follower);
int database_unfollow(std::string usernameFollowing, std::string usernameToBeUnfollowed);
int database_add_post(std::string username, std::string content, time_t timestamp);
void database_get_users_and_followers(std::string username, std::vector<std::string>& users, std::vector<std::string>& followers);

std::vector<std::string> database_get_followers(std::string username);
std::vector<std::string> recent_20(std::string username, std::vector<std::string> followers);


struct Client
{
    std::string username;
    bool connected = true;
    ServerReaderWriter<Message, Message>* stream = 0;
    bool operator==(const Client& c1) const
    {
        return (username == c1.username);
    }
    std::vector<std::string> posts;
};


//keep these
std::string mysql_server_address;

class SNSServiceImpl final : public SNSService::Service
{
private:
    std::string _host = "";
    std::string _port = "";
    std::string _rhost = "";
    std::string _rport = "";
    std::string _buddy_host = "";
    std::string _buddy_port = "";
    std::vector<Client> client_db;
    std::mutex client_db_mutex;


public:

    // the slave server calls this function to talk to the parent
    SNSServiceImpl(std::string host, std::string port)
        : _host(host)
        , _port(port)
        , _buddy_host()
        , _buddy_port()
        , client_db()
        , client_db_mutex()
    {
    }

    SNSServiceImpl() = delete;

    void talk_to_buddy(std::string host, std::string port)
    {
        _buddy_host = host;
        _buddy_port = port;
        pthread_t th;
        pthread_attr_t ta;
        pthread_attr_init(&ta);
        pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);
        pthread_create(&th, NULL, &SNSServiceImpl::__talk_to_buddy_helper__, this);
    }

    // both servers call this to register with the router
    void talk_to_router(std::string host, std::string port)
    {
        _rhost = host;
        _rport = port;
        pthread_t th;
        pthread_attr_t ta;
        pthread_attr_init(&ta);
        pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);
        pthread_create(&th, NULL, &SNSServiceImpl::__talk_to_router_helper__, this);
    }

    void make_buddy(std::string parent_host, std::string parent_port, std::string host, std::string port, std::string rhost)
    {
        // fork a new process
        if (fork() == 0)
        {
            execl("./tsd", "tsd", "-h", parent_host.c_str(), "-r", rhost.c_str(), "-s", port.c_str(), "-p", parent_port.c_str());
        }
        else
        {
            _buddy_port = port;
            _buddy_host = host;
            return;
        }
    }

private:
    static void* __talk_to_buddy_helper__(void* arg)
    {
        return ((SNSServiceImpl*)arg)->__talk_to_buddy__();
    }

    void* __talk_to_buddy__(void)
    {
        // this function should open up a connection with the master
        // if the master dies, this server should become the master
        // by making a new slave
        // Create brand new Stub instance(BuddyConnection)
        std::unique_ptr<SNSService::Stub> stub(SNSService::NewStub(
                grpc::CreateChannel(_buddy_host + ":" + _buddy_port, grpc::InsecureChannelCredentials())));
        //clientcontext
        ClientContext context;
        //client readerwriter
        std::shared_ptr<ClientReaderWriter<Host_port, Host_port>> stream(
                    stub->BuddyConnection(&context));
        //now entering the threaded R/W
        std::thread writer([stream]()
        {
            Host_port empty;

            while (stream->Write(empty))
            {
            }

            stream->WritesDone();
        });
        std::thread reader([stream]()
        {
            Host_port empty;

            while (stream->Read(&empty))
            {
            }
        });
        writer.join();
        reader.join();
        // when we get here, we know that our buddy has died
        make_buddy(_host, _port, _host, std::to_string(std::atoi(_port.c_str()) + 1), _rhost);
        return (void*)0;
    }

    static void* __talk_to_router_helper__(void* arg)
    {
        return ((SNSServiceImpl*)arg)->__talk_to_router__();
    }

    void* __talk_to_router__(void)
    {
        // Create brand new Stub instance(Bidirectional_r_and_s)
        std::unique_ptr<SNSService::Stub> stub(SNSService::NewStub(
                grpc::CreateChannel(_rhost + ":" + _rport, grpc::InsecureChannelCredentials())));
        //clientcontext
        ClientContext context;
        //client readerwriter
        std::shared_ptr<ClientReaderWriter<RouterMessage, RouterMessage>> stream(
                    stub->Bidirectional_r_and_s(&context));
        //now entering the threaded R/W
        std::thread writer([stream, this]()
        {
            // make the initial connection
            RouterMessage rm;
            Host_port hp;
            hp.set_host(_host);
            hp.set_port(_port);
            rm.set_allocated_hp(&hp);
            rm.set_operation(csce438::CONNECT);
            stream->Write(rm);
            rm.release_hp();
            rm.set_operation(csce438::NONE);
            auto start = std::chrono::high_resolution_clock::now();

            while (1)
            {
                auto end = std::chrono::high_resolution_clock::now();

                // every few seconds, we need to ask the router for the other machines
                // in the network to broadcast the posts
                if ((end - start) > std::chrono::seconds(5))
                {
                    rm.set_operation(csce438::BCAST);
                    stream->Write(rm);
                    stream->Read(&rm);
                    rm.set_operation(csce438::NONE);
                    client_db_mutex.lock();

                    // write each of the posts to the disc and send those files to the other machines.
                    //so this for loop basically constantly checks every CLient objects in client_db
                    //and sees if any client has a new post. if it hasm then write on txt file and sedn to cli
                    for (int i = 0; i < client_db.size(); ++i)
                    {
                        Client* c = &client_db[i];

                        for (int i = 0; i < rm.machines_size(); ++i)
                        {
                            if (c->posts.size() > 0)
                            {
                                broadcast_to_server(rm.machines(i).host(), rm.machines(i).port(), c->posts, c->username);
                            }
                        }

                        std::ofstream ofs;
                        ofs.open(c->username + "_posts.txt", std::ios_base::app);

                        for (std::string& post : c->posts)
                        {
                            ofs << post;
                        }

                        ofs.flush();
                        ofs.close();
                        c->posts.clear();
                    }

                    client_db_mutex.unlock();
                    rm.clear_machines();
                    start = std::chrono::high_resolution_clock::now();
                }
                else
                {
                    // decreases the bandwidth so that there is less busy wait
                    sleep(1);
                }
            }

            stream->WritesDone();
        });
        //Wait for the threads to finish
        writer.join();
        return (void*)0;
    }


    //Onetime rpc
    //between server and buddy,
    //this rpc receives updates(username+post) and sends the updates to the followers(no writing to txt file)
    Status Onetime_s_to_buddy(ServerContext* context, const Message* request,
                              Message* reply) override
    {
        // send this posts to all of the followers of username
        std::string temp = "";
        Message message;
        message.set_msg(request->msg());
        std::vector<std::string> followers = database_get_followers(request->username());
        client_db_mutex.lock();

        //std::cout << "searching for connected clients that follow " << request->username() << std::endl;
        for (std::string& follower : followers)
        {
            for (Client& c : client_db)
            {
                if (c.stream != 0 && c.connected && c.username == follower)
                {
                    c.stream->Write(message);
                }
            }
        }

        client_db_mutex.unlock();
        //std::cout << "Received a post update from another server." << std::endl;
        return Status::OK;
    }



    //BIdirectional server side rpc to communicate between server and client
    Status Bidirectional_s_and_c(ServerContext* context,
                                 ServerReaderWriter<Simple_string, Simple_string>* strm) override
    {
        Simple_string s;
        int count = 0; //semaphore

        while (strm->Read(&s))
        {
            //as long as were inside,
            //means client and server are connected
            if (count == 0)
            {
                count = 1;
                std::cout << "server received a client!" << std::endl;
            }
        }

        //while loop failed so means client disconnected
        std::cout << "client disconnected from server" << std::endl;
        return Status::OK;
    }


    Status List(ServerContext* context, const Request* request, ListReply* list_reply) override
    {
        std::vector<std::string> users;
        std::vector<std::string> followers;
        database_get_users_and_followers(request->username(), users, followers);

        for (auto c : users)
        {
            list_reply->add_all_users(c);
        }

        for (auto c : followers)
        {
            list_reply->add_followers(c);
        }

        return Status::OK;
    }

    Status Follow(ServerContext* context, const Request* request, Reply* reply) override
    {
        std::string follower = request->username();
        std::string username = request->arguments(0);
        int rv = database_add_follower(username, follower);

        if (rv == FAILURE_NOT_EXISTS)
        {
            reply->set_msg("Join Failed -- Invalid Username");
        }
        else if (rv == FAILURE_ALREADY_FOLLOWING)
        {
            reply->set_msg("Join Failed -- Already Following User");
        }
        else if (rv == SUCCESS)
        {
            reply->set_msg("Follow Successful");
        }
        else
        {
            reply->set_msg("Unknown Error");
        }

        return Status::OK;
    }

    Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override
    {
        std::string username = request->username();
        std::string unfollow = request->arguments(0);
        int rv = database_unfollow(username, unfollow);

        if (rv == FAILURE_NOT_EXISTS)
        {
            reply->set_msg("Leave Failed -- Invalid Username");
        }
        else if (rv == FAILURE_NOT_FOLLOWING)
        {
            reply->set_msg("Leave Failed -- Not Following User");
        }
        else if (rv == SUCCESS)
        {
            std::cout << "successful leave" << std::endl;
            reply->set_msg("Leave Successful");
        }
        else
        {
            reply->set_msg("Unknown Error");
        }

        return Status::OK;
    }

    Status Login(ServerContext* context, const Request* request, Reply* reply) override
    {
        // login is really just an insert, if it fails
        // it could mean that the user is already logged in
        int rv = database_login(request->username());
        client_db_mutex.lock();

        // if the client is logged in
        if (std::find_if(client_db.begin(),
                         client_db.end(),
                         [request] (const Client & c)
    {
        return c.username == request->username();
        }) != client_db.end())
        {
            reply->set_msg("Invalid Username");

            for (Client& c : client_db)
            {
                if (c.connected == false && c.username == request->username())
                {
                    std::string msg = "Welcome Back " + c.username;
                    reply->set_msg(msg);
                    c.connected = true;
                    break;
                }
            }
        }
        else
        {
            Client c;
            c.username = request->username();
            client_db.push_back(c);
            reply->set_msg("Login Successful!");
        }

        client_db_mutex.unlock();
        return Status::OK;
    }

    Status Timeline(ServerContext* context,
                    ServerReaderWriter<Message, Message>* stream) override
    {
        Message message;
        Client* c;
        Client* c1;

        while (stream->Read(&message))
        {
            std::string username = message.username(); //user who sent the msg
            google::protobuf::Timestamp temptime = message.timestamp();

            //getting the client obj from client_db by searching via username
            client_db_mutex.lock();

            for (Client& client : client_db)
            {
                if (client.username == message.username())
                {
                    c = &client;  //c = obj of user who made the post
                    break;
                }
            }
            client_db_mutex.unlock();

            //this block is used to push new posts to the client's Client obj
            if (message.msg() != "Set Stream")
            {
                client_db_mutex.lock();
                time_t timestamp = google::protobuf::util::TimeUtil::TimestampToTimeT(temptime);
                std::tm* ptm = std::localtime(&timestamp);
                char buffer[32];
                std::strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", ptm);
                c->posts.push_back(message.msg());
                client_db_mutex.unlock();

                //Send the message to each follower's stream right after a new post arrives from username
                std::vector<std::string> followers = database_get_followers(username);

                //first update my own buddy on same VM
                send_updates_to_buddy(message.msg(), username);

                client_db_mutex.lock();

                for (std::string& follower : followers)
                {
                    for (Client& client : client_db)
                    {
                        //c1 = &client;
                        if (client.stream != 0 && client.connected &&
                                client.username == follower)
                        {
                            client.stream->Write(message);
                        }
                    }
                }

                client_db_mutex.unlock();
            }
            //If message = "Set Stream",
            //it sets the Client obj stream, and
            //calculate the 20 recent posts of client's followees and send it back to client
            //this block only executes once, right after client goes to TIMELINE
            else
            {
                //set the stream in client_db obj
                client_db_mutex.lock();

                if (c->stream == 0)
                {
                    c->stream = stream;
                }

                client_db_mutex.unlock();
                //calculating followees vector of the user(to pull recent 20s)
                std::vector<std::string> followees;
                std::vector<std::string> users;
                std::vector<std::string> followers;
                database_get_users_and_followers(username, users, followers);

                for (int i = 0; i < users.size(); i++)
                {
                    std::vector<std::string> followers_temp = database_get_followers(users[i]);

                    for (int j = 0; j < followers_temp.size(); j++)
                    {
                        if (followers_temp[j] == username)
                        {
                            followees.push_back(users[i]);
                        }
                    }

                    followers_temp.clear();//to be safe
                }

                // get the last 20 posts of username and of the users this username follows(keyword:followees)
                std::vector<std::string> newest_twenty = recent_20(username, followees);
                //Send the 20 followees posts to the client to be displayed
                Message new_msg;
                for (int i = 0; i < newest_twenty.size(); i++)
                {
                    //std::cout << newest_twenty[i] << " "; //DELETE
                    new_msg.set_msg(newest_twenty[i]);
                    stream->Write(new_msg);
                }

                //std::cout << std::endl; //DELETE
                //std::cout << "timeliner client should get the recent 20" << std::endl; //DELETE
                // go back to the while condition and wait for the next message
                continue;
            }
        }//end of the while loop

        //reaching here means our client has disconnected from timeline mode
        //If the client disconnected from Chat Mode, set connected to false
        std::cout << message.username() + " has disconnected from server while in timeline" << std::endl; //DELETE
        client_db_mutex.lock();
        c->connected = false;
        client_db_mutex.unlock();
        return Status::OK;
    }

    Status BuddyConnection(ServerContext* context,
                           ServerReaderWriter<Host_port, Host_port>* stream) override
    {
        // this is the master server side of the buddy communication
        // when the stream fails, the slave died, so we make a new one and wait
        // for it to connect
        Host_port empty;
        auto start = std::chrono::high_resolution_clock::now();

        while (1)
        {
            auto end = std::chrono::high_resolution_clock::now();

            if ((end - start) > std::chrono::seconds(3))
            {
                if (stream->Read(&empty))
                {
                    start = std::chrono::high_resolution_clock::now();
                    continue;
                }
                else
                {
                    // when we get here, we know that our buddy has died
                    make_buddy(_host, _port, _host, std::to_string(std::atoi(_port.c_str()) + 1), _rhost);
                    return Status::OK;
                }
            }
            else
            {
                sleep(1);
            }
        }
    }

    Status BroadcastPosts(ServerContext* context, const ClientPosts* request, Empty* reply) override
    {
        std::ofstream ofs;
        ofs.open(request->username() + "_posts.txt", std::ios_base::app);
        // send this posts to all of the followers of username
        std::string temp = "";

        for (int i = 0; i < request->posts_size(); ++i)
        {
            temp = temp + request->posts(i) + "\n";
        }

        Message message;
        message.set_msg(temp);
        std::vector<std::string> followers = database_get_followers(request->username());
        client_db_mutex.lock();

        for (std::string& follower : followers)
        {
            for (Client& c : client_db)
            {
                if (c.stream != 0 && c.connected && c.username == follower)
                {
                    c.stream->Write(message);
                }
            }
        }

        client_db_mutex.unlock();

        //actualy writing on txt file happening here
        for (int i = 0; i < request->posts_size(); ++i)
        {
            ofs << request->posts(i);
        }

        ofs.flush();
        ofs.close();
        //send this updates to the buddy server of the same VM
        send_updates_to_buddy(temp, request->username());
        return Status::OK;
    }

    //sends the updates over different VMs
    void broadcast_to_server(std::string host, std::string port, std::vector<std::string> posts, std::string username)
    {
        //std::cout << "(" << username << ", " << posts.size() << ") ";
        //std::cout << "sending the posts to another VM:" << host + ":" + port << std::endl;
        std::unique_ptr<SNSService::Stub> stub(SNSService::NewStub(
                grpc::CreateChannel(host + ":" + port, grpc::InsecureChannelCredentials())
                                               ));
        Empty empty;
        ClientPosts cp;
        cp.set_username(username);

        for (const std::string& post : posts)
        {
            cp.add_posts(post);
        }

        std::string updates = "";

        for (int i = 0; i < posts.size(); i++)
        {
            updates = updates + posts[i] + "\n";
        }

        ClientContext context;
        Status status = stub->BroadcastPosts(&context, cp, &empty);
    }

    //sends updates to buddy on the same machine. bussy uses it to notify timeliners
    void send_updates_to_buddy(std::string posts, std::string username)
    {
        //stub
        //std::cout << "sending buddy stuff to " << _buddy_host + ":" + _buddy_port << std::endl;
        std::unique_ptr<SNSService::Stub> stub(SNSService::NewStub(
                grpc::CreateChannel(_buddy_host + ":" + _buddy_port, grpc::InsecureChannelCredentials())
                                               ));
        //this is a request
        Message req;
        req.set_msg(posts);
        req.set_username(username);
        //this is a reply holder
        Message reply;
        //CLientContext
        ClientContext master_context;
        //sending and receiving happening here
        Status master_status = stub->Onetime_s_to_buddy(&master_context, req, &reply);

        if (reply.msg() == "buddy_received_updates!")
        {
            //std::cout << "Slave server has rceived the post updates" << std::endl;
        }
    }
};


void database_init(std::string mysql_host)
{
    mysql_server_address = mysql_host + ":3306";

    // create all of the tables if they don't already exist
    try
    {
        sql::Driver* driver;
        sql::Connection* con;
        sql::PreparedStatement* prep_stmt;
        driver = get_driver_instance();
        con = driver->connect(mysql_server_address, "root", "root");
        prep_stmt = con->prepareStatement("CREATE DATABASE IF NOT EXISTS tinysns");
        prep_stmt->executeUpdate();
        delete prep_stmt;
        con->setSchema("tinysns");
        prep_stmt = con->prepareStatement("CREATE TABLE IF NOT EXISTS User(id INTEGER AUTO_INCREMENT PRIMARY KEY, name CHAR(255) NOT NULL)");
        prep_stmt->executeUpdate();
        delete prep_stmt;
        prep_stmt = con->prepareStatement("CREATE TABLE IF NOT EXISTS Follower(id INTEGER NOT NULL, followerId INTEGER NOT NULL)");
        prep_stmt->executeUpdate();
        delete prep_stmt;
        prep_stmt = con->prepareStatement("CREATE TABLE IF NOT EXISTS Timeline(id INTEGER NOT NULL, timeStamp TIMESTAMP NOT NULL, content TEXT NOT NULL)");
        prep_stmt->executeUpdate();
        delete prep_stmt;
        delete con;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "# ERR: SQLException in " << __FILE__;
        std::cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << std::endl;
        std::cerr << "# ERR: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
    }
    catch (std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int get_user_id(std::string username)
{
    // grab the id of a user from the database based off of their name
    try
    {
        sql::Driver* driver;
        sql::Connection* con;
        sql::PreparedStatement* prep_stmt;
        sql::ResultSet* res;
        driver = get_driver_instance();
        con = driver->connect(mysql_server_address, "root", "root");
        con->setSchema("tinysns");
        prep_stmt = con->prepareStatement("SELECT id FROM User WHERE name = ?");
        prep_stmt->setString(1, username);
        res = prep_stmt->executeQuery();
        int id = 0;

        while (res->next())
        {
            id = res->getInt("id");
            delete prep_stmt;
            delete res;
            delete con;
            return id;
        }

        delete prep_stmt;
        delete res;
        delete con;
        return 0;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "# ERR: SQLException in " << __FILE__;
        std::cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << std::endl;
        std::cerr << "# ERR: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return FAILURE_UNKNOWN;
    }
    catch (std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl << "on line: " << __LINE__ << std::endl;
        return FAILURE_UNKNOWN;
    }
}

int database_login(std::string username)
{
    try
    {
        sql::Driver* driver;
        sql::Connection* con;
        sql::PreparedStatement* prep_stmt;
        driver = get_driver_instance();
        con = driver->connect(mysql_server_address, "root", "root");
        con->setSchema("tinysns");
        int id = get_user_id(username);

        if (id != 0)
        {
            // if the id is already in the database, it is possible that the user is already logged in
            delete con;
            return FAILURE_ALREADY_EXISTS;
        }

        // put the new user into the database
        prep_stmt = con->prepareStatement("INSERT INTO User(name) VALUES(?)");
        prep_stmt->setString(1, username);
        prep_stmt->executeUpdate();
        delete prep_stmt;
        delete con;
        return SUCCESS;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "# ERR: SQLException in " << __FILE__;
        std::cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << std::endl;
        std::cerr << "# ERR: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return FAILURE_UNKNOWN;
    }
    catch (std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl << "on line: " << __LINE__ << std::endl;
        return FAILURE_UNKNOWN;
    }
}

int database_add_follower(std::string username, std::string follower)
{
    try
    {
        sql::Driver* driver;
        sql::Connection* con;
        sql::PreparedStatement* prep_stmt;
        sql::ResultSet* res;
        driver = get_driver_instance();
        con = driver->connect(mysql_server_address, "root", "root");
        con->setSchema("tinysns");
        int id = get_user_id(username);
        int followerId = get_user_id(follower);

        if (id == 0 || followerId == 0)
        {
            // if either of the id's are 0, then at least one of them doesn't exist
            delete con;
            return FAILURE_NOT_EXISTS;
        }

        // add the follower
        prep_stmt = con->prepareStatement("SELECT followerId FROM Follower WHERE followerId = ? AND id = ?");
        prep_stmt->setInt(1, followerId);
        prep_stmt->setInt(2, id);
        res = prep_stmt->executeQuery();
        int exists = 0;

        while (res->next())
        {
            exists = res->getInt("followerId");

            if (exists != 0)
            {
                delete prep_stmt;
                delete res;
                delete con;
                return FAILURE_ALREADY_FOLLOWING;
            }
        }

        prep_stmt = con->prepareStatement("INSERT INTO Follower(id, followerId) VALUES(?, ?)");
        prep_stmt->setInt(1, id);
        prep_stmt->setInt(2, followerId);
        prep_stmt->executeUpdate();
        delete prep_stmt;
        delete res;
        delete con;
        return SUCCESS;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "# ERR: SQLException in " << __FILE__;
        std::cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << std::endl;
        std::cerr << "# ERR: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return FAILURE_UNKNOWN;
    }
    catch (std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl << "on line: " << __LINE__ << std::endl;
        return FAILURE_UNKNOWN;
    }
}

int database_unfollow(std::string usernameFollowing, std::string usernameToBeUnfollowed)
{
    try
    {
        sql::Driver* driver;
        sql::Connection* con;
        sql::PreparedStatement* prep_stmt;
        sql::ResultSet* res;
        driver = get_driver_instance();
        con = driver->connect(mysql_server_address, "root", "root");
        con->setSchema("tinysns");
        int followingId = get_user_id(usernameFollowing);
        int idToBeUnfollowed = get_user_id(usernameToBeUnfollowed);

        if (followingId == 0 || idToBeUnfollowed == 0)
        {
            delete con;
            return FAILURE_NOT_EXISTS;
        }

        // check if idToBeUnfollowed is currently followed by followingId
        prep_stmt = con->prepareStatement("SELECT followerId FROM Follower WHERE id = ? AND followerId = ?");
        prep_stmt->setInt(1, idToBeUnfollowed);
        prep_stmt->setInt(2, followingId);
        res = prep_stmt->executeQuery();
        bool isAFollower = false;

        while (res->next())
        {
            int follower = res->getInt("followerId");

            // if the returned id is the same as followingId,
            // then followingId is in fact following idToBeUnfollowed
            if (follower == followingId)
            {
                isAFollower = true;
            }
        }

        if (isAFollower == false)
        {
            std::cout << "was not a follower" << std::endl;
            delete prep_stmt;
            delete res;
            delete con;
            return FAILURE_NOT_FOLLOWING;
        }

        // delete the follower
        prep_stmt = con->prepareStatement("DELETE FROM Follower WHERE id = ? AND followerId = ?");
        prep_stmt->setInt(1, idToBeUnfollowed);
        prep_stmt->setInt(2, followingId);
        prep_stmt->executeUpdate();
        delete prep_stmt;
        delete res;
        delete con;
        return SUCCESS;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "# ERR: SQLException in " << __FILE__;
        std::cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << std::endl;
        std::cerr << "# ERR: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return FAILURE_UNKNOWN;
    }
    catch (std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl << "on line: " << __LINE__ << std::endl;
        return FAILURE_UNKNOWN;
    }
}

int database_add_post(std::string username, std::string content, time_t timestamp)
{
    try
    {
        sql::Driver* driver;
        sql::Connection* con;
        sql::PreparedStatement* prep_stmt;
        driver = get_driver_instance();
        con = driver->connect(mysql_server_address, "root", "root");
        con->setSchema("tinysns");
        int id = get_user_id(username);

        if (id == 0)
        {
            delete con;
            return FAILURE_NOT_EXISTS;
        }

        // convert the timestamp into a datetime object that SQL can use
        std::tm* ptm = std::localtime(&timestamp);
        char buffer[32];
        std::strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", ptm);
        prep_stmt = con->prepareStatement("INSERT INTO Timeline(id, timeStamp, content) VALUES(?, ?, ?)");
        prep_stmt->setInt(1, id);
        prep_stmt->setDateTime(2, std::string(buffer));
        prep_stmt->setString(3, content);
        prep_stmt->executeUpdate();
        delete prep_stmt;
        delete con;
        return SUCCESS;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "# ERR: SQLException in " << __FILE__;
        std::cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << std::endl;
        std::cerr << "# ERR: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return FAILURE_UNKNOWN;
    }
    catch (std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl << "on line: " << __LINE__ << std::endl;
        return FAILURE_UNKNOWN;
    }
}



void database_get_users_and_followers(std::string username, std::vector<std::string>& users, std::vector<std::string>& followers)
{
    try
    {
        sql::Driver* driver;
        sql::Connection* con;
        sql::PreparedStatement* prep_stmt;
        sql::ResultSet* res;
        driver = get_driver_instance();
        con = driver->connect(mysql_server_address, "root", "root");
        con->setSchema("tinysns");
        users.clear();
        followers.clear();
        // get the followers
        followers = database_get_followers(username);
        int id = get_user_id(username);

        if (id == 0)
        {
            delete con;
            return;
        }

        // get the names of all users in the database
        prep_stmt = con->prepareStatement("SELECT name FROM User");
        res = prep_stmt->executeQuery();

        while (res->next())
        {
            users.push_back(res->getString("name"));
        }

        delete prep_stmt;
        delete res;
        delete con;
        return;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "# ERR: SQLException in " << __FILE__;
        std::cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << std::endl;
        std::cerr << "# ERR: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return;
    }
    catch (std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl << "on line: " << __LINE__ << std::endl;
        return;
    }
}

std::vector<std::string> database_get_followers(std::string username)
{
    try
    {
        sql::Driver* driver;
        sql::Connection* con;
        sql::PreparedStatement* prep_stmt;
        sql::ResultSet* res;
        driver = get_driver_instance();
        con = driver->connect(mysql_server_address, "root", "root");
        con->setSchema("tinysns");
        std::vector<std::string> followers;
        int id = get_user_id(username);

        if (id == 0)
        {
            delete con;
            return std::vector<std::string>();
        }

        // get the name of each user that username is following
        prep_stmt = con->prepareStatement("SELECT User.name FROM Follower INNER JOIN User ON User.id=Follower.followerId WHERE Follower.id = ?");
        prep_stmt->setInt(1, id);
        res = prep_stmt->executeQuery();

        while (res->next())
        {
            followers.push_back(res->getString("name"));
        }

        delete prep_stmt;
        delete res;
        delete con;
        return followers;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "# ERR: SQLException in " << __FILE__;
        std::cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << std::endl;
        std::cerr << "# ERR: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return std::vector<std::string>();
    }
    catch (std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl << "on line: " << __LINE__ << std::endl;
        return std::vector<std::string>();
    }
}

std::vector<std::string> recent_20(std::string username, std::vector<std::string> followers)
{
    std::vector<std::string> users_posts;//this gets returned
    std::vector<std::string> ret;
    std::ifstream ifs(username + "_posts.txt");

    for (std::string line; std::getline( ifs, line ); )
    {
        users_posts.push_back(line);
    }

    ifs.close();

    int n_posts = 0;
    //std::cout << "grabbing user's posts" << std::endl;
    for(auto ptr = users_posts.rbegin(); n_posts < 20 && ptr != users_posts.rend(); ++ptr)
    {
        ++n_posts;
        ret.push_back(*ptr);
    }

    //std::cout << "grabbing follower's posts" << std::endl;
    for (std::string uname : followers)
    {
        std::ifstream ifs(uname + "_posts.txt");
        std::vector<std::string> fposts;

        for (std::string line; std::getline( ifs, line ); )
        {
            fposts.push_back(line);
        }

        ifs.close();

        n_posts = 0;
        for(auto ptr = fposts.rbegin(); n_posts < 20 && ptr != fposts.rend(); ++ptr)
        {
            ++n_posts;
            ret.push_back(*ptr);
        }
        fposts.clear();
    }
    std::cout << "done" << std::endl;

    return ret;
}

void RunSlave(std::string parent_host, std::string parent_port, std::string host, std::string port, std::string rhost, std::string rport)
{
    database_init(rhost);
    std::string server_address = host + ":" + port;
    SNSServiceImpl service(host, port);
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    // talk to the parent server
    service.talk_to_router(rhost, rport);
    service.talk_to_buddy(parent_host, parent_port);
    server->Wait();
}

void RunServer(std::string host, std::string port, std::string rhost, std::string rport)
{
    database_init(rhost);
    std::string server_address = host + ":" + port;
    SNSServiceImpl service(host, port);
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    service.talk_to_router(rhost, rport);
    service.make_buddy(host, port, host, std::to_string(std::atoi(port.c_str()) + 1), rhost);
    server->Wait();
}



int main(int argc, char** argv)
{
    std::string host{""};
    std::string port{"3010"};
    std::string rhost{""};
    std::string rport{"3410"};
    std::string slave_port{""};
    int opt = 0;

    while ((opt = getopt(argc, argv, "p:h:r:s:")) != -1)
    {
        switch (opt)
        {
        case 'p':
            port = optarg;
            break;

        case 'h':
            host = optarg;
            break;

        case 'r':
            rhost = optarg;
            break;

        case 's':
            slave_port = optarg;
            break;

        default:
            std::cerr << "Invalid Command Line Argument\n";
        }
    }

    if (host == "")
    {
        std::cout << "please enter server host address that is not " << std::endl;
        std::cout << "format: ./tsd -p serverport -h serverIP -r routerIP" << std::endl;
        exit(0);
    }

    if (slave_port != "")
    {
        RunSlave(host, port, host, slave_port, rhost, rport);
    }
    else
    {
        RunServer(host, port, rhost, rport);
    }

    return 0;
}
