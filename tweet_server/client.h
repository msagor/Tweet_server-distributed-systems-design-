#include <iostream>
#include <string>
#include <ctime>
#include <vector>
#include <grpc++/grpc++.h>

#define MAX_DATA 256

enum IStatus
{
    SUCCESS,
    FAILURE_ALREADY_EXISTS,
    FAILURE_NOT_EXISTS,
    FAILURE_INVALID_USERNAME,
    FAILURE_INVALID,
    FAILURE_UNKNOWN
};

/*
 * IReply structure is designed to be used for displaying the
 * result of the command that has been sent to the server.
 * For example, in the "processCommand" function, you should
 * declare a variable of IReply structure and set based on
 * the type of command and the result.
 *
 * - FOLLOW/UNFOLLOW/TIMELINE command:
 * IReply ireply;
 * ireply.grpc_status = return value of a service method
 * ireply.comm_status = one of values in IStatus enum
 *
 * - LIST command:
 * IReply ireply;
 * ireply.grpc_status = return value of a service method
 * ireply.comm_status = one of values in IStatus enum
 * reply.users = list of all users who connected to the server at least onece
 * reply.followers = list of users who following current user;
 *
 * This structure is not for communicating between server and client.
 * You need to design your own rules for the communication.
 */
struct IReply
{
    grpc::Status grpc_status;
    enum IStatus comm_status;
    std::vector<std::string> all_users;
    std::vector<std::string> followers;
};

class IClient
{
public:
    void run_client()
    {
        run();
    }

protected:
    /*
     * Pure virtual functions to be implemented by students
     */
    virtual int connectTo() = 0;
    virtual IReply processCommand(std::string& cmd) = 0;
    virtual void processTimeline() = 0;

private:

    void run();

    void displayTitle() const;
    std::string getCommand() const;
    void displayCommandReply(const std::string& comm, const IReply& reply) const;
    void toUpperCase(std::string& str) const;
};

void IClient::run()
{
    int ret = connectTo();

    if (ret < 0)
    {
        std::cout << "connection failed: " << ret << std::endl;
        exit(1);
    }

    displayTitle();

    while (1)
    {
        std::string cmd = getCommand();
        IReply reply = processCommand(cmd);
        displayCommandReply(cmd, reply);

        if (reply.grpc_status.ok() && reply.comm_status == SUCCESS
                && cmd == "TIMELINE")
        {
            std::cout << "Now you are in the timeline" << std::endl;
            processTimeline();
        }
    }
}

void IClient::displayTitle() const
{
    std::cout << "\n========= TINY SNS CLIENT =========\n";
    std::cout << " Command Lists and Format:\n";
    std::cout << " FOLLOW <username>\n";
    std::cout << " UNFOLLOW <username>\n";
    std::cout << " LIST\n";
    std::cout << " TIMELINE\n";
    std::cout << "=====================================\n";
}

std::string IClient::getCommand() const
{
    std::string input;

    while (1)
    {
        std::cout << "Cmd> ";
        std::getline(std::cin, input);
        std::size_t index = input.find_first_of(" ");

        if (index != std::string::npos)
        {
            std::string cmd = input.substr(0, index);
            toUpperCase(cmd);

            if (input.length() == index + 1)
            {
                std::cout << "Invalid Input -- No Arguments Given\n";
                continue;
            }

            std::string argument = input.substr(index + 1, (input.length() - index));
            input = cmd + " " + argument;
        }
        else
        {
            toUpperCase(input);

            if (input != "LIST" && input != "TIMELINE")
            {
                std::cout << "Invalid Command\n";
                continue;
            }
        }

        break;
    }

    return input;
}

void IClient::displayCommandReply(const std::string& comm, const IReply& reply) const
{
    if (reply.grpc_status.ok())
    {
        switch (reply.comm_status)
        {
        case SUCCESS:
            std::cout << "Command completed successfully\n";

            if (comm == "LIST")
            {
                std::cout << "All users: ";

                for (std::string room : reply.all_users)
                {
                    std::cout << room << ", ";
                }

                std::cout << "\nFollowers: ";

                for (std::string room : reply.followers)
                {
                    std::cout << room << ", ";
                }

                std::cout << std::endl;
            }

            break;

        case FAILURE_ALREADY_EXISTS:
            std::cout << "Input username already exists, command failed\n";
            break;

        case FAILURE_NOT_EXISTS:
            std::cout << "Input username does not exists, command failed\n";
            break;

        case FAILURE_INVALID_USERNAME:
            std::cout << "Command failed with invalid username\n";
            break;

        case FAILURE_INVALID:
            std::cout << "Command failed with invalid command\n";
            break;

        case FAILURE_UNKNOWN:
            std::cout << "Command failed with unknown reason\n";
            break;

        default:
            std::cout << "Invalid status\n";
            break;
        }
    }
    else
    {
        std::cout << "grpc failed: " << reply.grpc_status.error_message() << std::endl;
    }
}

void IClient::toUpperCase(std::string& str) const
{
    std::locale loc;

    for (std::string::size_type i = 0; i < str.size(); i++)
    {
        str[i] = toupper(str[i], loc);
    }
}

/*
 * get/displayPostMessage functions will be called in chatmode
 */
std::string getPostMessage()
{
    char buf[MAX_DATA];

    while (1)
    {
        fgets(buf, MAX_DATA, stdin);

        if (buf[0] != '\n')
        {
            break;
        }
    }

    std::string message(buf);
    return message;
}

void displayPostMessage(const std::string& sender, const std::string& message, std::time_t& time)
{
    std::string t_str(std::ctime(&time));
    t_str[t_str.size() - 1] = '\0';
    std::cout << sender << "(" << t_str << ") >> " << message << std::endl;
}

void displayReConnectionMessage(const std::string& host, const std::string& port)
{
    std::cout << "Reconnecting to " << host << ":" << port << "..." << std::endl;
}
